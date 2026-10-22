// Pipelined HW SHA256d mining loop for classic ESP32, second generation.
//
// The schedule is derived from on-device measurement of the SHA engine
// (src/sha_bench*.cpp, -D SHA_BENCH=1..3). In 240 MHz CPU cycles:
//   - START/CONTINUE keep the engine busy ~68 cycles (one round per cycle);
//   - the engine samples TEXT[i] about i+1 cycles after the trigger and never
//     looks at TEXT again, so TEXT is free to rewrite from ~cycle 17 on;
//   - a compute leaves TEXT untouched; LOAD only writes TEXT[0..7], one word
//     per cycle, and a CPU store to TEXT while it runs damages the transfer.
// So the engine needs ~3*68 + 2*12 cycles per nonce, and the loop's job is to
// never keep it waiting. Everything except the TEXT[0..7] header refill after
// the final LOAD is hidden behind engine-busy time:
//   W1 (block-1 compute): hit/budget/run checks, whole block-2 fill
//   W2 (block-2 compute): block-3 pad + length into TEXT[8], TEXT[15]
//   W3 (block-3 compute): next block-1's TEXT[8..15] prefill
//
// Two rules, both learnt from wrong hashes on real hardware:
//
// 1. Stores to the SHA registers are at least three cycles apart (a store,
//    then two other instructions). Written closer together, a store is
//    silently lost whenever the other core touches any DPORT register at
//    that moment: 30-60% wrong hashes at two-cycle spacing under core-0
//    DPORT load, none at three.
//
// 2. The hot path never reads BUSY. A poll costs ~10 cycles per wait over
//    issuing the next trigger at the right moment, and it buys no safety: a
//    fully polled variant of this loop still got hashes wrong in most bench
//    passes with core 0 reading APB registers (why was not pinned down), the
//    counted one in none. Counted delays are safe as minimums -- interrupts
//    and bus contention only ever stall the CPU, never the engine, so a
//    delay that is long enough when nothing else runs is always long enough.
//
// The delay constants are the measured thresholds under worst-case core-0
// bus traffic (one over the quiet thresholds), with no cycles to spare: all
// timing here is counted in clock cycles of one synchronous chip, and that
// schedule ran ~23 million bench hashes under every stress mode without a
// wrong one. The schedule with two cycles more on every trigger delay is
// kept as the first fallback. Any change here has to go back through the
// bench: SHA_BENCH=2 runs every variant in sha_bench_variants.inc against
// software, hash by hash, under each stress mode. A threshold missed by one
// cycle shows up there, not in a hashrate counter.
//
// uint32_t fn(volatile uint32_t *sha_text_base, pl_ctx_t *ctx)
//   Returns 1 on a candidate (low 16 bits of the digest's last word zero;
//   the candidate is ctx->nonce - 1), 0 when the budget ran out or ctx->run
//   was cleared. ctx->nonce and ctx->budget are updated on return. The
//   engine must be idle on entry and is idle again on return.
#include "sdkconfig.h"
#if defined(CONFIG_IDF_TARGET_ESP32) && (defined(PIPELINED_ASM_MINING) || defined(SHA_BENCH))

#define CTX_HDR16   64
#define CTX_NONCE   80
#define CTX_BUDGET  84
#define CTX_RUN     88
#define CTX_PAD     92
#define CTX_CHK     96

#define R_START 0x90
#define R_CONT  0x94
#define R_LOAD  0x98

    .macro NOPS n
    .rept \n
    nop.n
    .endr
    .endm

    // At least 84 cycles: longer than any engine operation.
    .macro ENGINE_SETTLE
    movi.n  a7, 28
