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

#ifndef f373f456_d150_46f0_92e7_2bda6949bcb2
#define f373f456_d150_46f0_92e7_2bda6949bcb2

/*
 * base64.h
 * Standard base 64 encoding/decoding (rfc4648).
 */

#include "../ctoolbox.h"


/* flags */
/* #define BASE64_CFG_IGNORE_WHITESPACE 1 */


/*
 * Transform the input to base64 encoded format, returns the output len. */
uintxx base64_encode(const uint8* in, uintxx size, uint8* out);

/*
 * Decodes the input (base64 encoded) to binary format, returns the output len
 * or zero if there was an error. */
uintxx base64_decode(const uint8* in, uintxx size, uint8* out);

/*
 * Returns the numbers of bytes resulting of decoding the input, zero on
 * error. */
uintxx base64_getdecodesz(const uint8* in, uintxx size);

/*
 * Returns the numbers of bytes resulting of encoding n numbers of bytes to
 * base64. */
CTB_INLINE uintxx base64_getencodesz(uintxx size);


/*
 * Inlines */

CTB_INLINE
uintxx base64_getencodesz(uintxx size)
{
	size = (size * 4) / 3;
	if (size & 3)
		return size + (4 - (size & 3));
	return size;
}


#endif

