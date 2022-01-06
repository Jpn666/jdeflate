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

#include "../md5.h"


#define ROTL(X, N) (((X) << (N)) | ((X) >> (32 - (N))))
 
/* F, G, H and I are basic MD5 functions. */

#define F(X, Y, Z) (((X) & (Y)) | ((~(X)) & (Z)))
#define G(X, Y, Z) (((X) & (Z)) | ((Y) & (~(Z))))
#define H(X, Y, Z) ( (X) ^ (Y) ^ (Z))
#define I(X, Y, Z) ( (Y) ^ ((X) | (~(Z))))


/* FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4.
 * Rotation is separate from addition to prevent recomputation. */

#define FF(a, b, c, d, x, s, constant) \
	a += F(b, c, d) + x + (constant); a = ROTL(a, s) + b;

#define GG(a, b, c, d, x, s, constant) \
	a += G(b, c, d) + x + (constant); a = ROTL(a, s) + b;

#define HH(a, b, c, d, x, s, constant) \
	a += H(b, c, d) + x + (constant); a = ROTL(a, s) + b;

#define II(a, b, c, d, x, s, constant) \
	a += I(b, c, d) + x + (constant); a = ROTL(a, s) + b;


#define S11  7
#define S12 12
#define S13 17
#define S14 22
#define S21  5
#define S22  9
#define S23 14
#define S24 20
#define S31  4
#define S32 11
#define S33 16
#define S34 23
#define S41  6
#define S42 10
#define S43 15
#define S44 21


static void
md5_compress(uint32 state[4], const uint8 data[64])
{
	uint32 a;
	uint32 b;
	uint32 c;
	uint32 d;
	uint32 v[16];
	uintxx i;
	
	for (i = 0; i < 16; i++) {
		v[i] = ((uint32) data[3]) << 0x18 |
		       ((uint32) data[2]) << 0x10 |
		       ((uint32) data[1]) << 0x08 |
		       ((uint32) data[0]);
		data += 4;
	}
	
	a = state[0];
	b = state[1];
	c = state[2];
	d = state[3];
	
	/* round 1 */
	FF(a, b, c, d, v[ 0], S11, 0xd76aa478UL);  /*  1 */
	FF(d, a, b, c, v[ 1], S12, 0xe8c7b756UL);  /*  2 */
	FF(c, d, a, b, v[ 2], S13, 0x242070dbUL);  /*  3 */
	FF(b, c, d, a, v[ 3], S14, 0xc1bdceeeUL);  /*  4 */
	FF(a, b, c, d, v[ 4], S11, 0xf57c0fafUL);  /*  5 */
	FF(d, a, b, c, v[ 5], S12, 0x4787c62aUL);  /*  6 */
	FF(c, d, a, b, v[ 6], S13, 0xa8304613UL);  /*  7 */
	FF(b, c, d, a, v[ 7], S14, 0xfd469501UL);  /*  8 */
	FF(a, b, c, d, v[ 8], S11, 0x698098d8UL);  /*  9 */
	FF(d, a, b, c, v[ 9], S12, 0x8b44f7afUL);  /* 10 */
	FF(c, d, a, b, v[10], S13, 0xffff5bb1UL);  /* 11 */
	FF(b, c, d, a, v[11], S14, 0x895cd7beUL);  /* 12 */
	FF(a, b, c, d, v[12], S11, 0x6b901122UL);  /* 13 */
	FF(d, a, b, c, v[13], S12, 0xfd987193UL);  /* 14 */
	FF(c, d, a, b, v[14], S13, 0xa679438eUL);  /* 15 */
	FF(b, c, d, a, v[15], S14, 0x49b40821UL);  /* 16 */
	
	/* round 2 */
	GG(a, b, c, d, v[ 1], S21, 0xf61e2562UL);  /* 17 */
	GG(d, a, b, c, v[ 6], S22, 0xc040b340UL);  /* 18 */
	GG(c, d, a, b, v[11], S23, 0x265e5a51UL);  /* 19 */
	GG(b, c, d, a, v[ 0], S24, 0xe9b6c7aaUL);  /* 20 */
	GG(a, b, c, d, v[ 5], S21, 0xd62f105dUL);  /* 21 */
	GG(d, a, b, c, v[10], S22, 0x02441453UL);  /* 22 */
	GG(c, d, a, b, v[15], S23, 0xd8a1e681UL);  /* 23 */
	GG(b, c, d, a, v[ 4], S24, 0xe7d3fbc8UL);  /* 24 */
	GG(a, b, c, d, v[ 9], S21, 0x21e1cde6UL);  /* 25 */
	GG(d, a, b, c, v[14], S22, 0xc33707d6UL);  /* 26 */
	GG(c, d, a, b, v[ 3], S23, 0xf4d50d87UL);  /* 27 */
	GG(b, c, d, a, v[ 8], S24, 0x455a14edUL);  /* 28 */
	GG(a, b, c, d, v[13], S21, 0xa9e3e905UL);  /* 29 */
	GG(d, a, b, c, v[ 2], S22, 0xfcefa3f8UL);  /* 30 */
	GG(c, d, a, b, v[ 7], S23, 0x676f02d9UL);  /* 31 */
	GG(b, c, d, a, v[12], S24, 0x8d2a4c8aUL);  /* 32 */
	
	/* round 3 */
	HH(a, b, c, d, v[ 5], S31, 0xfffa3942UL);  /* 33 */
	HH(d, a, b, c, v[ 8], S32, 0x8771f681UL);  /* 34 */
	HH(c, d, a, b, v[11], S33, 0x6d9d6122UL);  /* 35 */
	HH(b, c, d, a, v[14], S34, 0xfde5380cUL);  /* 36 */
	HH(a, b, c, d, v[ 1], S31, 0xa4beea44UL);  /* 37 */
	HH(d, a, b, c, v[ 4], S32, 0x4bdecfa9UL);  /* 38 */
	HH(c, d, a, b, v[ 7], S33, 0xf6bb4b60UL);  /* 39 */
	HH(b, c, d, a, v[10], S34, 0xbebfbc70UL);  /* 40 */
	HH(a, b, c, d, v[13], S31, 0x289b7ec6UL);  /* 41 */
	HH(d, a, b, c, v[ 0], S32, 0xeaa127faUL);  /* 42 */
	HH(c, d, a, b, v[ 3], S33, 0xd4ef3085UL);  /* 43 */
	HH(b, c, d, a, v[ 6], S34, 0x04881d05UL);  /* 44 */
	HH(a, b, c, d, v[ 9], S31, 0xd9d4d039UL);  /* 45 */
	HH(d, a, b, c, v[12], S32, 0xe6db99e5UL);  /* 46 */
	HH(c, d, a, b, v[15], S33, 0x1fa27cf8UL);  /* 47 */
	HH(b, c, d, a, v[ 2], S34, 0xc4ac5665UL);  /* 48 */
	
	/* round 4 */
	II(a, b, c, d, v[ 0], S41, 0xf4292244UL);  /* 49 */
	II(d, a, b, c, v[ 7], S42, 0x432aff97UL);  /* 50 */
	II(c, d, a, b, v[14], S43, 0xab9423a7UL);  /* 51 */
	II(b, c, d, a, v[ 5], S44, 0xfc93a039UL);  /* 52 */
	II(a, b, c, d, v[12], S41, 0x655b59c3UL);  /* 53 */
	II(d, a, b, c, v[ 3], S42, 0x8f0ccc92UL);  /* 54 */
	II(c, d, a, b, v[10], S43, 0xffeff47dUL);  /* 55 */
	II(b, c, d, a, v[ 1], S44, 0x85845dd1UL);  /* 56 */
	II(a, b, c, d, v[ 8], S41, 0x6fa87e4fUL);  /* 57 */
	II(d, a, b, c, v[15], S42, 0xfe2ce6e0UL);  /* 58 */
	II(c, d, a, b, v[ 6], S43, 0xa3014314UL);  /* 59 */
	II(b, c, d, a, v[13], S44, 0x4e0811a1UL);  /* 60 */
	II(a, b, c, d, v[ 4], S41, 0xf7537e82UL);  /* 61 */
	II(d, a, b, c, v[11], S42, 0xbd3af235UL);  /* 62 */
	II(c, d, a, b, v[ 2], S43, 0x2ad7d2bbUL);  /* 63 */
	II(b, c, d, a, v[ 9], S44, 0xeb86d391UL);  /* 64 */
	
	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
}

