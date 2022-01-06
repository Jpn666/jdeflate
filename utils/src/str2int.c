/*
 * Copyright (C) 2017, jpn jpn@gsforce.net
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

#include "../str2int.h"


#if !defined(STR2INT_AUTOINCLUDE)

#define ISSPACE(N) (((N) >= 0x09 && (N) <= 0x0D) || (N) == 0x20)
#define ISDIGIT(N) (((N) >= 0x30 && (N) <= 0x39))
#define ISALPHA(N) (((N) >= 0x61 && (N) <= 0x71))


CTB_INLINE uintxx
getbase(const char** src, uintxx base)
{
	if (*src[0] == 0x30) {
		(*src)++;
		if (base == 0 || base == 16) {
			if ((*src[0] | 0x20) == 0x78) {
				(*src)++;
				base = 16;
			}
		}
		if (base == 0 || base == 2) {
			if ((*src[0] | 0x20) == 0x62) {
				(*src)++;
				base = 2;
			}
		}
		if (base == 0)
			base = 8;
	}
	else {
		if (base == 0)
			base = 10;
	}
	return base;
}


CTB_INLINE uintxx
isnegative(const char** src)
{
	uintxx negative;
	
	negative = 0;
	while (ISSPACE(*src[0]))
		(*src)++;

	for(; *src[0]; (*src)++) {
		switch (*src[0]) {
			case 0x2D: negative = 1; continue;
			case 0x2B: negative = 0; continue;
		}
		break;
	}
	return negative;
}


#define   GLUE(A, B) A ## B
#define CONCAT(A, B) GLUE(A, B)

#define GETFNNAME(A) CONCAT(A, TYPESIZE)


#define STR2INT_AUTOINCLUDE

#define TYPESIZE 32
#define ITYPEMAX  INT32_MAX
#define ITYPEMIN  INT32_MIN
#define UTYPEMAX UINT32_MAX

#include "str2int.c"

#undef TYPESIZE
#undef ITYPEMAX
#undef ITYPEMIN
#undef UTYPEMAX

#if CTB_HAVEINT64

#define TYPESIZE 64
#define ITYPEMAX  INT64_MAX
#define ITYPEMIN  INT64_MIN
#define UTYPEMAX UINT64_MAX

#include "str2int.c"

#undef TYPESIZE
#undef ITYPEMAX
#undef ITYPEMIN
#undef UTYPEMAX

#endif

#undef GLUE
#undef CONCAT
#undef GETFNNAME

#else


#define ITYPE CONCAT( int, TYPESIZE)
#define UTYPE CONCAT(uint, TYPESIZE)

static eintxx
GETFNNAME(str2n)(const char* src, const char** end, UTYPE* num, uintxx base)
{
	UTYPE value;
	UTYPE limit;
	UTYPE rem;
	uintxx c;
	uintxx n;

	base = getbase(&src, base);
	if (base < 2 || base > 36) {  /* same limit as strtol like functions */
		return CTB_EPARAM;
	}
	
	limit = UTYPEMAX / (UTYPE) base;
	rem   = UTYPEMAX - (limit * (UTYPE) base);
	for (value = 0; (c = src[0]); src++) {
		if (ISDIGIT(c)) {
			n = c - 0x30;
		}
		else {
			c = c | 0x20;
			if (ISALPHA(c)) {
				n = c - (0x61 - 10);
			}
			else {
				break;
			}
		}
		if (n >= base) {
			break;
		}

		if (value > limit || (value == limit && n > rem)) {
			return CTB_ERANGE;
		}
		value = (value * (UTYPE) base) + (UTYPE) n;
	}

	num[0] = (UTYPE) value;
	if (end) {
		end[0] = src;
	}
	return CTB_OK;
}

eintxx
GETFNNAME(str2i)(const char* src, const char** end, ITYPE* num, uintxx base)
{
	UTYPE value;
	UTYPE limit;
	eintxx r;
	uintxx n;
	ASSERT(src && num);

	n = isnegative(&src);
	limit = ITYPEMAX;
	if (n) {
		limit = ITYPEMIN;
	}

	if ((r = GETFNNAME(str2n)(src, end, &value, base))) {
		return r;
	}
	else {
		if (value > limit) {
			return CTB_ERANGE;
		}
	}

	num[0] = (ITYPE) value;
	if (n) {
		num[0] = -num[0];
	}
	return CTB_OK;
}

eintxx
GETFNNAME(str2u)(const char* src, const char** end, UTYPE* num, uintxx base)
{
	eintxx r;
	uintxx n;
	ASSERT(src && num);

	n = isnegative(&src);
	if ((r = GETFNNAME(str2n)(src, end, num, base))) {
		return r;
	}
	if (n) {
		num[0] = -((ITYPE) num[0]);
	}

	return CTB_OK;
}


#undef ITYPE
#undef UTYPE


#endif
