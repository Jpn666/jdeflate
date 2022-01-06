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

#ifndef b32d1608_a8a0_4078_82a3_9f192e84bbab
#define b32d1608_a8a0_4078_82a3_9f192e84bbab

/*
 * adler32.h
 * Adler32 hash.
 */

#include "../ctoolbox.h"


#define ADLER32_INIT(A) ((A) = 1UL)


/*
 * */
CTB_INLINE uint32 adler32_getdigest(const uint8* data, uintxx dsize);

/*
 * */
uint32 adler32_update(uint32 adler, const uint8* data, uintxx dsize);


/*
 * Inlines */

CTB_INLINE uint32
adler32_getdigest(const uint8* data, uintxx dsize)
{
	ASSERT(data && dsize);
	return adler32_update(1L, data, dsize);
}


#endif

