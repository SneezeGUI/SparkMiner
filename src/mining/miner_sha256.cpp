/*
 * SparkMiner - BitsyMiner SHA-256 Implementation
 * Ported from BitsyMiner by Justin Williams (GPL v3)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
#include <Arduino.h>
#include <string.h>
#include "miner_sha256.h"

#define BYTESWAP32(z) ((uint32_t)((z&0xFF)<<24|((z>>8)&0xFF)<<16|((z>>16)&0xFF)<<8|((z>>24)&0xFF)))

#define RROT(v, s) ((v)>>(s) | (v)<<(32-(s)))
#define SSIG0(v) (RROT((v),7) ^ RROT((v),18) ^ ((v) >> 3))
#define SSIG1(v) (RROT((v),17) ^ RROT((v),19) ^ ((v) >> 10))
#define R1_a(i) (w[i] = w[i-16] + (RROT(w[i-15],7) ^ (RROT(w[i-15],18) ^ (w[i-15] >> 3))) + ((RROT(w[i-2],17) ^ RROT(w[i-2],19) ^ (w[i-2] >> 10))))

#define R1(i) (w[i] = w[i-16] + (RROT(w[i-15],7) ^ (RROT(w[i-15],18) ^ (w[i-15] >> 3))) + w[i-7] + ((RROT(w[i-2],17) ^ RROT(w[i-2],19) ^ (w[i-2] >> 10))))
#define R1_b(i) (w[i] = w[i-16] + w[i-7] + ((RROT(w[i-2],17) ^ RROT(w[i-2],19) ^ (w[i-2] >> 10))))
#define R1_c(i) (w[i] = w[i-7] + ((RROT(w[i-2],17) ^ RROT(w[i-2],19) ^ (w[i-2] >> 10))))
#define R1_d(i) (w[i] = (RROT(w[i-15],7) ^ (RROT(w[i-15],18) ^ (w[i-15] >> 3))) + w[i-7] + ((RROT(w[i-2],17) ^ RROT(w[i-2],19) ^ (w[i-2] >> 10))))

#define WORD uint32_t

#define S1 (RROT(e, 6) ^ RROT(e,11) ^ RROT(e, 25))
#define CH ((e & f) ^ ((~e) & g))
#define S0 (RROT(a, 2) ^ RROT(a,13) ^ RROT(a, 22))
#define MAJ ((a & b) ^ (a & c) ^ (b & c))
#define C1(i) temp1=h+S1+CH+k[i]+w[i];temp2=S0+MAJ;h=g;g=f;f=e;e=d+temp1;d=c;c=b;b=a;a=temp1+temp2

#define MAJ_1(a,b,c) ((WA[a] & WA[b]) | (WA[c] & (WA[a] | WA[b])))
#define CH_1(e,f,g) (WA[g] ^ (WA[e] & (WA[f] ^ WA[g])))
#define S1_1(e) (RROT(WA[e], 6) ^ RROT(WA[e],11) ^ RROT(WA[e], 25))
#define S0_1(a) (RROT(WA[a], 2) ^ RROT(WA[a],13) ^ RROT(WA[a], 22))
#define CM(a, b, c, d, e, f, g, h, i) \
        temp1=WA[h]+S1_1(e)+CH_1(e,f,g)+k[i]+w[i];temp2=S0_1(a)+MAJ_1(a,b,c);WA[d]=WA[d]+temp1;WA[h]=temp1+temp2

#define GET_DATA(v,i) (((uint32_t)(v[i]) << 24) | ((uint32_t)(v[i + 1]) << 16) | ((uint32_t)(v[i + 2]) << 8) | ((uint32_t)(v[i + 3])))

// SHA-256 initial hash values
static WORD h0 = 0x6a09e667;
static WORD h1 = 0xbb67ae85;
static WORD h2 = 0x3c6ef372;
static WORD h3 = 0xa54ff53a;
static WORD h4 = 0x510e527f;
static WORD h5 = 0x9b05688c;
static WORD h6 = 0x1f83d9ab;
static WORD h7 = 0x5be0cd19;

// SHA-256 round constants
static uint32_t k[] = {
   0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
   0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
   0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
   0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
   0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
   0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
   0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
   0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static const WORD kTailPadWord = 0x80000000u;
static const WORD kTailLenWord = 0x00000280u;
static const WORD kSigma0PadWord = 0x11002000u; // sigma0(0x80000000)
static const WORD kSigma0LenWord = 0x00a00055u; // sigma0(0x00000280)
static const WORD kSigma1LenWord = 0x01100000u; // sigma1(0x00000280)

#ifndef MINER_EXPERIMENTAL_COMPRESSOR
#define MINER_EXPERIMENTAL_COMPRESSOR 0
#endif

#if MINER_EXPERIMENTAL_COMPRESSOR
#define BSIG0(v) (RROT((v), 2) ^ RROT((v), 13) ^ RROT((v), 22))
#define BSIG1(v) (RROT((v), 6) ^ RROT((v), 11) ^ RROT((v), 25))
#define CHX(e,f,g) (((e) & (f)) ^ (~(e) & (g)))
#define MAJX(a,b,c) (((a) & (b)) ^ ((a) & (c)) ^ ((b) & (c)))

static IRAM_ATTR void sha256_compress_64_experimental(
    WORD *a,
    WORD *b,
    WORD *c,
    WORD *d,
    WORD *e,
    WORD *f,
    WORD *g,
    WORD *h,
    const WORD *w
) {
    WORD ra = *a, rb = *b, rc = *c, rd = *d;
    WORD re = *e, rf = *f, rg = *g, rh = *h;

    for (int i = 0; i < 64; i++) {
        const WORD t1 = rh + BSIG1(re) + CHX(re, rf, rg) + k[i] + w[i];
        const WORD t2 = BSIG0(ra) + MAJX(ra, rb, rc);
        rh = rg;
        rg = rf;
        rf = re;
        re = rd + t1;
        rd = rc;
        rc = rb;
        rb = ra;
        ra = t1 + t2;
    }

    *a = ra; *b = rb; *c = rc; *d = rd;
    *e = re; *f = rf; *g = rg; *h = rh;
}
#endif


static void IRAM_ATTR sha256_transform(sha256_hash_t *ctx, uint8_t *msg) {
    WORD w[64];
    WORD temp1, temp2;
    WORD i, j;

    WORD a = ctx->hash[0], b = ctx->hash[1], c = ctx->hash[2], d = ctx->hash[3];
    WORD e = ctx->hash[4], f = ctx->hash[5], g = ctx->hash[6], h = ctx->hash[7];

    // Copy chunk into first 16 words w[0..15] of the message schedule array
    for (i = 0, j = 0; i < 16; ++i, j += 4) {
        w[i] = (msg[j] << 24) | (msg[j+1] << 16) | (msg[j+2] << 8) | (msg[j+3]);
    }

    R1(16); R1(17); R1(18); R1(19); R1(20); R1(21); R1(22); R1(23); R1(24); R1(25);
    R1(26); R1(27); R1(28); R1(29); R1(30); R1(31); R1(32); R1(33); R1(34); R1(35);
    R1(36); R1(37); R1(38); R1(39); R1(40); R1(41); R1(42); R1(43); R1(44); R1(45);
    R1(46); R1(47); R1(48); R1(49); R1(50); R1(51); R1(52); R1(53); R1(54); R1(55);
    R1(56); R1(57); R1(58); R1(59); R1(60); R1(61); R1(62); R1(63);

    C1(0);C1(1);C1(2);C1(3);C1(4);C1(5);C1(6);C1(7);C1(8);C1(9);
    C1(10);C1(11);C1(12);C1(13);C1(14);C1(15);C1(16);C1(17);C1(18);C1(19);
    C1(20);C1(21);C1(22);C1(23);C1(24);C1(25);C1(26);C1(27);C1(28);C1(29);
    C1(30);C1(31);C1(32);C1(33);C1(34);C1(35);C1(36);C1(37);C1(38);C1(39);
    C1(40);C1(41);C1(42);C1(43);C1(44);C1(45);C1(46);C1(47);C1(48);C1(49);
    C1(50);C1(51);C1(52);C1(53);C1(54);C1(55);C1(56);C1(57);C1(58);C1(59);
    C1(60);C1(61);C1(62);C1(63);

    ctx->hash[0] += a;
    ctx->hash[1] += b;
    ctx->hash[2] += c;
    ctx->hash[3] += d;
    ctx->hash[4] += e;
    ctx->hash[5] += f;
    ctx->hash[6] += g;
    ctx->hash[7] += h;
}


void IRAM_ATTR miner_sha256(sha256_hash_t *ctx, uint8_t *msg, size_t len) {
    ctx->hash[0] = h0;
    ctx->hash[1] = h1;
    ctx->hash[2] = h2;
    ctx->hash[3] = h3;
    ctx->hash[4] = h4;
    ctx->hash[5] = h5;
    ctx->hash[6] = h6;
    ctx->hash[7] = h7;

    WORD i, j;
    size_t remain = len % 64;
    size_t total_len = len - remain;

    for (i = 0; i < total_len; i += 64) {
        sha256_transform(ctx, &msg[i]);
    }

    uint8_t m[64] = {};
    for (i = total_len, j = 0; i < len; ++i, ++j) {
        m[j] = msg[i];
    }
    m[j++] = 0x80;

    if (j > 56) {
        sha256_transform(ctx, m);
        memset(m, 0, sizeof(m));
    }

    unsigned long long L = len * 8;
    m[63] = L;
    m[62] = L >> 8;
    m[61] = L >> 16;
    m[60] = L >> 24;
    m[59] = L >> 32;
    m[58] = L >> 40;
    m[57] = L >> 48;
    m[56] = L >> 56;

    sha256_transform(ctx, m);

    ctx->hash[0] = BYTESWAP32(ctx->hash[0]);
    ctx->hash[1] = BYTESWAP32(ctx->hash[1]);
    ctx->hash[2] = BYTESWAP32(ctx->hash[2]);
    ctx->hash[3] = BYTESWAP32(ctx->hash[3]);
    ctx->hash[4] = BYTESWAP32(ctx->hash[4]);
    ctx->hash[5] = BYTESWAP32(ctx->hash[5]);
    ctx->hash[6] = BYTESWAP32(ctx->hash[6]);
    ctx->hash[7] = BYTESWAP32(ctx->hash[7]);
}


void IRAM_ATTR miner_sha256_midstate(sha256_hash_t *ctx, block_header_t *hb) {
    WORD w[64];
    WORD temp1, temp2;
    WORD a, b, c, d, e, f, g, h;
    int i, j;

    uint8_t *data = (uint8_t *)hb;

    ctx->hash[0] = a = h0;
    ctx->hash[1] = b = h1;
    ctx->hash[2] = c = h2;
    ctx->hash[3] = d = h3;
    ctx->hash[4] = e = h4;
    ctx->hash[5] = f = h5;
    ctx->hash[6] = g = h6;
    ctx->hash[7] = h = h7;

    // Copy the first 64 bytes of the header into the working area
    for (i = 0, j = 0; i < 16; ++i, j += 4)
        w[i] = (data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) | (data[j + 3]);

    R1(16); R1(17); R1(18); R1(19); R1(20); R1(21); R1(22); R1(23); R1(24); R1(25);
    R1(26); R1(27); R1(28); R1(29); R1(30); R1(31); R1(32); R1(33); R1(34); R1(35);
    R1(36); R1(37); R1(38); R1(39); R1(40); R1(41); R1(42); R1(43); R1(44); R1(45);
    R1(46); R1(47); R1(48); R1(49); R1(50); R1(51); R1(52); R1(53); R1(54); R1(55);
    R1(56); R1(57); R1(58); R1(59); R1(60); R1(61); R1(62); R1(63);

    C1(0);C1(1);C1(2);C1(3);C1(4);C1(5);C1(6);C1(7);C1(8);C1(9);
    C1(10);C1(11);C1(12);C1(13);C1(14);C1(15);C1(16);C1(17);C1(18);C1(19);
    C1(20);C1(21);C1(22);C1(23);C1(24);C1(25);C1(26);C1(27);C1(28);C1(29);
    C1(30);C1(31);C1(32);C1(33);C1(34);C1(35);C1(36);C1(37);C1(38);C1(39);
    C1(40);C1(41);C1(42);C1(43);C1(44);C1(45);C1(46);C1(47);C1(48);C1(49);
    C1(50);C1(51);C1(52);C1(53);C1(54);C1(55);C1(56);C1(57);C1(58);C1(59);
    C1(60);C1(61);C1(62);C1(63);

    ctx->hash[0] += a;
    ctx->hash[1] += b;
    ctx->hash[2] += c;
    ctx->hash[3] += d;
    ctx->hash[4] += e;
    ctx->hash[5] += f;
    ctx->hash[6] += g;
    ctx->hash[7] += h;
}

bool IRAM_ATTR miner_sha256_header(sha256_hash_t *midpoint, sha256_hash_t *ctx, block_header_t *hb) {
    miner_sha256_complete_from_midstate(midpoint, hb, NULL, ctx);

    // Keep existing fast prefilter semantics for callers that use the bool return.
    // Early reject optimization: Check if upper 16 bits of H0 are zero.
    // This filters out ~65,536× more hashes before full target comparison.
    // Since pool difficulty is typically around 4 billion, this rejects ~99.99% of hashes
    // with just a 2-byte comparison instead of a full 32-byte target check.
    return (ctx->bytes[31] == 0 && ctx->bytes[30] == 0);
}

void IRAM_ATTR miner_sha256_prepare_tail_schedule(
    sha256_tail_schedule_cache_t *cache,
    const block_header_t *hb
) {
    const uint8_t *data = (const uint8_t *)hb;

    cache->w0 = GET_DATA(data, 64);
    cache->w1 = GET_DATA(data, 68);
    cache->w2 = GET_DATA(data, 72);

    cache->w16 = cache->w0 + SSIG0(cache->w1);
    cache->w17 = cache->w1 + SSIG0(cache->w2) + kSigma1LenWord;
    cache->s1_w16 = SSIG1(cache->w16);
    cache->s1_w17 = SSIG1(cache->w17);
}

/**
 * Complete Bitcoin double SHA-256 from pre-computed first-64-byte midstate.
 *
 * This is the core mining hash function called once per nonce.
 * Time: ~4-5 microseconds
 * Work: Two full SHA-256 compression blocks (one for tail, one for doubling)
 *
 * Algorithm:
 * 1. Load tail block (last 16 bytes of header + nonce + padding)
 * 2. Expand message schedule (w[16..63] from w[0..15])
 * 3. 64 macro-unrolled compression rounds using midstate as initial state
 * 4. Finalize first hash (H0-H7 + midstate)
 * 5. Byte-swap result to big-endian for second hash input
 * 6. Feed through miner_sha256() for second compression
 * 7. Return final 32-byte hash in little-endian (for target comparison)
 *
 * Why software (not hardware):
 * - Hardware SHA requires mutex (dual-core contention)
 * - Software is single-core per nonce (no synchronization)
 * - Macro-unrolled rounds avoid loop overhead
 * - No register I/O latency
 * - Proven stable and fast in production (~43-45 kH/s)
 *
 * Alternative hardware path exists but was disabled because:
 * - Lock overhead > hardware acceleration benefit
 * - Full 80-byte hardware SHA recomputes cached midstate
 * - Midstate restore/continue adds register write overhead
 * - Detailed analysis: See SHA256_BACKEND_INVESTIGATION.md
 *
 * @param midpoint Pre-computed midstate from miner_sha256_midstate()
 * @param hb Block header with current nonce
 * @param firstOut Optional: First SHA-256 result (only used in testing)
 * @param secondOut Final double-SHA-256 result
 */
