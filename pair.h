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

#ifndef ff66f37e_d082_4b69_a861_cecb532dadd5
#define ff66f37e_d082_4b69_a861_cecb532dadd5

/*
 * pair.h
 * Pair (key, value) to be used in maps like ADTs.
 */

#include "ctoolbox.h"


#define PAIRKEY(p) (p).key
#define PAIRVAL(p) (p).val

#define PAIRixx(x) (x).asixx
#define PAIRuxx(x) (x).asuxx
#define PAIRptr(x) (x).asptr
#define PAIRchr(x) (x).aschr


/* Entry types */
typedef enum {
	PTypeStr = 0,
	PTypeInt = 1,
	PTypeExt = 2  /* to be hashed using an external function */
} ePairType;


/* */
struct TPair {
	union TPairKey {
		void*  asptr;
		char*  aschr;
		uintxx asuxx;
		intxx  asixx;
	} key;
	
	union TPairVal {
		void*  asptr;
		char*  aschr;
		uintxx asuxx;
		intxx  asixx;
	} val;
};

typedef union TPairKey TPairKey;
typedef union TPairVal TPairVal;

typedef struct TPair TPair;


#endif

