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

#ifndef fee43bef_bb4d_4550_b044_fb3f533ab30d
#define fee43bef_bb4d_4550_b044_fb3f533ab30d

/*
 * tiger.h
 * Tiger hash. Based on the reference implemetation
 * http://www.cs.technion.ac.il/~biham/Reports/Tiger/
 */

#include "../ctoolbox.h"


#if !CTB_HAVEINT64
#	error "Your compiler does not seems to support 64 bit integers"
#endif


#define TIGER_PASSES    3
#define TIGER_BLOCKSZ  64
#define TIGER_DIGESTSZ 24


/*
 * Get the tiger hash value. */
void tiger_getdigest(uint64 digest[3], const uint8* data, uintxx size);

/*
 * ... */
void tiger_createtable(uint64 table[1024]);


#endif

