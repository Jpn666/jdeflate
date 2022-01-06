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

#ifndef ea5d3323_0d01_4d31_a288_0e8bc22f7843
#define ea5d3323_0d01_4d31_a288_0e8bc22f7843

/*
 * sha1.h
 * SHA 1 hash.
 */

#include "../ctoolbox.h"


#define SHA1_BLOCKSZ  64
#define SHA1_DIGESTSZ 20


/* */
struct TSHA1ctx {
	uint32 state[ 5];
	uint8  rdata[64];
	uint32 blcks;
	uintxx rmnng;
};

typedef struct TSHA1ctx TSHA1ctx;


/*
 * Init the TSha1 structure. */
CTB_INLINE void sha1_init(TSHA1ctx*);

/*
 * Convenient way to get the hash. */
CTB_INLINE void sha1_getdigest(uint32 dgst[5], const uint8* data, uintxx sz);

/*
 * Updates the hash using data chunks. */
void sha1_update(TSHA1ctx*, const uint8* data, uintxx size);

/*
 * Get the resulting hash digest. */
void sha1_final(TSHA1ctx*, uint32 digest[5]);


/*
 * Inlines */

CTB_INLINE void
sha1_init(TSHA1ctx* context)
{
	ASSERT(context);
	context->state[0] = 0x67452301UL;
	context->state[1] = 0xefcdab89UL;
	context->state[2] = 0x98badcfeUL;
	context->state[3] = 0x10325476UL;
	context->state[4] = 0xc3d2e1f0UL;

	context->rmnng = 0;
	context->blcks = 0;
}

CTB_INLINE void
sha1_getdigest(uint32 digest[5], const uint8* data, uintxx size)
{
	struct TSHA1ctx sha1;
	ASSERT(data && digest);
	
	sha1_init(&sha1);
	sha1_update(&sha1, data, size);
	
	sha1_final(&sha1, digest);
}


#endif
