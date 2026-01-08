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


static void sha256_transform(sha256_hash_t *ctx, uint8_t *msg) {
    WORD w[64];
    WORD temp1, temp2;
    WORD i, j;

    WORD WA[8] = {
        ctx->hash[0],
        ctx->hash[1],
        ctx->hash[2],
        ctx->hash[3],
        ctx->hash[4],
        ctx->hash[5],
        ctx->hash[6],
        ctx->hash[7]
    };

    // Copy chunk into first 16 words w[0..15] of the message schedule array
    for (i = 0, j = 0; i < 16; ++i, j += 4) {
        w[i] = (msg[j] << 24) | (msg[j+1] << 16) | (msg[j+2] << 8) | (msg[j+3]);
    }

    R1(16); R1(17); R1(18); R1(19); R1(20); R1(21); R1(22); R1(23); R1(24); R1(25);
    R1(26); R1(27); R1(28); R1(29); R1(30); R1(31); R1(32); R1(33); R1(34); R1(35);
    R1(36); R1(37); R1(38); R1(39); R1(40); R1(41); R1(42); R1(43); R1(44); R1(45);
    R1(46); R1(47); R1(48); R1(49); R1(50); R1(51); R1(52); R1(53); R1(54); R1(55);
    R1(56); R1(57); R1(58); R1(59); R1(60); R1(61); R1(62); R1(63);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 0);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 1);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 2);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 3);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 4);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 5);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 6);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 7);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 8);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 9);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 10);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 11);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 12);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 13);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 14);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 15);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 16);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 17);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 18);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 19);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 20);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 21);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 22);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 23);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 24);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 25);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 26);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 27);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 28);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 29);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 30);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 31);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 32);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 33);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 34);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 35);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 36);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 37);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 38);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 39);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 40);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 41);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 42);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 43);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 44);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 45);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 46);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 47);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 48);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 49);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 50);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 51);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 52);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 53);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 54);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 55);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 56);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 57);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 58);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 59);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 60);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 61);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 62);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 63);

    ctx->hash[0] += WA[0];
    ctx->hash[1] += WA[1];
    ctx->hash[2] += WA[2];
    ctx->hash[3] += WA[3];
    ctx->hash[4] += WA[4];
    ctx->hash[5] += WA[5];
    ctx->hash[6] += WA[6];
    ctx->hash[7] += WA[7];
}


