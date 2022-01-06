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

#include "../sha1.h"


#define ROTL(X, N) (((X) << (N)) | ((X) >> (32 - (N))))


#define C(X, Y, Z) (((X) & (Y)) ^ (~(X) & (Z)))
#define M(X, Y, Z) (((X) & (Y)) ^ ( (X) & (Z)) ^ ((Y) & (Z)))
#define P(X, Y, Z) ((X) ^ (Y) ^ (Z))


/* for unrolled convenience */
#define ROUNDx1(a, b, c, d, e, FNCTN, constant, v) \
	e = ROTL(a,  5) + e + constant + FNCTN(b, c, d) + v; \
	b = ROTL(b, 30);


#define F1(i) \
	ROTL(( \
		v[((i) + 13) & 0x0F] ^ \
		v[((i) +  8) & 0x0F] ^ \
		v[((i) +  2) & 0x0F] ^ \
		v[((i) & 0x0F)]), 1)

#define F2(i) (v[(i) & 0x0F] = F1(i))


static void
sha1_compress(uint32 state[5], const uint8 data[64])
{
	uint32 s[ 5];
	uint32 v[16];
	uintxx i;
	
	/* load the input */
	for (i = 0; i < 16; i++) {
		v[i] = ((uint32) data[0]) << 0x18 |
			   ((uint32) data[1]) << 0x10 |
			   ((uint32) data[2]) << 0x08 |
			   ((uint32) data[3]);
		data += 4;
	}
	
	s[0] = state[0];
	s[1] = state[1];
	s[2] = state[2];
	s[3] = state[3];
	s[4] = state[4];
	
	/* iterate */
	ROUNDx1(s[0], s[1], s[2], s[3], s[4], C, 0x5a827999UL, v[ 0]);
	ROUNDx1(s[4], s[0], s[1], s[2], s[3], C, 0x5a827999UL, v[ 1]);
	ROUNDx1(s[3], s[4], s[0], s[1], s[2], C, 0x5a827999UL, v[ 2]);
	ROUNDx1(s[2], s[3], s[4], s[0], s[1], C, 0x5a827999UL, v[ 3]);
	ROUNDx1(s[1], s[2], s[3], s[4], s[0], C, 0x5a827999UL, v[ 4]);
	ROUNDx1(s[0], s[1], s[2], s[3], s[4], C, 0x5a827999UL, v[ 5]);
	ROUNDx1(s[4], s[0], s[1], s[2], s[3], C, 0x5a827999UL, v[ 6]);
	ROUNDx1(s[3], s[4], s[0], s[1], s[2], C, 0x5a827999UL, v[ 7]);
	ROUNDx1(s[2], s[3], s[4], s[0], s[1], C, 0x5a827999UL, v[ 8]);
	ROUNDx1(s[1], s[2], s[3], s[4], s[0], C, 0x5a827999UL, v[ 9]);
	ROUNDx1(s[0], s[1], s[2], s[3], s[4], C, 0x5a827999UL, v[10]);
	ROUNDx1(s[4], s[0], s[1], s[2], s[3], C, 0x5a827999UL, v[11]);
	ROUNDx1(s[3], s[4], s[0], s[1], s[2], C, 0x5a827999UL, v[12]);
	ROUNDx1(s[2], s[3], s[4], s[0], s[1], C, 0x5a827999UL, v[13]);
	ROUNDx1(s[1], s[2], s[3], s[4], s[0], C, 0x5a827999UL, v[14]);
	ROUNDx1(s[0], s[1], s[2], s[3], s[4], C, 0x5a827999UL, v[15]);
	
	ROUNDx1(s[4], s[0], s[1], s[2], s[3], C, 0x5a827999UL, F2(16));
	ROUNDx1(s[3], s[4], s[0], s[1], s[2], C, 0x5a827999UL, F2(17));
	ROUNDx1(s[2], s[3], s[4], s[0], s[1], C, 0x5a827999UL, F2(18));
	ROUNDx1(s[1], s[2], s[3], s[4], s[0], C, 0x5a827999UL, F2(19));
	
	ROUNDx1(s[0], s[1], s[2], s[3], s[4], P, 0x6ed9eba1UL, F2(20));
	ROUNDx1(s[4], s[0], s[1], s[2], s[3], P, 0x6ed9eba1UL, F2(21));
	ROUNDx1(s[3], s[4], s[0], s[1], s[2], P, 0x6ed9eba1UL, F2(22));
	ROUNDx1(s[2], s[3], s[4], s[0], s[1], P, 0x6ed9eba1UL, F2(23));
	ROUNDx1(s[1], s[2], s[3], s[4], s[0], P, 0x6ed9eba1UL, F2(24));
	ROUNDx1(s[0], s[1], s[2], s[3], s[4], P, 0x6ed9eba1UL, F2(25));
	ROUNDx1(s[4], s[0], s[1], s[2], s[3], P, 0x6ed9eba1UL, F2(26));
	ROUNDx1(s[3], s[4], s[0], s[1], s[2], P, 0x6ed9eba1UL, F2(27));
	ROUNDx1(s[2], s[3], s[4], s[0], s[1], P, 0x6ed9eba1UL, F2(28));
	ROUNDx1(s[1], s[2], s[3], s[4], s[0], P, 0x6ed9eba1UL, F2(29));
	ROUNDx1(s[0], s[1], s[2], s[3], s[4], P, 0x6ed9eba1UL, F2(30));
	ROUNDx1(s[4], s[0], s[1], s[2], s[3], P, 0x6ed9eba1UL, F2(31));
	ROUNDx1(s[3], s[4], s[0], s[1], s[2], P, 0x6ed9eba1UL, F2(32));
	ROUNDx1(s[2], s[3], s[4], s[0], s[1], P, 0x6ed9eba1UL, F2(33));
	ROUNDx1(s[1], s[2], s[3], s[4], s[0], P, 0x6ed9eba1UL, F2(34));
	ROUNDx1(s[0], s[1], s[2], s[3], s[4], P, 0x6ed9eba1UL, F2(35));
	ROUNDx1(s[4], s[0], s[1], s[2], s[3], P, 0x6ed9eba1UL, F2(36));
	ROUNDx1(s[3], s[4], s[0], s[1], s[2], P, 0x6ed9eba1UL, F2(37));
	ROUNDx1(s[2], s[3], s[4], s[0], s[1], P, 0x6ed9eba1UL, F2(38));
	ROUNDx1(s[1], s[2], s[3], s[4], s[0], P, 0x6ed9eba1UL, F2(39));
	
	ROUNDx1(s[0], s[1], s[2], s[3], s[4], M, 0x8f1bbcdcUL, F2(40));
	ROUNDx1(s[4], s[0], s[1], s[2], s[3], M, 0x8f1bbcdcUL, F2(41));
	ROUNDx1(s[3], s[4], s[0], s[1], s[2], M, 0x8f1bbcdcUL, F2(42));
	ROUNDx1(s[2], s[3], s[4], s[0], s[1], M, 0x8f1bbcdcUL, F2(43));
	ROUNDx1(s[1], s[2], s[3], s[4], s[0], M, 0x8f1bbcdcUL, F2(44));
	ROUNDx1(s[0], s[1], s[2], s[3], s[4], M, 0x8f1bbcdcUL, F2(45));
	ROUNDx1(s[4], s[0], s[1], s[2], s[3], M, 0x8f1bbcdcUL, F2(46));
	ROUNDx1(s[3], s[4], s[0], s[1], s[2], M, 0x8f1bbcdcUL, F2(47));
	ROUNDx1(s[2], s[3], s[4], s[0], s[1], M, 0x8f1bbcdcUL, F2(48));
	ROUNDx1(s[1], s[2], s[3], s[4], s[0], M, 0x8f1bbcdcUL, F2(49));
	ROUNDx1(s[0], s[1], s[2], s[3], s[4], M, 0x8f1bbcdcUL, F2(50));
	ROUNDx1(s[4], s[0], s[1], s[2], s[3], M, 0x8f1bbcdcUL, F2(51));
	ROUNDx1(s[3], s[4], s[0], s[1], s[2], M, 0x8f1bbcdcUL, F2(52));
	ROUNDx1(s[2], s[3], s[4], s[0], s[1], M, 0x8f1bbcdcUL, F2(53));
	ROUNDx1(s[1], s[2], s[3], s[4], s[0], M, 0x8f1bbcdcUL, F2(54));
	ROUNDx1(s[0], s[1], s[2], s[3], s[4], M, 0x8f1bbcdcUL, F2(55));
	ROUNDx1(s[4], s[0], s[1], s[2], s[3], M, 0x8f1bbcdcUL, F2(56));
	ROUNDx1(s[3], s[4], s[0], s[1], s[2], M, 0x8f1bbcdcUL, F2(57));
	ROUNDx1(s[2], s[3], s[4], s[0], s[1], M, 0x8f1bbcdcUL, F2(58));
	ROUNDx1(s[1], s[2], s[3], s[4], s[0], M, 0x8f1bbcdcUL, F2(59));
	
	ROUNDx1(s[0], s[1], s[2], s[3], s[4], P, 0xca62c1d6UL, F2(60));
	ROUNDx1(s[4], s[0], s[1], s[2], s[3], P, 0xca62c1d6UL, F2(61));
	ROUNDx1(s[3], s[4], s[0], s[1], s[2], P, 0xca62c1d6UL, F2(62));
	ROUNDx1(s[2], s[3], s[4], s[0], s[1], P, 0xca62c1d6UL, F2(63));
	ROUNDx1(s[1], s[2], s[3], s[4], s[0], P, 0xca62c1d6UL, F2(64));
	ROUNDx1(s[0], s[1], s[2], s[3], s[4], P, 0xca62c1d6UL, F2(65));
	ROUNDx1(s[4], s[0], s[1], s[2], s[3], P, 0xca62c1d6UL, F2(66));
	ROUNDx1(s[3], s[4], s[0], s[1], s[2], P, 0xca62c1d6UL, F2(67));
	ROUNDx1(s[2], s[3], s[4], s[0], s[1], P, 0xca62c1d6UL, F2(68));
	ROUNDx1(s[1], s[2], s[3], s[4], s[0], P, 0xca62c1d6UL, F2(69));
	ROUNDx1(s[0], s[1], s[2], s[3], s[4], P, 0xca62c1d6UL, F2(70));
	ROUNDx1(s[4], s[0], s[1], s[2], s[3], P, 0xca62c1d6UL, F2(71));
	ROUNDx1(s[3], s[4], s[0], s[1], s[2], P, 0xca62c1d6UL, F2(72));
	ROUNDx1(s[2], s[3], s[4], s[0], s[1], P, 0xca62c1d6UL, F2(73));
	ROUNDx1(s[1], s[2], s[3], s[4], s[0], P, 0xca62c1d6UL, F2(74));
	ROUNDx1(s[0], s[1], s[2], s[3], s[4], P, 0xca62c1d6UL, F2(75));
	ROUNDx1(s[4], s[0], s[1], s[2], s[3], P, 0xca62c1d6UL, F2(76));
	ROUNDx1(s[3], s[4], s[0], s[1], s[2], P, 0xca62c1d6UL, F2(77));
	ROUNDx1(s[2], s[3], s[4], s[0], s[1], P, 0xca62c1d6UL, F2(78));
	ROUNDx1(s[1], s[2], s[3], s[4], s[0], P, 0xca62c1d6UL, F2(79));
	
	/* adds the results into the digest state */
	state[0] += s[0];
	state[1] += s[1];
	state[2] += s[2];
	state[3] += s[3];
	state[4] += s[4];
}

