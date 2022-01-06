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

#ifndef e97c8141_af1c_4db0_9dc6_a9bbff10199b
#define e97c8141_af1c_4db0_9dc6_a9bbff10199b

/*
 * sha384.h
 * SHA 384 hash.
 */

#include "../ctoolbox.h"


#define SHA384_BLOCKSZ  SHA512_BLOCKSZ
#define SHA384_DIGESTSZ 48


typedef struct TSHA512ctx TSHA384ctx;


/*
 * Init the TShaxx struture. */
CTB_INLINE void sha384_init(TSHA384ctx*);

/*
 * Convenient way to get the hash. */
CTB_INLINE void sha384_getdigest(uint64 dgst[6], const uint8* data, uintxx sz);

/*
 * Updates the digest state using data chunks. */
CTB_INLINE void sha384_update(TSHA384ctx*, const uint8* data, uintxx size);

/*
 * Get the resulting hash digest. */
CTB_INLINE void sha384_final(TSHA384ctx*, uint64 digest[6]);


/*
 * Inlines */

CTB_INLINE void
sha384_init(TSHA384ctx* context)
{
	ASSERT(context);
	context->state[0] = 0xcbbb9d5dc1059ed8LL;
	context->state[1] = 0x629a292a367cd507LL;
	context->state[2] = 0x9159015a3070dd17LL;
	context->state[3] = 0x152fecd8f70e5939LL;
	context->state[4] = 0x67332667ffc00b31LL;
	context->state[5] = 0x8eb44a8768581511LL;
	context->state[6] = 0xdb0c2e0d64f98fa7LL;
	context->state[7] = 0x47b5481dbefa4fa4LL;

	context->blcks = 0;
	context->rmnng = 0;
}

CTB_INLINE void
sha384_getdigest(uint64 digest[6], const uint8* data, uintxx size)
{
	struct TSHA512ctx context;
	ASSERT(data && digest);
	
	sha384_init(&context);
	sha384_update(&context, data, size);
	
	sha384_final(&context, digest);
}

CTB_INLINE void
sha384_update(TSHA384ctx* context, const uint8* data, uintxx size)
{
	ASSERT(context && data);
	sha512_update(context, data, size);
}

CTB_INLINE void
sha384_final(TSHA384ctx* context, uint64 digest[6])
{
	uint64 result[8];
	ASSERT(context && digest);
	
	sha512_final(context, result);
	digest[0] = result[0];
	digest[1] = result[1];
	digest[2] = result[2];
	digest[3] = result[3];
	digest[4] = result[4];
	digest[5] = result[5];
}


#endif

