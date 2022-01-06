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

#ifndef f563b1d3_17ba_4b15_9623_10f49e29ccb0
#define f563b1d3_17ba_4b15_9623_10f49e29ccb0

/*
 * sha256.h
 * SHA 256 hash.
 */

#include "../ctoolbox.h"


#define SHA256_BLOCKSZ  64
#define SHA256_DIGESTSZ 32


/* */
struct TSHA256ctx {
	uint32 state[ 8];
	uint8  rdata[64];
	uint32 blcks;
	uintxx rmnng;
};

typedef struct TSHA256ctx TSHA256ctx;


/*
 * Init the TShaxx structure. */
CTB_INLINE void sha256_init(TSHA256ctx*);

/*
 * Convenient way to get the hash. */
CTB_INLINE void sha256_getdigest(uint32 dgst[8], const uint8* data, uintxx sz);

/*
 * Updates the digest state using data chunks. */
void sha256_update(TSHA256ctx*, const uint8* data, uintxx size);

/*
 * Get the resulting hash digest. */
void sha256_final(TSHA256ctx*, uint32 digest[8]);


/*
 * Inlines */

CTB_INLINE void
sha256_init(TSHA256ctx* context)
{
	ASSERT(context);
	context->state[0] = 0x6a09e667UL;
	context->state[1] = 0xbb67ae85UL;
	context->state[2] = 0x3c6ef372UL;
	context->state[3] = 0xa54ff53aUL;
	context->state[4] = 0x510e527fUL;
	context->state[5] = 0x9b05688cUL;
	context->state[6] = 0x1f83d9abUL;
	context->state[7] = 0x5be0cd19UL;

	context->blcks = 0;
	context->rmnng = 0;
}

CTB_INLINE void
sha256_getdigest(uint32 digest[8], const uint8* data, uintxx size)
{
	struct TSHA256ctx sha256;
	ASSERT(data && digest);
	
	sha256_init(&sha256);
	
	sha256_update(&sha256, data, size);
	sha256_final(&sha256, digest);
}


#endif

