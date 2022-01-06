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

#ifndef c8d1f2c3_73ba_47a9_ba70_72739f45caf5
#define c8d1f2c3_73ba_47a9_ba70_72739f45caf5

/*
 * aes.h
 * AES encryption algorimth.
 */

#include "../ctoolbox.h"


#define AES_BLOCKSZ 16

enum eAESKeyLen {
	AES_KEYLEN256 = 256,
	AES_KEYLEN192 = 192,
	AES_KEYLEN128 = 128
};


/* */
struct TAESctx {
	uint8  state[4][4];
	uint32 swork[60];
	uintxx nrnds;  
};

typedef struct TAESctx TAESctx;


/*
 * ... */
CTB_INLINE void aes_init(TAESctx*);

/* 
 * ... */
bool aes_setupkey(TAESctx*, const uint8* key, uintxx keylen);

/*
 * ... */
void aes_encrypt(TAESctx*, uint8 ptxt[16], uint8 ctxt[16]);

/*
 * ... */
void aes_decrypt(TAESctx*, uint8 ctxt[16], uint8 ptxt[16]);


/*
 * Inlines */

CTB_INLINE void
aes_init(TAESctx* context)
{
	uintxx i;
	ASSERT(context);

	for (i = 0; i < 4; i++) {
		context->state[i][0] = 0;
		context->state[i][1] = 0;
		context->state[i][2] = 0;
		context->state[i][3] = 0;
	}
}


#endif
