/*
 * Copyright (C) 2021, jpn 
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

#ifndef c4af13ff_2e7d_4aa4_9668_435d94f47c61
#define c4af13ff_2e7d_4aa4_9668_435d94f47c61

/*
 * zstrm.h
 * Stream oriented deflate and gzip encoder/decoder. */

#include <ctoolbox.h>


/* Stream type */
typedef enum {
	ZSTRM_AUTO    = 0,
	ZSTRM_DEFLATE = 1,
	ZSTRM_GZIP    = 2
} eZSTRMType;


/* Stream mode */
typedef enum {
	ZSTRM_RMODE = 0,
	ZSTRM_WMODE = 1
} eZSTRMMode;


/* Error codes */
typedef enum {
	ZSTRM_OK        = 0,
	ZSTRM_EIOERROR  = 1,
	ZSTRM_EOOM      = 2,
	ZSTRM_EBADDATA  = 3,
	ZSTRM_ECHECKSUM = 4,
	ZSTRM_EDEFLATE  = 5,
	ZSTRM_EBADUSE   = 6
} eZSTRMError;


#define ZSTRM_BADSTATE 0xDEADBEEF


/* 
 * IO function prototype.
 * Return value must be the number of bytes written or readed to the buffer
 * (zero if there is no more input avaible or -1 if there is an error). */
typedef intxx (*TZStrmIOFn)(uint8* buffer, uintxx size, void* payload);


/* */
struct TZStrm;
typedef struct TZStrm TZStrm;


/*
 * Creates a new stream. If mode is ZSTRM_WMODE then it's posible to set
 * the compression level (0-9), level is ignored if mode is ZSTRM_RMODE */
TZStrm* zstrm_create(eZSTRMMode mode, eZSTRMType strmtype, uintxx level);

/*
 * */
void zstrm_destroy(TZStrm*);

/*
 * */
void zstrm_reset(TZStrm*);

/*
 * */
void zstrm_setiofn(TZStrm*, TZStrmIOFn fn, void* payload);

/*
 * */
uintxx zstrm_r(TZStrm*, uint8* buffer, uintxx size);

/*
 * */
uintxx zstrm_w(TZStrm*, uint8* buffer, uintxx size);

/*
 *  */
bool zstrm_flush(TZStrm*);

/*
 * */
bool zstrm_endstream(TZStrm*);

/*
 * */
bool zstrm_eof(TZStrm*);

/*
 * */
eZSTRMError zstrm_geterror(TZStrm*);


#endif