1:  addi.n  a7, a7, -1
    bnez    a7, 1b
    .endm

    // TEXT[\dst / 4] = ctx word at \src, spaced for the next store
    .macro COPY src, dst, wgap
    l32i    a7, a3, \src
    s32i.n  a7, a2, \dst
    NOPS    \wgap
    .endm

    // TEXT[\dst / 4] = 0, spaced for the next store
    .macro ZERO dst, wgap
    s32i.n  a8, a2, \dst
    nop.n
    NOPS    \wgap
    .endm

    // TEXT[\k] = header word \k; the digest's last word is sampled after
    // \lpos of these (it has to be read before word 7 is overwritten).
    .macro HDRW k, lpos, wgap
    COPY    \k * 4, \k * 4, \wgap
    .if \lpos == \k + 1
    l16ui   a6, a2, 28          // low 16 bits of the last digest word: 0 = candidate
    .endif
    .endm

// Knobs (cycles of nop unless noted):
//   p1   after the block-2 fill, before CONTINUE
//   d2   after CONTINUE, before TEXT[8]/TEXT[15] are rewritten for block 3
//   p2   rest of the block-2 compute, before LOAD
//   pl1  LOAD -> START block 3
//   d3   after START block 3, before TEXT[8..15] are rewritten for block 1
//   p3   rest of the block-3 compute, before LOAD
//   pl2  LOAD -> first header store
//   lpos header stores done before the digest word is sampled (1..7)
//   wgap extra instructions between consecutive TEXT stores (rule 1: >= 1)
//   chk  bench builds: sum every sampled digest word into ctx->chk
// Registers: a2 TEXT base, a3 ctx, a4 nonce, a5 budget, a6 trigger const /
// sampled digest word, a7 scratch, a8 zero.
    .macro PL_MINE name, sect, p1, d2, p2, pl1, d3, p3, pl2, lpos, wgap, chk
    .section \sect, "ax"
    .global \name
    .type   \name, @function
    .balign 4
\name:
    entry   a1, 32
    l32i    a4, a3, CTX_NONCE
    l32i    a5, a3, CTX_BUDGET
    movi.n  a8, 0
    ENGINE_SETTLE
    // cold start: TEXT[0..15] = header words 0..15
    COPY    0, 0, \wgap
    COPY    4, 4, \wgap
    COPY    8, 8, \wgap
    COPY    12, 12, \wgap
    COPY    16, 16, \wgap
    COPY    20, 20, \wgap
    COPY    24, 24, \wgap
    COPY    28, 28, \wgap
    COPY    32, 32, \wgap
    COPY    36, 36, \wgap
    COPY    40, 40, \wgap
    COPY    44, 44, \wgap
    COPY    48, 48, \wgap
    COPY    52, 52, \wgap
    COPY    56, 56, \wgap
    COPY    60, 60, \wgap
    movi.n  a6, 1               // "previous nonce was not a candidate"
    s32i    a6, a2, R_START     // START block 1 of the first nonce
    j       .Lhead_\name

    .balign 4
.Lw2b_\name:
    // ===== W2, second half: wait out block 2 =====
    NOPS    \p2
    s32i    a6, a2, R_LOAD      // ===== LOAD digest1 -> TEXT[0..7] =====
    NOPS    \pl1
    s32i    a6, a2, R_START     // ===== START block 3 =====

    // ===== W3: next block-1's TEXT[8..15], once the engine has read block 3 =====
    NOPS    \d3
    COPY    32, 32, \wgap
    COPY    36, 36, \wgap
    COPY    40, 40, \wgap
    COPY    44, 44, \wgap
    COPY    48, 48, \wgap
    COPY    52, 52, \wgap
    COPY    56, 56, \wgap
    COPY    60, 60, \wgap
    NOPS    \p3
    s32i    a6, a2, R_LOAD      // ===== LOAD digest2 -> TEXT[0..7] =====
    NOPS    \pl2

    // The only engine-idle work: TEXT[0..7] = header words 0..7.
    HDRW    0, \lpos, \wgap
    HDRW    1, \lpos, \wgap
    HDRW    2, \lpos, \wgap
    HDRW    3, \lpos, \wgap
    HDRW    4, \lpos, \wgap
    HDRW    5, \lpos, \wgap
    HDRW    6, \lpos, \wgap
    HDRW    7, \lpos, \wgap
    movi.n  a7, 1
    s32i    a7, a2, R_START     // ===== START block 1 (next nonce) =====

