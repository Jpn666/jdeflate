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

#ifndef d23989c4_4a8b_4d24_a0ec_f62492460c10
#define d23989c4_4a8b_4d24_a0ec_f62492460c10

/*
 * str2flt.h
 * String to float conversion.
 */

#include "../ctoolbox.h"


/* flags */
#define	STR2FLT_FPOINT  0x01UL
#define	STR2FLT_FCOMMA  0x02UL
#define	STR2FLT_FANY    (STR2FLT_FPOINT | STR2FLT_FCOMMA)


/*
 * ... */
eintxx str2flt32(const char* src, const char** end, flt32* num, uintxx flags);

/*
 * ... */
eintxx str2flt64(const char* src, const char** end, flt64* num, uintxx flags);


#endif