/*
 * Inline-only helper: second SHA-256 of a 32-byte first-hash result.
 *
 * Existing second-SHA optimization (landed before first-tail schedule cache work).
 * Replaces miner_sha256(secondOut, tmp.bytes, 32) in the hot path.
 *
 * Optimizations vs the generic call:
 *  1. No byte-array roundtrip – takes raw first-hash words directly as w[0..7]
 *  2. Hardcoded padding: w[8..15] = {0x80000000,0,0,0,0,0,0,0x00000100}
 *  3. Schedule simplified where padding words are 0:
 *       sigma0(0)=0, sigma1(0)=0 eliminate additions for w[16..29]
 *       sigma1(0x100), sigma0(0x80000000), sigma0(0x100) folded to constants
 *  4. IV baked in as register initializers (no ctx struct load/store)
 *  5. always_inline lets the compiler optimize registers across both SHAs
 *
 * Input invariant:  fh[i] = a_i + midpoint->hash[i]  (pre-BYTESWAP32 value)
 * This equals GET_DATA(byteswapped_bytes, i*4), so it is the correct w[i]
 * input for standard big-endian SHA-256 message loading.
 *
 * Pre-computed padding constants (verified):
 *   sigma1(0)          = 0
 *   sigma0(0)          = 0
 *   sigma1(0x00000100) = RROT(0x100,17)^RROT(0x100,19)^(0x100>>10) = 0x00A00000
 *   sigma0(0x80000000) = RROT(0x80000000,7)^RROT(0x80000000,18)^(0x80000000>>3) = 0x11002000
 *   sigma0(0x00000100) = RROT(0x100,7)^RROT(0x100,18)^(0x100>>3) = 0x00400022
 */