void
md5_update(TMD5ctx* context, const uint8* data, uintxx size)
{
	uintxx rmnng;
	uintxx i;
	ASSERT(context && data);
	
	if (context->rmnng) {
		rmnng = MD5_BLOCKSZ - context->rmnng;
		if (rmnng > size)
			rmnng = size;
		
		for (i = 0; i < rmnng; i++) {
			context->rdata[context->rmnng++] = *data++;
		}
		size -= i;
		
		if (context->rmnng == MD5_BLOCKSZ) {
			md5_compress(context->state, context->rdata);
			
			context->rmnng = 0;
			context->blcks++;
		}
		else {
			return;
		}
	}
	
	while (size >= MD5_BLOCKSZ) {
		md5_compress(context->state, data);
		
		size -= MD5_BLOCKSZ;
		data += MD5_BLOCKSZ;
		context->blcks++;  /* we 'll scale it later */
	}
	
	if (size) {
		for (i = 0; i < size; i++) {
			context->rdata[context->rmnng++] = *data++;
		}
	}
}

void
md5_final(TMD5ctx* context, uint32 digest[8])
{
	uintxx length;
	uintxx tmp;
	uint32 nlo;
	uint32 nhi;
	ASSERT(context && digest);
	
	context->rdata[length = context->rmnng] = 0x80;
	length++;
	
	if (length > MD5_BLOCKSZ - 8) {
		while (length < MD5_BLOCKSZ)
			context->rdata[length++] = 0;
		
		md5_compress(context->state, context->rdata);
		length = 0;
	}
	
	while (length < (MD5_BLOCKSZ - 8))  /* pad with zeros */
		context->rdata[length++] = 0;
	
	/* scales the numbers of bits */
	nhi = context->blcks >> (32 - 9);
	nlo = context->blcks << 9;
	
	/* add the remainings bits */
	tmp = nlo;
	if ((nlo += ((uint32) context->rmnng << 3)) < tmp)
		nhi++;
	
	context->rdata[56] = nlo;
	context->rdata[57] = nlo >> 0x08;
	context->rdata[58] = nlo >> 0x10;
	context->rdata[59] = nlo >> 0x18;
	
	context->rdata[60] = nhi;
	context->rdata[61] = nhi >> 0x08;
	context->rdata[62] = nhi >> 0x10;
	context->rdata[63] = nhi >> 0x18;
	
	md5_compress(context->state, context->rdata);
	digest[0] = ctb_swap32(context->state[0]);
	digest[1] = ctb_swap32(context->state[1]);
	digest[2] = ctb_swap32(context->state[2]);
	digest[3] = ctb_swap32(context->state[3]);
}
