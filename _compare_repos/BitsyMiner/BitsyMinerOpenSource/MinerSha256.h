/* 
 * BitsyMiner Open Source
 * Copyright (c) 2025 Justin Williams
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef MINER_SHA256_H
#define MINER_SHA256_H

#include "miner.h"

typedef union {
  unsigned long hash[8];
  unsigned char bytes[32];
} miner_sha256_hash;

void sha256(miner_sha256_hash *ctx, unsigned char* msg, size_t len);
void sha256midstate(miner_sha256_hash *ctx, hash_block *hb);
bool  sha256header(miner_sha256_hash *midpoint, miner_sha256_hash *ctx, hash_block *hb);

#endif