static IRAM_ATTR void sha256_second_from_first_hash(
    const WORD *fh,       // fh[0..7] = first hash words (a+mid, not byteswapped)
    sha256_hash_t *secondOut
) {
    WORD w[64];
    WORD temp1, temp2;

    // Load first hash as w[0..7] (no byte parsing needed)
    w[0]=fh[0]; w[1]=fh[1]; w[2]=fh[2]; w[3]=fh[3];
    w[4]=fh[4]; w[5]=fh[5]; w[6]=fh[6]; w[7]=fh[7];
    // Hardcoded 32-byte padding: w[8]=0x80000000, w[9..14]=0, w[15]=0x100
    w[8]=0x80000000u;
    w[9]=0; w[10]=0; w[11]=0; w[12]=0; w[13]=0; w[14]=0;
    w[15]=0x00000100u;

    // w[16] = w[0] + sigma0(w[1]) + w[9] + sigma1(w[14])
    //       = w[0] + sigma0(w[1])          [w[9]=0, sigma1(0)=0, w[14]=0]
    w[16] = w[0] + (RROT(w[1],7)^RROT(w[1],18)^(w[1]>>3));

    // w[17] = w[1] + sigma0(w[2]) + w[10] + sigma1(w[15])
    //       = w[1] + sigma0(w[2]) + 0xA00000  [w[10]=0, sigma1(0x100)=0xA00000]
    w[17] = w[1] + (RROT(w[2],7)^RROT(w[2],18)^(w[2]>>3)) + 0x00A00000u;

    // w[18] = w[2] + sigma0(w[3]) + w[11] + sigma1(w[16])  [w[11]=0]
    w[18] = w[2] + (RROT(w[3],7)^RROT(w[3],18)^(w[3]>>3)) + (RROT(w[16],17)^RROT(w[16],19)^(w[16]>>10));

    // w[19] = w[3] + sigma0(w[4]) + w[12] + sigma1(w[17])  [w[12]=0]
    w[19] = w[3] + (RROT(w[4],7)^RROT(w[4],18)^(w[4]>>3)) + (RROT(w[17],17)^RROT(w[17],19)^(w[17]>>10));

    // w[20] = w[4] + sigma0(w[5]) + w[13] + sigma1(w[18])
    //       = w[4] + sigma0(w[5]) + sigma1(w[18])          [w[13]=0]
    w[20] = w[4] + (RROT(w[5],7)^RROT(w[5],18)^(w[5]>>3)) + (RROT(w[18],17)^RROT(w[18],19)^(w[18]>>10));

    // w[21] = w[5] + sigma0(w[6]) + w[14] + sigma1(w[19])
    //       = w[5] + sigma0(w[6]) + sigma1(w[19])          [w[14]=0]
    w[21] = w[5] + (RROT(w[6],7)^RROT(w[6],18)^(w[6]>>3)) + (RROT(w[19],17)^RROT(w[19],19)^(w[19]>>10));

    // w[22] = w[6] + sigma0(w[7]) + w[15] + sigma1(w[20])
    //       = w[6] + sigma0(w[7]) + 0x100 + sigma1(w[20])  [w[15]=0x100]
    w[22] = w[6] + (RROT(w[7],7)^RROT(w[7],18)^(w[7]>>3)) + 0x00000100u + (RROT(w[20],17)^RROT(w[20],19)^(w[20]>>10));

    // w[23] = w[7] + sigma0(w[8]) + w[16] + sigma1(w[21])
    //       = w[7] + 0x11002000 + w[16] + sigma1(w[21])  [sigma0(0x80000000)=0x11002000]
    w[23] = w[7] + 0x11002000u + w[16] + (RROT(w[21],17)^RROT(w[21],19)^(w[21]>>10));

    // w[24] = w[8] + sigma0(w[9]) + w[17] + sigma1(w[22])
    //       = 0x80000000 + w[17] + sigma1(w[22])  [sigma0(0)=0, w[9]=0]
    w[24] = 0x80000000u + w[17] + (RROT(w[22],17)^RROT(w[22],19)^(w[22]>>10));

    // w[25] = w[9] + sigma0(w[10]) + w[18] + sigma1(w[23])
    //       = w[18] + sigma1(w[23])         [w[9]=0, sigma0(0)=0, w[10]=0]
    w[25] = w[18] + (RROT(w[23],17)^RROT(w[23],19)^(w[23]>>10));

    // w[26] = w[10] + sigma0(w[11]) + w[19] + sigma1(w[24])
    //       = w[19] + sigma1(w[24])         [w[10]=0, sigma0(0)=0, w[11]=0]
    w[26] = w[19] + (RROT(w[24],17)^RROT(w[24],19)^(w[24]>>10));

    // w[27] = w[11] + sigma0(w[12]) + w[20] + sigma1(w[25])
    //       = w[20] + sigma1(w[25])         [w[11]=0, sigma0(0)=0, w[12]=0]
    w[27] = w[20] + (RROT(w[25],17)^RROT(w[25],19)^(w[25]>>10));

    // w[28] = w[12] + sigma0(w[13]) + w[21] + sigma1(w[26])
    //       = w[21] + sigma1(w[26])         [w[12]=0, sigma0(0)=0, w[13]=0]
    w[28] = w[21] + (RROT(w[26],17)^RROT(w[26],19)^(w[26]>>10));

    // w[29] = w[13] + sigma0(w[14]) + w[22] + sigma1(w[27])
    //       = w[22] + sigma1(w[27])         [w[13]=0, sigma0(0)=0, w[14]=0]
    w[29] = w[22] + (RROT(w[27],17)^RROT(w[27],19)^(w[27]>>10));

    // w[30] = w[14] + sigma0(w[15]) + w[23] + sigma1(w[28])
    //       = 0x400022 + w[23] + sigma1(w[28])  [w[14]=0, sigma0(0x100)=0x400022]
    w[30] = 0x00400022u + w[23] + (RROT(w[28],17)^RROT(w[28],19)^(w[28]>>10));

    // w[31] = w[15] + sigma0(w[16]) + w[24] + sigma1(w[29])  [w[15]=0x100 constant]
    w[31] = 0x00000100u + (RROT(w[16],7)^RROT(w[16],18)^(w[16]>>3)) + w[24] + (RROT(w[29],17)^RROT(w[29],19)^(w[29]>>10));

    // w[32..63]: regular expansion, all inputs data-dependent from here
    R1(32); R1(33); R1(34); R1(35); R1(36); R1(37); R1(38); R1(39); R1(40); R1(41);
    R1(42); R1(43); R1(44); R1(45); R1(46); R1(47); R1(48); R1(49); R1(50); R1(51);
    R1(52); R1(53); R1(54); R1(55); R1(56); R1(57); R1(58); R1(59); R1(60); R1(61);
    R1(62); R1(63);

    // Compression: start from SHA-256 IV (baked-in constants, no struct load)
    WORD a=h0, b=h1, c=h2, d=h3, e=h4, f=h5, g=h6, h=h7;
    C1(0);C1(1);C1(2);C1(3);C1(4);C1(5);C1(6);C1(7);C1(8);C1(9);
    C1(10);C1(11);C1(12);C1(13);C1(14);C1(15);C1(16);C1(17);C1(18);C1(19);
    C1(20);C1(21);C1(22);C1(23);C1(24);C1(25);C1(26);C1(27);C1(28);C1(29);
    C1(30);C1(31);C1(32);C1(33);C1(34);C1(35);C1(36);C1(37);C1(38);C1(39);
    C1(40);C1(41);C1(42);C1(43);C1(44);C1(45);C1(46);C1(47);C1(48);C1(49);
    C1(50);C1(51);C1(52);C1(53);C1(54);C1(55);C1(56);C1(57);C1(58);C1(59);
    C1(60);C1(61);C1(62);C1(63);

    // Davies-Meyer addback with byteswap (same as miner_sha256 output format)
    secondOut->hash[0] = BYTESWAP32(a + h0);
    secondOut->hash[1] = BYTESWAP32(b + h1);
    secondOut->hash[2] = BYTESWAP32(c + h2);
    secondOut->hash[3] = BYTESWAP32(d + h3);
    secondOut->hash[4] = BYTESWAP32(e + h4);
    secondOut->hash[5] = BYTESWAP32(f + h5);
    secondOut->hash[6] = BYTESWAP32(g + h6);
    secondOut->hash[7] = BYTESWAP32(h + h7);
}

