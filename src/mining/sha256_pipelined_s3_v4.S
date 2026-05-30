/*
 * SparkMiner - ESP32-S3 SHA mining loop v4
 *
 * Xtensa windowed ABI:
 *   input after entry:
 *     a2 = midstate
 *     a3 = block2_words
 *     a4 = nonce_ptr
 *     a5 = hash_count_ptr
 *     a6 = mining_flag
 *     a7 = early_h7_limit, full target word for SHA_H[7] bytes[28..31]
 *
 * Return:
 *   a2 = 1 on candidate, 0 when mining_flag was cleared.
 */

    .section .iram1,"ax"
    .align 4
    .global sha256_pipelined_mine_s3_v4
    .type sha256_pipelined_mine_s3_v4, @function

sha256_pipelined_mine_s3_v4:
    entry   sp, 32

    /*
     * Hot register map:
     *   sp+0  midstate pointer
     *   sp+4  nonce_ptr
     *   sp+8  hash_count_ptr
     *   sp+12 mining_flag
     *   sp+16 early_h7_limit
     *   a2-a9 scratch / burst-copy registers
     *   a10 block2 word 0, then also return register in caller window
     *   a11 block2 word 1
     *   a12 block2 word 2
     *   a13 SHA base 0x6003B000
     *   a14 nonce
     *   a15 local hash counter
     */

    s32i.n  a2, sp, 0
    s32i.n  a4, sp, 4
    s32i.n  a5, sp, 8
    s32i.n  a6, sp, 12
    s32i.n  a7, sp, 16

    l32i.n  a14, a4, 0
    l32i.n  a10, a3, 0
    l32i.n  a11, a3, 4
    l32i.n  a12, a3, 8

    movi    a13, 0x6003
    slli    a13, a13, 16
    movi    a7, 0xB
    slli    a7, a7, 12
    add     a13, a13, a7
    movi.n  a15, 0

    movi.n  a7, 2
    s32i.n  a7, a13, 0          /* SHA_MODE = SHA2_256 */

.Lloop_v4:
    /* Restore midstate to SHA_H[0..7]. */
    l32i.n  a9, sp, 0
    l32i.n  a2, a9, 0
    l32i.n  a3, a9, 4
    l32i.n  a4, a9, 8
    l32i.n  a5, a9, 12
    l32i.n  a6, a9, 16
    l32i.n  a7, a9, 20
    l32i.n  a8, a9, 24
    s32i    a2, a13, 0x40
    s32i    a3, a13, 0x44
    s32i    a4, a13, 0x48
    s32i    a5, a13, 0x4C
    s32i    a6, a13, 0x50
    s32i    a7, a13, 0x54
    s32i    a8, a13, 0x58
    l32i.n  a2, a9, 28
    s32i    a2, a13, 0x5C

    /* Block 2: merkle tail, ntime, nbits, nonce, padding. */
    s32i    a10, a13, 0x80
    s32i    a11, a13, 0x84
    s32i    a12, a13, 0x88
    s32i    a14, a13, 0x8C

    movi    a7, 0x80
    s32i    a7, a13, 0x90

    movi.n  a7, 0
    s32i    a7, a13, 0x94
    s32i    a7, a13, 0x98
    s32i    a7, a13, 0x9C
    s32i    a7, a13, 0xA0

    movi    a7, 0x8002
    slli    a7, a7, 16
    s32i    a7, a13, 0xBC

.Lstart_first_v4:
    movi.n  a7, 1
    s32i    a7, a13, 0x14       /* SHA_CONTINUE */

.Lwait_blk2_v4:
    l32i    a7, a13, 0x18       /* SHA_BUSY */
    bnez.n  a7, .Lwait_blk2_v4

    /* Copy first digest SHA_H[0..7] to SHA_TEXT[0..7]. */
    l32i    a2, a13, 0x40
    l32i    a3, a13, 0x44
    l32i    a4, a13, 0x48
    l32i    a5, a13, 0x4C
    l32i    a6, a13, 0x50
    l32i    a7, a13, 0x54
    l32i    a8, a13, 0x58
    l32i    a9, a13, 0x5C
    s32i    a2, a13, 0x80
    s32i    a3, a13, 0x84
    s32i    a4, a13, 0x88
    s32i    a5, a13, 0x8C
    s32i    a6, a13, 0x90
    s32i    a7, a13, 0x94
    s32i    a8, a13, 0x98
    s32i    a9, a13, 0x9C

    movi    a7, 0x80
    s32i    a7, a13, 0xA0

    movi    a7, 0x0001
    slli    a7, a7, 16
    s32i    a7, a13, 0xBC

    addi.n  a14, a14, 1

    movi.n  a7, 1
    s32i    a7, a13, 0x10       /* SHA_START */

