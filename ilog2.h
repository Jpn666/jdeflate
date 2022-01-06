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

#ifndef e33aaf67_7e8a_4b4d_aa01_4df26b8ae7eb
#define e33aaf67_7e8a_4b4d_aa01_4df26b8ae7eb

/*
 * ilog2.h
 * Integer log base 2 function.
 */

#include "ctoolbox.h"


#if defined(CTB_ENV64)
#	define CLZ_ILOG2(A) (__builtin_clzll(A) ^ 63)
#else
#	define CLZ_ILOG2(A) (__builtin_clz(A)   ^ 31)
#endif


CTB_INLINE uintxx
ctb_ilog2(uintxx v)
{
#if defined(__GNUC__)
	return CLZ_ILOG2(v);
#else
	uintxx i;
	
	i = 0;
#if defined(CTB_ENV64)
	if (v >= (((uintxx) 1) << 0x20)) { i += 0x20; v >>= 0x20; }
#endif
	
	if (v >= (((uintxx) 1) << 0x10)) { i += 0x10; v >>= 0x10; }
	if (v >= (((uintxx) 1) << 0x08)) { i += 0x08; v >>= 0x08; }
	if (v >= (((uintxx) 1) << 0x04)) { i += 0x04; v >>= 0x04; }
	if (v >= (((uintxx) 1) << 0x02)) { i += 0x02; v >>= 0x02; }
	if (v >= (((uintxx) 1) << 0x01)) { i += 0x01; v >>= 0x01; }
	return i;
#endif
}

#undef CLZ_ILOG2


#endif