void IRAM_ATTR miner_sha256_complete_from_midstate(
    const sha256_hash_t *midpoint,
    const block_header_t *hb,
    sha256_hash_t *firstOut,
    sha256_hash_t *secondOut
) {
#if !MINER_EXPERIMENTAL_COMPRESSOR
    WORD temp1, temp2;
#endif
    const uint8_t *data = (const uint8_t *)hb;

    WORD a = midpoint->hash[0], b = midpoint->hash[1], c = midpoint->hash[2], d = midpoint->hash[3];
    WORD e = midpoint->hash[4], f = midpoint->hash[5], g = midpoint->hash[6], h = midpoint->hash[7];

    // Second half of block (last 16 bytes of 80-byte header + padding)
    // w[0..3] = bytes 64-79 (merkle tail, timestamp, nbits, nonce)
    // w[4] = 0x80000000 (padding bit)
    // w[5..14] = 0
    // w[15] = 0x00000280 (640 bits = 80 bytes in big-endian)
    WORD w[64] = {
        GET_DATA(data, 64), GET_DATA(data, 68), GET_DATA(data, 72), GET_DATA(data, 76),
        0x80000000,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0x00000280
    };

    R1(16); R1(17); R1(18); R1(19); R1(20); R1(21); R1(22); R1(23); R1(24); R1(25);
    R1(26); R1(27); R1(28); R1(29); R1(30); R1(31); R1(32); R1(33); R1(34); R1(35);
    R1(36); R1(37); R1(38); R1(39); R1(40); R1(41); R1(42); R1(43); R1(44); R1(45);
    R1(46); R1(47); R1(48); R1(49); R1(50); R1(51); R1(52); R1(53); R1(54); R1(55);
    R1(56); R1(57); R1(58); R1(59); R1(60); R1(61); R1(62); R1(63);

#if MINER_EXPERIMENTAL_COMPRESSOR
    sha256_compress_64_experimental(&a, &b, &c, &d, &e, &f, &g, &h, w);
#else
    C1(0);C1(1);C1(2);C1(3);C1(4);C1(5);C1(6);C1(7);C1(8);C1(9);
    C1(10);C1(11);C1(12);C1(13);C1(14);C1(15);C1(16);C1(17);C1(18);C1(19);
    C1(20);C1(21);C1(22);C1(23);C1(24);C1(25);C1(26);C1(27);C1(28);C1(29);
    C1(30);C1(31);C1(32);C1(33);C1(34);C1(35);C1(36);C1(37);C1(38);C1(39);
    C1(40);C1(41);C1(42);C1(43);C1(44);C1(45);C1(46);C1(47);C1(48);C1(49);
    C1(50);C1(51);C1(52);C1(53);C1(54);C1(55);C1(56);C1(57);C1(58);C1(59);
    C1(60);C1(61);C1(62);C1(63);
#endif

    // First hash complete - byte-swap for second hash input
    // Store as raw (pre-BYTESWAP) words — sha256_second_from_first_hash uses them
    // as w[0..7] directly (equivalent to what GET_DATA reads from byteswapped bytes).
    WORD fh[8] = {
        a + midpoint->hash[0], b + midpoint->hash[1],
        c + midpoint->hash[2], d + midpoint->hash[3],
        e + midpoint->hash[4], f + midpoint->hash[5],
        g + midpoint->hash[6], h + midpoint->hash[7]
    };

    if (firstOut) {
        firstOut->hash[0] = BYTESWAP32(fh[0]);
        firstOut->hash[1] = BYTESWAP32(fh[1]);
        firstOut->hash[2] = BYTESWAP32(fh[2]);
        firstOut->hash[3] = BYTESWAP32(fh[3]);
        firstOut->hash[4] = BYTESWAP32(fh[4]);
        firstOut->hash[5] = BYTESWAP32(fh[5]);
        firstOut->hash[6] = BYTESWAP32(fh[6]);
        firstOut->hash[7] = BYTESWAP32(fh[7]);
    }

    if (secondOut) {
        sha256_second_from_first_hash(fh, secondOut);
    }
}

