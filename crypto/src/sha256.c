/*
 * Copyright (C) 2014, jpn jpn@gsforce.net
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "../sha256.h"


#define ROTR(X, N) (((X) >> (N)) | ((X) << (32 - (N))))


/* SHA-256 uses six logical functions, where each function operates on 32-bit
 * words, which are represented as x, y, and z. The result of each function is
 * a new 32-bit word. */
#define C(X, Y, Z) (((X) & (Y)) ^ (~(X) & (Z)))
#define M(X, Y, Z) (((X) & (Y)) ^ ( (X) & (Z)) ^ ((Y) & (Z)))

#define SIGMA0(x) (ROTR(x,  2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define SIGMA1(x) (ROTR(x,  6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define GAMMA0(x) (ROTR(x,  7) ^ ROTR(x, 18) ^ ((x) >>  3))
#define GAMMA1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))


/* for unrolled convenience */

#define ROUNDx1(a, b, c, d, e, f, g, h, constant, v) \
	x = constant + v + h + SIGMA1(e) + C(e, f, g);   \
	h =                x + SIGMA0(a) + M(a, b, c);   \
	d = d + x;


#define ROUNDx8(c0, c1, c2, c3, c4, c5, c6, c7, i) \
	ROUNDx1(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], c0, v[(i) + 0]) \
	ROUNDx1(s[7], s[0], s[1], s[2], s[3], s[4], s[5], s[6], c1, v[(i) + 1]) \
	ROUNDx1(s[6], s[7], s[0], s[1], s[2], s[3], s[4], s[5], c2, v[(i) + 2]) \
	ROUNDx1(s[5], s[6], s[7], s[0], s[1], s[2], s[3], s[4], c3, v[(i) + 3]) \
	ROUNDx1(s[4], s[5], s[6], s[7], s[0], s[1], s[2], s[3], c4, v[(i) + 4]) \
	ROUNDx1(s[3], s[4], s[5], s[6], s[7], s[0], s[1], s[2], c5, v[(i) + 5]) \
	ROUNDx1(s[2], s[3], s[4], s[5], s[6], s[7], s[0], s[1], c6, v[(i) + 6]) \
	ROUNDx1(s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[0], c7, v[(i) + 7])


static void
sha256_compress(uint32 state[8], const uint8 data[64])
{
	uint32 s[ 8];
	uint32 v[64];
	uint32 x;
	uintxx i;
	
	/* load the input */
	for (i = 0; i < 16; i++) {
		v[i] = ((uint32) data[0]) << 0x18 |
			   ((uint32) data[1]) << 0x10 |
			   ((uint32) data[2]) << 0x08 |
			   ((uint32) data[3]);
		data += 4;
	}
	
	for (i = 16; i < 64; i++)
		v[i] = GAMMA1(v[i -  2]) + v[i - 7] + GAMMA0(v[i - 15]) + v[i - 16];
	
	/* load the state */
	s[0] = state[0];
	s[1] = state[1];
	s[2] = state[2];
	s[3] = state[3];
	s[4] = state[4];
	s[5] = state[5];
	s[6] = state[6];
	s[7] = state[7];
	
	/* iterate */
	ROUNDx8(0x428a2f98UL, 0x71374491UL,
			0xb5c0fbcfUL, 0xe9b5dba5UL,
			0x3956c25bUL, 0x59f111f1UL,
			0x923f82a4UL, 0xab1c5ed5UL, 0 << 3);
	
	ROUNDx8(0xd807aa98UL, 0x12835b01UL,
			0x243185beUL, 0x550c7dc3UL,
			0x72be5d74UL, 0x80deb1feUL,
			0x9bdc06a7UL, 0xc19bf174UL, 1 << 3);
	
	ROUNDx8(0xe49b69c1UL, 0xefbe4786UL,
			0x0fc19dc6UL, 0x240ca1ccUL,
			0x2de92c6fUL, 0x4a7484aaUL,
			0x5cb0a9dcUL, 0x76f988daUL, 2 << 3);
	
	ROUNDx8(0x983e5152UL, 0xa831c66dUL,
			0xb00327c8UL, 0xbf597fc7UL,
			0xc6e00bf3UL, 0xd5a79147UL,
			0x06ca6351UL, 0x14292967UL, 3 << 3);
	
	ROUNDx8(0x27b70a85UL, 0x2e1b2138UL,
			0x4d2c6dfcUL, 0x53380d13UL,
			0x650a7354UL, 0x766a0abbUL,
			0x81c2c92eUL, 0x92722c85UL, 4 << 3);
	
	ROUNDx8(0xa2bfe8a1UL, 0xa81a664bUL,
			0xc24b8b70UL, 0xc76c51a3UL,
			0xd192e819UL, 0xd6990624UL,
			0xf40e3585UL, 0x106aa070UL, 5 << 3);
	
	ROUNDx8(0x19a4c116UL, 0x1e376c08UL,
			0x2748774cUL, 0x34b0bcb5UL,
			0x391c0cb3UL, 0x4ed8aa4aUL,
			0x5b9cca4fUL, 0x682e6ff3UL, 6 << 3);
	
	ROUNDx8(0x748f82eeUL, 0x78a5636fUL,
			0x84c87814UL, 0x8cc70208UL,
			0x90befffaUL, 0xa4506cebUL,
			0xbef9a3f7UL, 0xc67178f2UL, 7 << 3);
	
	/* adds the results into the digest state */
	state[0] += s[0];
	state[1] += s[1];
	state[2] += s[2];
	state[3] += s[3];
	state[4] += s[4];
	state[5] += s[5];
	state[6] += s[6];
	state[7] += s[7];
}

