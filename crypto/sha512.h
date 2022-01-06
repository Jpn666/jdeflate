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

#ifndef cd861e18_7b51_4b12_96eb_9f71fa7cb0f0
#define cd861e18_7b51_4b12_96eb_9f71fa7cb0f0

/*
 * sha512.h
 * SHA 512 hash.
 */

#include "../ctoolbox.h"


#if !CTB_HAVEINT64
#	error "Your compiler does not seems to support 64 bit integers"
#endif


#define SHA512_BLOCKSZ  128
#define SHA512_DIGESTSZ 64


/* */
struct TSHA512ctx {
	uint64 state[  8];
	uint8  rdata[128];
	uint64 blcks;
	uintxx rmnng;
};

typedef struct TSHA512ctx TSHA512ctx;


/*
 * Init the TShaxx structure. */
CTB_INLINE void sha512_init(TSHA512ctx*);

/*
 * Convenient way to get the hash. */
CTB_INLINE void sha512_getdigest(uint64 dgst[8], const uint8* data, uintxx sz);

/*
 * Updates the digest state using data chunks. */
void sha512_update(TSHA512ctx*, const uint8* data, uintxx size);

/*
 * Get the resulting hash digest. */
void sha512_final(TSHA512ctx*, uint64 digest[8]);


/*
 * Inlines */

CTB_INLINE void
sha512_init(TSHA512ctx* context)
{
	ASSERT(context);
	context->state[0] = 0x6a09e667f3bcc908ULL;
	context->state[1] = 0xbb67ae8584caa73bULL;
	context->state[2] = 0x3c6ef372fe94f82bULL;
	context->state[3] = 0xa54ff53a5f1d36f1ULL;
	context->state[4] = 0x510e527fade682d1ULL;
	context->state[5] = 0x9b05688c2b3e6c1fULL;
	context->state[6] = 0x1f83d9abfb41bd6bULL;
	context->state[7] = 0x5be0cd19137e2179ULL;

	context->blcks = 0;
	context->rmnng = 0;
}

CTB_INLINE void
sha512_getdigest(uint64 digest[8], const uint8* data, uintxx size)
{
	struct TSHA512ctx sha512;
	ASSERT(data && digest);
	
	sha512_init(&sha512);
	
	sha512_update(&sha512, data, size);
	sha512_final(&sha512, digest);
}


#endif

