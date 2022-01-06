/*
 * Copyright (C) 2016, jpn jpn@gsforce.net
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

#ifndef b2b15d78_058f_4560_a77e_c819b7318ee5
#define b2b15d78_058f_4560_a77e_c819b7318ee5

/*
 * hash.h
 * Hash functions.
 */

#include "ctoolbox.h"


/*
 * */
CTB_INLINE uintxx ctb_hashint(intxx key);

/*
 * */
CTB_INLINE uintxx ctb_hashstr(char* key);


/*
 * Inlines */

CTB_INLINE uintxx
ctb_hashint(intxx key)
{
	uintxx v = key;
	
#if !defined(CTB_ENV64) || (CTB_HAVEINT64 == 0)
	v = v ^ (v >> 16);
	v = v * 0x85ebca6b;
	v = v ^ (v >> 13);
	v = v * 0xc2b2ae35;
	v = v ^ (v >> 16);
#else
	v = v ^ (v >> 33);
	v = v * 0xff51afd7ed558ccd;
	v = v ^ (v >> 33);
	v = v * 0xc4ceb9fe1a85ec53;
	v = v ^ (v >> 33);
#endif
	return v;
}

#if defined(CTB_STRICTALIGNMENT)

CTB_INLINE uintxx
stringhash(void* key)
{
	uint8* c;
	uintxx hash = 5381;
	
	for (c = key; c[0]; c++)
		hash = ((hash << 5) + hash) + c[0];  /* hash * 33 + c */
	return hash;
}

#else

/* murmur hash 2 */
#define MURMURHASH_SEED 0x01000193

CTB_INLINE uintxx
stringhash(char* key)
{
	uintxx len;
	uintxx m;
	uintxx r;
	uintxx h;
	uintxx k;
	uint8* data;
	
	h = MURMURHASH_SEED ^ (len = strlen(key));
	m = 0x5bd1e995;
	r = 24;
	for (data = (void*) key; len >= 4;) {
		k = *((uint32*) data);
		k = k * m;
		k = k ^ (k >> r);
		k = k * m;
		
		h = h * m;
		h = h ^ k;
		data += 4;
		len  -= 4;
	}
	switch(len) {
		case 3: h ^= data[2] << 16;
		case 2: h ^= data[1] << 8;
		case 1: h ^= data[0];
		 	h *= m;
	};
	
	h = h ^ (h >> 13);
	h = h * m;
	h = h ^ (h >> 15);
	return h;
}

#endif

CTB_INLINE uintxx
ctb_hashstr(char* key)
{
	ASSERT(key);
	return stringhash(key);
}


#endif