.Lhead_\name:
    // ===== W1: decide about the nonce that just finished... =====
    .if \chk
    l32i    a7, a3, CTX_CHK
    add.n   a7, a7, a6
    s32i    a7, a3, CTX_CHK
    .else
    NOPS    5                   // same timing as the bench build (measured)
    .endif
    beqz    a6, .Lhit_\name
    beqz    a5, .Lstop_\name
    l32i    a7, a3, CTX_RUN
    beqz    a7, .Lstop_\name
    addi.n  a5, a5, -1
    movi.n  a6, 1

    // ===== ...and fill block 2 behind the engine's read pointer =====
    COPY    CTX_HDR16, 0, \wgap
    COPY    CTX_HDR16 + 4, 4, \wgap
    COPY    CTX_HDR16 + 8, 8, \wgap
    l32i    a7, a3, CTX_PAD
    s32i.n  a4, a2, 12          // nonce
    addi.n  a4, a4, 1
    NOPS    \wgap
    s32i.n  a7, a2, 16          // 0x80000000
    nop.n
    NOPS    \wgap
    ZERO    20, \wgap
    ZERO    24, \wgap
    ZERO    28, \wgap
    ZERO    32, \wgap
    ZERO    36, \wgap
    ZERO    40, \wgap
    ZERO    44, \wgap
    ZERO    48, \wgap
    ZERO    52, \wgap
    ZERO    56, \wgap
    movi    a7, 640
    s32i.n  a7, a2, 60          // message length, first SHA
    NOPS    \p1
    s32i    a6, a2, R_CONT      // ===== CONTINUE block 2 =====

    // ===== W2, first half: block-3 pad + length, once the engine has read block 2 =====
    NOPS    \d2
    COPY    CTX_PAD, 32, \wgap  // TEXT[8] = 0x80000000 (TEXT[9..14] stay 0)
    movi    a7, 256
    s32i.n  a7, a2, 60          // TEXT[15] = 256
    j       .Lw2b_\name

.Lhit_\name:
    movi.n  a6, 1
    j       .Lout_\name
.Lstop_\name:
    movi.n  a6, 0
.Lout_\name:
    // The block-1 compute just started is abandoned; wait it out so the
    // engine is idle for whoever uses it next.
    ENGINE_SETTLE
    s32i    a4, a3, CTX_NONCE
    s32i    a5, a3, CTX_BUDGET
    mov.n   a2, a6
    retw.n
    .size   \name, . - \name
    .endm

    // Measured thresholds (quiet / worst-case core-0 bus traffic):
    //   p1 3/4, p2 39/40, pl1 9/10, p3 26/27, pl2 9/10, d2 12, d3 8.
    // 247 cycles per nonce.
    PL_MINE pipelined_hw_mine_classic_v2,        .iram1.plmine2,   4, 16, 40, 10, 14, 27, 10, 2, 1, 0
    // The same with 2 and with 14 more cycles on every trigger delay (257 and
    // 317 cycles per nonce): the fallbacks if the power-on self-test of the
    // fast one fails, or its candidates stop being confirmed by software
    // (see pl_selftest and pl_health in mining.cpp).
    PL_MINE pipelined_hw_mine_classic_v2_margin, .iram1.plmine2m,  6, 16, 42, 12, 14, 29, 12, 2, 1, 0
    PL_MINE pipelined_hw_mine_classic_v2_wide,   .iram1.plmine2w, 18, 16, 54, 24, 14, 41, 24, 2, 1, 0

#if defined(SHA_BENCH) && SHA_BENCH == 2
#include "sha_bench_variants.inc"
#endif

#endif
