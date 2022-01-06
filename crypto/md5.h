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

#ifndef d2e7bc7d_fd8c_41b2_a1ae_592d5909bf4f
#define d2e7bc7d_fd8c_41b2_a1ae_592d5909bf4f

/*
 * md5.h
 * MD5 hash.
 */

#include "../ctoolbox.h"


#define MD5_BLOCKSZ  64
#define MD5_DIGESTSZ 16


/* */
struct TMD5ctx {
	uint32 state[ 4];
	uint8  rdata[64];
	uint32 blcks;
	uintxx rmnng;
};

typedef struct TMD5ctx TMD5ctx;


/*
 * */
CTB_INLINE void md5_init(TMD5ctx*);

/*
 * */
CTB_INLINE void md5_getdigest(uint32 dgst[4], const uint8* data, uintxx sz);

/*
 * */
void md5_update(TMD5ctx*, const uint8* data, uintxx size);

/*
 * */
void md5_final(TMD5ctx*, uint32 digest[4]);


/*
 * Inlines */

CTB_INLINE void
md5_init(TMD5ctx* context)
{
	ASSERT(context);
	context->state[0] = 0x67452301UL;
	context->state[1] = 0xefcdab89UL;
	context->state[2] = 0x98badcfeUL;
	context->state[3] = 0x10325476UL;

	context->rmnng = 0;
	context->blcks = 0;
}

CTB_INLINE void
md5_getdigest(uint32 digest[4], const uint8* data, uintxx size)
{
	struct TMD5ctx md5;
	ASSERT(data && digest);
	
	md5_init(&md5);
	
	md5_update(&md5, data, size);
	md5_final(&md5, digest);
}

#endif

