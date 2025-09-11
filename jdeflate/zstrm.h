/*
 * Copyright (C) 2025, jpn
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

#include <ctoolbox/ctoolbox.h>
#include "deflator.h"
#include "inflator.h"


/* Stream mode */
typedef enum {
	ZSTRM_RMODE = 0x01,
	ZSTRM_WMODE = 0x02
} eZSTRMMode;


/* Stream type */
typedef enum {
	ZSTRM_DFLT = 0x04,
	ZSTRM_ZLIB = 0x08,
	ZSTRM_GZIP = 0x10,
	ZSTRM_AUTO = ZSTRM_DFLT | ZSTRM_ZLIB | ZSTRM_GZIP
} eZSTRMType;


/* Extra flags */
#define ZSTRM_DOCRC32 0x20
#define ZSTRM_DOADLER 0x30


/* Error codes */
typedef enum {
	ZSTRM_OK             = 0,
	ZSTRM_EIOERROR       = 1,
	ZSTRM_EOOM           = 2,
	ZSTRM_EBADDATA       = 3,
	ZSTRM_ECHECKSUM      = 4,
	ZSTRM_EFORMAT        = 5,
	ZSTRM_EDEFLATE       = 6,
	ZSTRM_EMISSINGDICT   = 7,
	ZSTRM_EINCORRECTDICT = 8,
	ZSTRM_EINCORRECTUSE  = 9
} eZSTRMError;


/* State */
typedef enum {
	ZSTRM_NOTSET   = 0,
	ZSTRM_READY    = 1,
	ZSTRM_NEEDDICT = 2,
	ZSTRM_NORMAL   = 3,
	ZSTRM_END      = 4
} eZSTRMState;


/*
 * IO function prototype.
 * Return value must be the number of bytes written or readed to the buffer
 * (zero if there is no more input avaible or -1 if there is an error). */
typedef intxx (*TZStrmIOFn)(uint8* buffer, uintxx size, void* payload);


/* */
struct TZStrm {
	/* state */
	uintxx state;
	uintxx error;
	uintxx flags;
	uintxx smode;  /* eZSTRMMode */
	uintxx stype;  /* eZSTRMType */
	uintxx mtype;

	/* dictionary id (adler32 checksum) */
	uint32 dictid;
	uintxx dict;

	/* checksums */
	uintxx docrc32;
	uintxx doadler;
	uint32 crc32;
	uint32 adler;

	uintxx level;
	uintxx total;
	uintxx result;

	struct TDeflator* defltr;
	struct TInflator* infltr;

	/* IO callback */
	TZStrmIOFn iofn;

	/* IO callback parameter */
	void* payload;

	/* buffers */
	uint8* source;
	uint8* target;
	uint8* sbgn;
	uint8* send;
	uint8* tbgn;
	uint8* tend;

	/* custom allocator */
	struct TAllocator* allctr;
};

typedef struct TZStrm TZStrm;


/*
 * Creates a new stream. */
TZStrm* zstrm_create(uintxx flags, uintxx level, TAllocator* allctr);

/*
 * Destroys the stream. */
void zstrm_destroy(TZStrm*);

/*
 * Resets the stream state. */
void zstrm_reset(TZStrm*);

/*
 * Sets the IO function. */
void zstrm_setiofn(TZStrm*, TZStrmIOFn fn, void* payload);

/*
 * Sets the dictionary. */
void zstrm_setdctn(TZStrm*, uint8* dict, uintxx size);

/*
 * Flushes the output buffer. If 'final' is true, finishes the stream. */
void zstrm_flush(TZStrm*, bool final);

/*
 * Read data from the stream.
 * Returns the number of bytes read or 0 if there is an error. */
uintxx zstrm_r(TZStrm*,       void* buffer, uintxx size);

/*
 * Write data to the stream.
 * Returns the number of bytes written or 0 if there is an error. */
uintxx zstrm_w(TZStrm*, const void* buffer, uintxx size);

/*
 * Get the current state of the stream. */
uintxx zstrm_getstate(TZStrm*, uintxx* error);

#endif
