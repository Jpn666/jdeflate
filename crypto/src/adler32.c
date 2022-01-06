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

#include "../adler32.h"


#define ADLER_BASE 65521  /* largest prime smaller than 65536 */
#define NMAX       5552


uint32
adler32_update(uint32 adler, const uint8* data, uintxx dsize)
{
	uint32 a;
	uint32 b;
	uintxx i;
	ASSERT(data && dsize);
	
	a = 0xffff & adler;
	b = 0xffff & adler >> 16;
	
	while (dsize >= NMAX) {
		dsize -= NMAX;
		i      = NMAX >> 4;
		do {
			b += (a += *data++); b += (a += *data++);  /*  2 */
			b += (a += *data++); b += (a += *data++);  /*  4 */
			b += (a += *data++); b += (a += *data++);  /*  6 */
			b += (a += *data++); b += (a += *data++);  /*  8 */
			b += (a += *data++); b += (a += *data++);  /* 10 */
			b += (a += *data++); b += (a += *data++);  /* 12 */
			b += (a += *data++); b += (a += *data++);  /* 14 */
			b += (a += *data++); b += (a += *data++);  /* 16 */
		} while (--i);
		a = a % ADLER_BASE;
		b = b % ADLER_BASE;
	}
	
	if (dsize) {
		while (dsize > 16) {
			b += (a += *data++); b += (a += *data++);  /*  2 */
			b += (a += *data++); b += (a += *data++);  /*  4 */
			b += (a += *data++); b += (a += *data++);  /*  6 */
			b += (a += *data++); b += (a += *data++);  /*  8 */
			b += (a += *data++); b += (a += *data++);  /* 10 */
			b += (a += *data++); b += (a += *data++);  /* 12 */
			b += (a += *data++); b += (a += *data++);  /* 14 */
			b += (a += *data++); b += (a += *data++);  /* 16 */
			dsize -= 16;
		}
		while (dsize) {
			b += (a += *data++);
			dsize--;
		}
		a = a % ADLER_BASE;
		b = b % ADLER_BASE;
	}
	
	return (b << 16) | a;
}

