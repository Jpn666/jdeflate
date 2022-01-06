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

#include "../sha512.h"


#define ROTR(X, N) (((X) >> (N)) | ((X) << (64 - (N))))


/* SHA-384 and SHA-512 each use six logical functions, where each function
 * operates on 64-bit words, which are represented as x, y, and z. The result
 * of each function is a new 64-bit word. */
#define C(X, Y, Z) (((X) & (Y)) ^ (~(X) & (Z)))
#define M(X, Y, Z) (((X) & (Y)) ^ ( (X) & (Z)) ^ ((Y) & (Z)))

#define SIGMA0(x) (ROTR(x, 28) ^ ROTR(x, 34) ^ ROTR(x, 39))
#define SIGMA1(x) (ROTR(x, 14) ^ ROTR(x, 18) ^ ROTR(x, 41))
#define GAMMA0(x) (ROTR(x,  1) ^ ROTR(x,  8) ^ ((x) >>  7))
#define GAMMA1(x) (ROTR(x, 19) ^ ROTR(x, 61) ^ ((x) >>  6))


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
sha512_compress(uint64 state[8], const uint8 data[128])
{
	uint64 s[ 8];
	uint64 v[80];
	uint64 x;
	uintxx i;
	
	/* load the input */
	for (i = 0; i < 16; i++) {
		v[i] = ((uint64) data[0]) << 0x38 |
			   ((uint64) data[1]) << 0x30 |
			   ((uint64) data[2]) << 0x28 |
			   ((uint64) data[3]) << 0x20 |
			   ((uint64) data[4]) << 0x18 |
			   ((uint64) data[5]) << 0x10 |
			   ((uint64) data[6]) << 0x08 |
			   ((uint64) data[7]);
		data += 8;
	}
	
	for (i = 16; i < 80; i++)
		v[i] = GAMMA1(v[i - 2]) + v[i - 7] + GAMMA0(v[i - 15]) + v[i - 16];
	
	/* load the state */
	s[0] = state[0];
	s[1] = state[1];
	s[2] = state[2];
	s[3] = state[3];
	s[4] = state[4];
	s[5] = state[5];
	s[6] = state[6];
	s[7] = state[7];
	
	ROUNDx8(0x428a2f98d728ae22LL, 0x7137449123ef65cdLL,
			0xb5c0fbcfec4d3b2fLL, 0xe9b5dba58189dbbcLL,
			0x3956c25bf348b538LL, 0x59f111f1b605d019LL,
			0x923f82a4af194f9bLL, 0xab1c5ed5da6d8118LL, 0 << 3);
	
	ROUNDx8(0xd807aa98a3030242LL, 0x12835b0145706fbeLL,
			0x243185be4ee4b28cLL, 0x550c7dc3d5ffb4e2LL,
			0x72be5d74f27b896fLL, 0x80deb1fe3b1696b1LL,
			0x9bdc06a725c71235LL, 0xc19bf174cf692694LL, 1 << 3);
	
	ROUNDx8(0xe49b69c19ef14ad2LL, 0xefbe4786384f25e3LL,
			0x0fc19dc68b8cd5b5LL, 0x240ca1cc77ac9c65LL, 
			0x2de92c6f592b0275LL, 0x4a7484aa6ea6e483LL,
			0x5cb0a9dcbd41fbd4LL, 0x76f988da831153b5LL, 2 << 3);
	
	ROUNDx8(0x983e5152ee66dfabLL, 0xa831c66d2db43210LL,
			0xb00327c898fb213fLL, 0xbf597fc7beef0ee4LL,
			0xc6e00bf33da88fc2LL, 0xd5a79147930aa725LL,
			0x06ca6351e003826fLL, 0x142929670a0e6e70LL, 3 << 3);
	
	ROUNDx8(0x27b70a8546d22ffcLL, 0x2e1b21385c26c926LL,
			0x4d2c6dfc5ac42aedLL, 0x53380d139d95b3dfLL,
			0x650a73548baf63deLL, 0x766a0abb3c77b2a8LL,
			0x81c2c92e47edaee6LL, 0x92722c851482353bLL, 4 << 3);
	
	ROUNDx8(0xa2bfe8a14cf10364LL, 0xa81a664bbc423001LL,
			0xc24b8b70d0f89791LL, 0xc76c51a30654be30LL,
			0xd192e819d6ef5218LL, 0xd69906245565a910LL,
			0xf40e35855771202aLL, 0x106aa07032bbd1b8LL, 5 << 3);
	
	ROUNDx8(0x19a4c116b8d2d0c8LL, 0x1e376c085141ab53LL,
			0x2748774cdf8eeb99LL, 0x34b0bcb5e19b48a8LL,
			0x391c0cb3c5c95a63LL, 0x4ed8aa4ae3418acbLL,
			0x5b9cca4f7763e373LL, 0x682e6ff3d6b2b8a3LL, 6 << 3);
	
	ROUNDx8(0x748f82ee5defb2fcLL, 0x78a5636f43172f60LL,
			0x84c87814a1f0ab72LL, 0x8cc702081a6439ecLL,
			0x90befffa23631e28LL, 0xa4506cebde82bde9LL,
			0xbef9a3f7b2c67915LL, 0xc67178f2e372532bLL, 7 << 3);
	
	ROUNDx8(0xca273eceea26619cLL, 0xd186b8c721c0c207LL,
			0xeada7dd6cde0eb1eLL, 0xf57d4f7fee6ed178LL,
			0x06f067aa72176fbaLL, 0x0a637dc5a2c898a6LL,
			0x113f9804bef90daeLL, 0x1b710b35131c471bLL, 8 << 3);
	
	ROUNDx8(0x28db77f523047d84LL, 0x32caab7b40c72493LL,
			0x3c9ebe0a15c9bebcLL, 0x431d67c49c100d4cLL,
			0x4cc5d4becb3e42b6LL, 0x597f299cfc657e2aLL,
			0x5fcb6fab3ad6faecLL, 0x6c44198c4a475817LL, 9 << 3);
	
	/* adds the results into digest the state */
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
sha512_update(TSHA512ctx* context, const uint8* data, uintxx size)
{
	uintxx rmnng;
	uintxx i;
	ASSERT(context && data);
	
	if (context->rmnng) {
		rmnng = SHA512_BLOCKSZ - context->rmnng;
		if (rmnng > size)
			rmnng = size;
		
		for (i = 0; i < rmnng; i++) {
			context->rdata[context->rmnng++] = *data++;
		}
		size -= i;
		
		if (context->rmnng == SHA512_BLOCKSZ) {
			sha512_compress(context->state, context->rdata);
			
			context->rmnng = 0;
			context->blcks++;
		}
		else {
			return;
		}
	}
	
	while (size >= SHA512_BLOCKSZ) {
		sha512_compress(context->state, data);
		
		size -= SHA512_BLOCKSZ;
		data += SHA512_BLOCKSZ;
		context->blcks++;  /* we 'll scale it later */
	}
	
	if (size) {
		for (i = 0; i < size; i++) {
			context->rdata[context->rmnng++] = *data++;
		}
	}
}

void
sha512_final(TSHA512ctx* context, uint64 digest[8])
{
	uintxx length;
	uint64 tmp;
	uint64 nlo;
	uint64 nhi;
	ASSERT(context && digest);
	
	context->rdata[length = context->rmnng] = 0x80;
	length++;
	
	if (length > SHA512_BLOCKSZ - 16) {
		while (length < SHA512_BLOCKSZ)
			context->rdata[length++] = 0;
		
		sha512_compress(context->state, context->rdata);
		length = 0;
	}
	
	while (length < (SHA512_BLOCKSZ - 16))  /* pad with zeros */
		context->rdata[length++] = 0;
	
	/* scales the numbers of bits */
	nhi = context->blcks >> (64 - 10);
	nlo = context->blcks << 10;
	
	/* add the remainings bits */
	tmp = nlo;
	if ((nlo += (context->rmnng << 3)) < tmp)
		nhi++;
	
	context->rdata[112] = (uint8) (nhi >> 0x38);
	context->rdata[113] = (uint8) (nhi >> 0x30);
	context->rdata[114] = (uint8) (nhi >> 0x28);
	context->rdata[115] = (uint8) (nhi >> 0x20);
	context->rdata[116] = (uint8) (nhi >> 0x18);
	context->rdata[117] = (uint8) (nhi >> 0x10);
	context->rdata[118] = (uint8) (nhi >> 0x08);
	context->rdata[119] = (uint8) (nhi);
	
	context->rdata[120] = (uint8) (nlo >> 0x38);
	context->rdata[121] = (uint8) (nlo >> 0x30);
	context->rdata[122] = (uint8) (nlo >> 0x28);
	context->rdata[123] = (uint8) (nlo >> 0x20);
	context->rdata[124] = (uint8) (nlo >> 0x18);
	context->rdata[125] = (uint8) (nlo >> 0x10);
	context->rdata[126] = (uint8) (nlo >> 0x08);
	context->rdata[127] = (uint8) (nlo);
	
	sha512_compress(context->state, context->rdata);
	digest[0] = context->state[0];
	digest[1] = context->state[1];
	digest[2] = context->state[2];
	digest[3] = context->state[3];
	digest[4] = context->state[4];
	digest[5] = context->state[5];
	digest[6] = context->state[6];
	digest[7] = context->state[7];
}