#undef C
#undef M
#undef P
#undef ROUNDx1
#undef F1
#undef F2


void
sha1_update(TSHA1ctx* context, const uint8* data, uintxx size)
{
	uintxx rmnng;
	uintxx i;
	ASSERT(context && data);
	
	if (context->rmnng) {
		rmnng = SHA1_BLOCKSZ - context->rmnng;
		if (rmnng > size)
			rmnng = size;
		
		for (i = 0; i < rmnng; i++) {
			context->rdata[context->rmnng++] = *data++;
		}
		size -= i;
		
		if (context->rmnng == SHA1_BLOCKSZ) {
			sha1_compress(context->state, context->rdata);
			
			context->rmnng = 0;
			context->blcks++;
		}
		else {
			return;
		}
	}
	
	while (size >= SHA1_BLOCKSZ) {
		sha1_compress(context->state, data);
		
		size -= SHA1_BLOCKSZ;
		data += SHA1_BLOCKSZ;
		context->blcks++;  /* we 'll scale it later */
	}
	
	if (size) {
		for (i = 0; i < size; i++) {
			context->rdata[context->rmnng++] = *data++;
		}
	}
}


/*
 * The message, M, shall be padded before hash computation begins. The purpose
 * of this padding is to ensure that the padded message is a multiple of 512 or
 * 1024 bits, depending on the algorithm.
 *
 * Suppose that the length of the message, M, is x bits. Append the bit "1" to
 * the end of the message, followed by k zero bits, where k is the smallest,
 * non-negative solution to the equation x + 1 + k = 448 mod 512. Then append
 * the 64-bit block that is equal to the number x expressed using a binary
 * representation. */

void
sha1_final(TSHA1ctx* context, uint32 digest[5])
{
	uintxx length;
	uintxx tmp;
	uint32 nlo;
	uint32 nhi;
	ASSERT(context && digest);
	
	context->rdata[length = context->rmnng] = 0x80;
	length++;
	
	if (length > SHA1_BLOCKSZ - 8) {
		while (length < SHA1_BLOCKSZ)
			context->rdata[length++] = 0;
		
		sha1_compress(context->state, context->rdata);
		length = 0;
	}
	
	while (length < (SHA1_BLOCKSZ - 8))  /* pad with zeros */
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
	
	sha1_compress(context->state, context->rdata);
	digest[0] = context->state[0];
	digest[1] = context->state[1];
	digest[2] = context->state[2];
	digest[3] = context->state[3];
	digest[4] = context->state[4];
}
