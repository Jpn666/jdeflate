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

#ifndef efc538ca_b0f9_4fab_afcf_c5e87438b898
#define efc538ca_b0f9_4fab_afcf_c5e87438b898

/*
 * sha224.h
 * SHA 224 hash.
 */

#include "../ctoolbox.h"


#define SHA224_BLOCKSZ  SHA256_BLOCKSZ
#define SHA224_DIGESTSZ 28


typedef struct TSHA256ctx TSHA224ctx;


/*
 * Init the TShaxx structure. */
CTB_INLINE void sha224_init(TSHA224ctx*);

/*
 * Convenient way to get the hash. */
CTB_INLINE void sha224_getdigest(uint32 dgst[7], const uint8* data, uintxx sz);

/*
 * Updates the digest state using data chunks. */
CTB_INLINE void sha224_update(TSHA224ctx*, const uint8* data, uintxx size);

/*
 * Get the resulting hash digest. */
CTB_INLINE void sha224_final(TSHA224ctx*, uint32 digest[7]);


/*
 * Inlines */

CTB_INLINE void
sha224_init(TSHA224ctx* context)
{
	ASSERT(context);
	context->state[0] = 0xc1059ed8UL;
	context->state[1] = 0x367cd507UL;
	context->state[2] = 0x3070dd17UL;
	context->state[3] = 0xf70e5939UL;
	context->state[4] = 0xffc00b31UL;
	context->state[5] = 0x68581511UL;
	context->state[6] = 0x64f98fa7UL;
	context->state[7] = 0xbefa4fa4UL;

	context->blcks = 0;
	context->rmnng = 0;
}

CTB_INLINE void
sha224_getdigest(uint32 digest[7], const uint8* data, uintxx size)
{
	struct TSHA256ctx context;
	ASSERT(data && digest);
	
	sha224_init(&context);
	sha224_update(&context, data, size);
	
	sha224_final(&context, digest);
}

CTB_INLINE void
sha224_update(TSHA224ctx* context, const uint8* data, uintxx size)
{
	ASSERT(context && data);
	sha256_update(context, data, size);
}

CTB_INLINE void
sha224_final(TSHA224ctx* context, uint32 digest[7])
{
	uint32 result[8];
	ASSERT(context && digest);
	
	sha256_final(context, result);
	digest[0] = result[0];
	digest[1] = result[1];
	digest[2] = result[2];
	digest[3] = result[3];
	digest[4] = result[4];
	digest[5] = result[5];
	digest[6] = result[6];
}


#endif

