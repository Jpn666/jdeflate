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

#include "../base64.h"


#if !defined(BASE64_CFG_IGNORE_WHITESPACE)
#	define BASE64_CFG_IGNORE_WHITESPACE 1
#endif


static const uint8 b64_table[] = {
	0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
	0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50,
	0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
	0x59, 0x5a, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
	0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e,
	0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76,
	0x77, 0x78, 0x79, 0x7a, 0x30, 0x31, 0x32, 0x33,
	0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x2b, 0x2f
};

static const uint8 b64_chindex[] = {
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x40, 0x40, 0x3e, 0x40, 0x40, 0x40, 0x3f,
	0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b,
	0x3c, 0x3d, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 
	0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
	0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
	0x17, 0x18, 0x19, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
	0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
	0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
	0x31, 0x32, 0x33, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40
};

uintxx
base64_encode(const uint8* in, uintxx size, uint8* out)
{
	uint8* tmp;
	ASSERT(in && out);
	
	tmp = (void*) out;
	for (; size >= 3; size -= 3) {
		*out++ = b64_table[((in[0] & 0xFC) >> 2)];
		*out++ = b64_table[((in[1] & 0xF0) >> 4) | ((in[0] & 0x03) << 4)];
		*out++ = b64_table[((in[2] & 0xC0) >> 6) | ((in[1] & 0x0F) << 2)];
		*out++ = b64_table[in[2] & 0x3F];
		in += 3;
	}
	
	/* remaining */
	switch (size) {
		case 1:
			*out++ = b64_table[((in[0] & 0xFC) >> 2)];
			*out++ = b64_table[((in[0] & 0x03) << 4)];
			*out++ = 0x3D;
			*out++ = 0x3D;
			break;
		case 2:
			*out++ = b64_table[((in[0] & 0xFC) >> 2)];
			*out++ = b64_table[((in[1] & 0xF0) >> 4) | ((in[0] & 0x03) << 4)];
			*out++ = b64_table[((in[1] & 0x0F) << 2)];
			*out++ = 0x3D;
	}
	return out - tmp;
}

#define ISSPACE(X) \
	(X) == 0x20 || \
	(X) == 0x09 || \
	(X) == 0x0A || \
	(X) == 0x0B || \
	(X) == 0x0C || (X) == 0x0D

#if BASE64_CFG_IGNORE_WHITESPACE
#	define BASE64_FILTER_CHAR(X) ((X) == 0x3D || ISSPACE(X))
#else
#	define BASE64_FILTER_CHAR(X) ((X) == 0x3D)
#endif


uintxx
base64_decode(const uint8* in, uintxx size, uint8* out)
{
	uint8* tmp;
	uint8  b[4];
	uintxx i;
	ASSERT(in && out);
	
	tmp = out;
	i   = 0;
	while (size) {
		for (i = 0; i < 4 && size; in++) {
			if (BASE64_FILTER_CHAR(in[0])) {
				size--;
				continue;
			}
			/* the RFC4648 recomends to reject the entire encode if a non
			 * alfabet character is found on the stream. */
			if ((b[i++] = b64_chindex[in[0]]) == 0x40)
				return 0;
			size--;
		}
		if (i == 4) {
			*out++ = ((b[0] << 2) | (b[1] >> 4));
			*out++ = ((b[1] << 4) | (b[2] >> 2));
			*out++ = ((b[2] << 6) | (b[3]));
		}
	}
	
	switch (i) {
		case 2:
			*out++ = ((b[0] << 2) | (b[1] >> 4));
			break;
		case 3:
			*out++ = ((b[0] << 2) | (b[1] >> 4));
			*out++ = ((b[1] << 4) | (b[2] >> 2));
	}
	*out = 0x00;
	return out - tmp;
}

uintxx
base64_getdecodesz(const uint8* in, uintxx size)
{
	uintxx i;
	ASSERT(in);
	
	i = 0;
	while (size--) {
		if (!BASE64_FILTER_CHAR(in[0])) {
			i++;
			if (b64_chindex[in[0]] == 0x40)
				return 0;
		}
		in++;
	}
	return (uintxx) (i * (3.0f / 4.0f));
}
