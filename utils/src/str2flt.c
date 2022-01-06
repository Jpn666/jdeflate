/*
 * Copyright (C) 2015, jpn jpn@gsforce.net
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

#include "../str2flt.h"
#include <float.h>
#include <math.h>


static const flt64 powersof10[] =
{
	  1e+1,
	  1e+2,
	  1e+4,
	  1e+8,
	 1e+16,
	 1e+32,
	 1e+64,
	1e+128
};


#define CHECKFPOINT_POINT(C, F) ((C) == 0x2e && ((F) & STR2FLT_FPOINT))
#define CHECKFPOINT_COMMA(C, F) ((C) == 0x2c && ((F) & STR2FLT_FCOMMA))


#define ISSPACE(N) (((N) >= 0x09 && (N) <= 0x0D) || (N) == 0x20)
#define ISDIGIT(N) (((N) >= 0x30 && (N) <= 0x39))

eintxx
str2flt64(const char* src, const char** end, flt64* num, uintxx flags)
{
	uintxx c;
	uintxx d;
	uintxx negative;
	flt64 v;
	ASSERT(src && num);
	
	v = 0.0f;
	
	while (ISSPACE(src[0]))
		src++;
	
	for (negative = 0; src[0]; src++) {
		switch (src[0]) {
			case 0x2D: negative = 1; continue;
			case 0x2B: negative = 0;
				continue;
		}
		break;
	}
	if (flags == 0)
		flags = STR2FLT_FPOINT;
	
	for (d = 0; (c = src[0]); src++) {
		if (ISDIGIT(c)) {
			if (d) {
				v = v + ((c - 0x30) / (float64) d);
				d = d * 10;
			}
			else {
				v = (v * 10.0) + (c - 0x30);
			}
			continue;
		}
		if (d == 0) {
			if (CHECKFPOINT_POINT(c, flags) || CHECKFPOINT_COMMA(c, flags)) {
				d = 10;
				continue;
			}
		}
		break;
	}
	
	if ((src[0] | 0x20) == 0x65) {  /* have an exponent */
		uintxx e;
		uintxx expnegative; 
		      flt64  exp;
		const flt64* p10;
		
		src++;
		expnegative = 0;
		switch (src[0]) {
			case 0x2D: expnegative = 1;
			case 0x2B:
				src++;
		}
		for (e = 0; (c = src[0]); src++) {
			if (ISDIGIT(c))
				e = (e * 10) + (c - 0x30);
			else
				break;
		}
		
		if (e > DBL_MAX_EXP || (expnegative && (-((intxx) e) < DBL_MIN_EXP))) {
			v = HUGE_VAL;
		}
		else {
			exp = 1.0;
			for (p10 = powersof10; e; e = e >> 1) {
				if (e & 0x01)
					exp = exp * p10[0];
				p10++;
			}
			if (expnegative)
				v = v / exp;
			else
				v = v * exp;
		}
	}
	
	if (negative) {
		v = -v;
	}
	num[0] = v;
	if (end) {
		end[0] = src;
	}
	
	if (v == HUGE_VAL || v == -HUGE_VAL) {
		return CTB_ERANGE;
	}
	return CTB_OK;
}

#undef CHECKFPOINT_POINT
#undef CHECKFPOINT_COMMA


eintxx
str2flt32(const char* src, const char** end, flt32* num, uintxx flags)
{
	eintxx r;
	flt64 n;
	ASSERT(src && num);
	
	r = str2flt64(src, end, &n, flags);
	num[0] = (flt32) n;
	
	if (r || num[0] == HUGE_VAL || num[0] == -HUGE_VAL)
		return CTB_ERANGE;
	return CTB_OK;
}