#if defined(S3_SPEC_PREPARE_TEXT_BUSY)
    /*
     * Experimental but safer than preparing SHA_H: while the second SHA is
     * busy, prepare only the next SHA_TEXT block. SHA_H must remain untouched
     * until after SHA_H[7] has been read for the early target check.
     */
    s32i    a10, a13, 0x80
    s32i    a11, a13, 0x84
    s32i    a12, a13, 0x88
    s32i    a14, a13, 0x8C

    movi    a7, 0x80
    s32i    a7, a13, 0x90

    movi.n  a7, 0
    s32i    a7, a13, 0x94
    s32i    a7, a13, 0x98
    s32i    a7, a13, 0x9C
    s32i    a7, a13, 0xA0

    movi    a7, 0x8002
    slli    a7, a7, 16
    s32i    a7, a13, 0xBC
#endif

.Lwait_dbl_v4:
    l32i    a7, a13, 0x18       /* SHA_BUSY */
    bnez.n  a7, .Lwait_dbl_v4

    addi.n  a15, a15, 1

    extui   a8, a15, 0, 16
    beqz.n  a8, .Lbatch_v4

    l32i    a7, a13, 0x5C       /* SHA_H[7] = canonical digest bytes[28..31] */
    l32i.n  a8, sp, 16
#if defined(S3_SPEC_PREPARE_TEXT_BUSY)
    bltu    a8, a7, .Lrestore_prepped_text_v4
#else
    bltu    a8, a7, .Lloop_v4
#endif

    /*
     * Avoid reading mining_flag on the common rejected-hash path. Job stop
     * latency becomes candidate-bound, typically well below one second.
     */
    l32i.n  a7, sp, 12
    l8ui    a7, a7, 0
    beqz.n  a7, .Lstopped_v4

    j       .Lcandidate_v4

.Lstopped_v4:
    movi.n  a2, 0
    j       .Lexit_v4

.Lbatch_v4:
    movi.n  a2, 0
    j       .Lexit_v4

.Lcandidate_v4:
    movi.n  a2, 1
    j       .Lexit_v4

#if defined(S3_SPEC_PREPARE_TEXT_BUSY)
.Lrestore_prepped_text_v4:
    /*
     * SHA_TEXT already contains the next block. Restore only SHA_H and start
     * the first compression for the next nonce.
     */
    l32i.n  a9, sp, 0
    l32i.n  a2, a9, 0
    l32i.n  a3, a9, 4
    l32i.n  a4, a9, 8
    l32i.n  a5, a9, 12
    l32i.n  a6, a9, 16
    l32i.n  a7, a9, 20
    l32i.n  a8, a9, 24
    s32i    a2, a13, 0x40
    s32i    a3, a13, 0x44
    s32i    a4, a13, 0x48
    s32i    a5, a13, 0x4C
    s32i    a6, a13, 0x50
    s32i    a7, a13, 0x54
    s32i    a8, a13, 0x58
    l32i.n  a2, a9, 28
    s32i    a2, a13, 0x5C
    j       .Lstart_first_v4
#endif

.Lexit_v4:
    l32i.n  a4, sp, 4
    s32i.n  a14, a4, 0

    /* Add local 32-bit batch count into volatile uint64_t hash_count_ptr. */
    l32i.n  a5, sp, 8
    l32i.n  a7, a5, 0
    l32i.n  a8, a5, 4
    add     a7, a7, a15
    bltu    a7, a15, .Lcarry_v4
    j       .Lstore_count_v4

.Lcarry_v4:
    addi.n  a8, a8, 1

.Lstore_count_v4:
    s32i.n  a7, a5, 0
    s32i.n  a8, a5, 4
    retw.n

    .size sha256_pipelined_mine_s3_v4, .-sha256_pipelined_mine_s3_v4