#undef C
#undef M
#undef ROUNDx1
#undef ROUNDx8


void
sha256_update(TSHA256ctx* context, const uint8* data, uintxx size)
{
	uintxx rmnng;
	uintxx i;
	ASSERT(context && data);
	
	if (context->rmnng) {
		rmnng = SHA256_BLOCKSZ - context->rmnng;
		if (rmnng > size)
			rmnng = size;
		
		for (i = 0; i < rmnng; i++) {
			context->rdata[context->rmnng++] = *data++;
		}
		size -= i;
		
		if (context->rmnng == SHA256_BLOCKSZ) {
			sha256_compress(context->state, context->rdata);
			
			context->rmnng = 0;
			context->blcks++;
		}
		else {
			return;
		}
	}
	
	while (size >= SHA256_BLOCKSZ) {
		sha256_compress(context->state, data);
		
		size -= SHA256_BLOCKSZ;
		data += SHA256_BLOCKSZ;
		context->blcks++;  /* we 'll scale it later */
	}
	
	if (size) {
		for (i = 0; i < size; i++) {
			context->rdata[context->rmnng++] = *data++;
		}
	}
}


/* The message, M, shall be padded before hash computation begins. The purpose
 * of this padding is to ensure that the padded message is a multiple of 512
 * or 1024 bits, depending on the algorithm.
 * 
 * Suppose that the length of the message, M, is x bits. Append the bit "1" to
 * the end of the message, followed by k zero bits, where k is the smallest,
 * non-negative solution to the equation x + 1 + k = 448 mod 512. Then append
 * the 64-bit block that is equal to the number x expressed using a binary
 * representation. */

void
sha256_final(TSHA256ctx* context, uint32 digest[8])
{
	uintxx length;
	uintxx tmp;
	uint32 nlo;
	uint32 nhi;
	ASSERT(context && digest);
	
	context->rdata[length = context->rmnng] = 0x80;
	length++;
	
	if (length > SHA256_BLOCKSZ - 8) {
		
		while (length < SHA256_BLOCKSZ)
			context->rdata[length++] = 0;
		
		sha256_compress(context->state, context->rdata);
		length = 0;
	}
	
	while (length < (SHA256_BLOCKSZ - 8))  /* pad with zeros */
		context->rdata[length++] = 0;
	
	/* scales the numbers of bits */
	nhi = context->blcks >> (32 - 9);
	nlo = context->blcks << 9;
	
	/* add the remainings bits */
	tmp = nlo;
	if ((nlo += ((uint32) context->rmnng << 3)) < tmp)
		nhi++;
	
	context->rdata[56] = nhi >> 0x18;
	context->rdata[57] = nhi >> 0x10;
	context->rdata[58] = nhi >> 0x08;
	context->rdata[59] = nhi;
	
	context->rdata[60] = nlo >> 0x18;
	context->rdata[61] = nlo >> 0x10;
	context->rdata[62] = nlo >> 0x08;
	context->rdata[63] = nlo;
	
	sha256_compress(context->state, context->rdata);
	digest[0] = context->state[0];
	digest[1] = context->state[1];
	digest[2] = context->state[2];
	digest[3] = context->state[3];
	digest[4] = context->state[4];
	digest[5] = context->state[5];
	digest[6] = context->state[6];
	digest[7] = context->state[7];
}