void miner_sha256(sha256_hash_t *ctx, uint8_t *msg, size_t len) {
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


void miner_sha256_midstate(sha256_hash_t *ctx, block_header_t *hb) {
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

bool miner_sha256_header(sha256_hash_t *midpoint, sha256_hash_t *ctx, block_header_t *hb) {
    sha256_hash_t tmp;
    WORD temp1, temp2;
    uint8_t *data = (uint8_t *)hb;
    int i, j;

    WORD WA[8] = {
        midpoint->hash[0],
        midpoint->hash[1],
        midpoint->hash[2],
        midpoint->hash[3],
        midpoint->hash[4],
        midpoint->hash[5],
        midpoint->hash[6],
        midpoint->hash[7]
    };

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

    CM(0, 1, 2, 3, 4, 5, 6, 7, 0);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 1);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 2);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 3);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 4);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 5);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 6);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 7);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 8);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 9);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 10);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 11);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 12);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 13);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 14);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 15);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 16);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 17);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 18);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 19);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 20);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 21);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 22);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 23);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 24);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 25);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 26);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 27);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 28);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 29);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 30);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 31);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 32);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 33);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 34);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 35);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 36);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 37);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 38);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 39);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 40);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 41);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 42);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 43);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 44);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 45);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 46);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 47);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 48);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 49);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 50);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 51);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 52);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 53);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 54);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 55);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 56);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 57);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 58);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 59);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 60);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 61);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 62);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 63);

    // First hash complete - byte-swap for second hash input
    tmp.hash[0] = BYTESWAP32(WA[0] + midpoint->hash[0]);
    tmp.hash[1] = BYTESWAP32(WA[1] + midpoint->hash[1]);
    tmp.hash[2] = BYTESWAP32(WA[2] + midpoint->hash[2]);
    tmp.hash[3] = BYTESWAP32(WA[3] + midpoint->hash[3]);
    tmp.hash[4] = BYTESWAP32(WA[4] + midpoint->hash[4]);
    tmp.hash[5] = BYTESWAP32(WA[5] + midpoint->hash[5]);
    tmp.hash[6] = BYTESWAP32(WA[6] + midpoint->hash[6]);
    tmp.hash[7] = BYTESWAP32(WA[7] + midpoint->hash[7]);

    // Copy first hash into working area for double hash
    data = (uint8_t *)tmp.hash;

    w[0] = GET_DATA(data, 0);
    w[1] = GET_DATA(data, 4);
    w[2] = GET_DATA(data, 8);
    w[3] = GET_DATA(data, 12);
    w[4] = GET_DATA(data, 16);
    w[5] = GET_DATA(data, 20);
    w[6] = GET_DATA(data, 24);
    w[7] = GET_DATA(data, 28);

    w[8] = 0x80000000;
    w[9] = w[10] = w[11] = w[12] = w[13] = w[14] = 0;
    w[15] = 0x00000100;  // 256 bits

    WA[0] = h0;
    WA[1] = h1;
    WA[2] = h2;
    WA[3] = h3;
    WA[4] = h4;
    WA[5] = h5;
    WA[6] = h6;
    WA[7] = h7;

    // Abbreviated macros where there is no data
    R1_a(16); R1_a(17); R1_a(18); R1_a(19); R1_a(20); R1_a(21);
    R1(22); R1(23);
    R1_b(24);
    R1_c(25); R1_c(26); R1_c(27); R1_c(28); R1_c(29);
    R1_d(30);
    R1(31); R1(32); R1(33); R1(34); R1(35);
    R1(36); R1(37); R1(38); R1(39); R1(40); R1(41); R1(42); R1(43); R1(44); R1(45);
    R1(46); R1(47); R1(48); R1(49); R1(50); R1(51); R1(52); R1(53); R1(54); R1(55);
    R1(56); R1(57); R1(58); R1(59); R1(60); R1(61); R1(62); R1(63);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 0);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 1);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 2);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 3);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 4);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 5);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 6);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 7);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 8);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 9);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 10);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 11);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 12);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 13);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 14);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 15);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 16);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 17);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 18);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 19);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 20);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 21);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 22);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 23);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 24);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 25);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 26);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 27);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 28);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 29);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 30);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 31);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 32);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 33);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 34);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 35);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 36);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 37);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 38);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 39);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 40);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 41);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 42);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 43);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 44);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 45);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 46);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 47);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 48);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 49);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 50);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 51);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 52);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 53);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 54);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 55);

    CM(0, 1, 2, 3, 4, 5, 6, 7, 56);
    CM(7, 0, 1, 2, 3, 4, 5, 6, 57);
    CM(6, 7, 0, 1, 2, 3, 4, 5, 58);
    CM(5, 6, 7, 0, 1, 2, 3, 4, 59);
    CM(4, 5, 6, 7, 0, 1, 2, 3, 60);
    CM(3, 4, 5, 6, 7, 0, 1, 2, 61);
    CM(2, 3, 4, 5, 6, 7, 0, 1, 62);
    CM(1, 2, 3, 4, 5, 6, 7, 0, 63);

    // Early 16-bit reject - no need to continue if we don't have a good hash
    ctx->hash[7] = WA[7] + h7;
    if (ctx->hash[7] & 0xffff) return false;

    // Complete the output hash with byte-swap
    ctx->hash[0] = BYTESWAP32(WA[0] + h0);
    ctx->hash[1] = BYTESWAP32(WA[1] + h1);
    ctx->hash[2] = BYTESWAP32(WA[2] + h2);
    ctx->hash[3] = BYTESWAP32(WA[3] + h3);
    ctx->hash[4] = BYTESWAP32(WA[4] + h4);
    ctx->hash[5] = BYTESWAP32(WA[5] + h5);
    ctx->hash[6] = BYTESWAP32(WA[6] + h6);
    ctx->hash[7] = BYTESWAP32(ctx->hash[7]);

    return true;
}
