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

#ifndef f752343e_e974_48ea_95e7_288826a30056
#define f752343e_e974_48ea_95e7_288826a30056

/*
 * str2int.h
 * String to integer conversion.
 */

#include "../ctoolbox.h"


#if defined(CTB_ENV64) && CTB_HAVEINT64

#define str2uxx str2u64
#define str2ixx str2i64

#else

#define str2uxx str2u32
#define str2ixx str2i32

#endif


/*
 * */
eintxx str2u32(const char* src, const char** end, uint32* n, uintxx base);
eintxx str2i32(const char* src, const char** end,  int32* n, uintxx base);


#if CTB_HAVEINT64

/*
 * */
eintxx str2u64(const char* src, const char** end, uint64* n, uintxx base);
eintxx str2i64(const char* src, const char** end,  int64* n, uintxx base);

#endif

#endif

