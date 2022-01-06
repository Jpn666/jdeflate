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

#ifndef e0fea415_8395_4e1d_8f72_ad5cbd3d4e99
#define e0fea415_8395_4e1d_8f72_ad5cbd3d4e99

/*
 * base32.h
 * Standard base 32 encoding/decoding (rfc4648).
 */

#include "../ctoolbox.h"


/* flags */
/* #define BASE32_CFG_IGNORE_WHITESPACE 1 */


/*
 * Transform the input to base32 encoded format, returns the output len. */
uintxx base32_encode(const uint8* in, uintxx size, uint8* out);

/*
 * Decodes the input (base32 encoded) to binary format, returns the output len
 * or zero if there was an error. */
uintxx base32_decode(const uint8* in, uintxx size, uint8* out);

/*
 * Returns the numbers of bytes resulting of decoding the input, zero on
 * error. */
uintxx base32_getdecodesz(const uint8* in, uintxx size);

/*
 * Returns the numbers of bytes resulting of encoding n numbers of bytes to
 * base32. */
CTB_INLINE uintxx base32_getencodesz(uintxx size);


/*
 * Inlines */

CTB_INLINE
uintxx base32_getencodesz(uintxx size)
{
	size = (size * 8) / 5;
	if (size & 7)
		return size + (8 - (size & 7));
	return size;
}


#endif