void IRAM_ATTR miner_sha256_complete_from_midstate_prepared(
    const sha256_hash_t *midpoint,
    const sha256_tail_schedule_cache_t *cache,
    uint32_t nonce,
    sha256_hash_t *firstOut,
    sha256_hash_t *secondOut
) {
    // Correctness-first implementation: reconstruct the exact tail bytes from cache+nonce
    // and route through the validated legacy completion path.
    block_header_t hb = {0};
    uint8_t *data = (uint8_t *)&hb;

    data[64] = (uint8_t)(cache->w0 >> 24);
    data[65] = (uint8_t)(cache->w0 >> 16);
    data[66] = (uint8_t)(cache->w0 >> 8);
    data[67] = (uint8_t)(cache->w0);

    data[68] = (uint8_t)(cache->w1 >> 24);
    data[69] = (uint8_t)(cache->w1 >> 16);
    data[70] = (uint8_t)(cache->w1 >> 8);
    data[71] = (uint8_t)(cache->w1);

    data[72] = (uint8_t)(cache->w2 >> 24);
    data[73] = (uint8_t)(cache->w2 >> 16);
    data[74] = (uint8_t)(cache->w2 >> 8);
    data[75] = (uint8_t)(cache->w2);

    data[76] = (uint8_t)(nonce);
    data[77] = (uint8_t)(nonce >> 8);
    data[78] = (uint8_t)(nonce >> 16);
    data[79] = (uint8_t)(nonce >> 24);

    miner_sha256_complete_from_midstate(midpoint, &hb, firstOut, secondOut);
}

bool IRAM_ATTR miner_sha256_header_prepared(
    const sha256_hash_t *midpoint,
    const sha256_tail_schedule_cache_t *cache,
    uint32_t nonce,
    sha256_hash_t *ctx
) {
    miner_sha256_complete_from_midstate_prepared(midpoint, cache, nonce, NULL, ctx);
    return (ctx->bytes[31] == 0 && ctx->bytes[30] == 0);
}
