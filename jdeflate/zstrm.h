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
 * Stream oriented deflate and gzip encoder/decoder.
 * 
 * This provides a simple interface for compressing and decompressing data
 * using the DEFLATE algorithm with support for zlib and gzip formats.
 * 
 * The library supports both buffer-based and callback-based I/O, allowing for
 * flexible data handling.
 */

#include <ctoolbox/ctoolbox.h>
#include <ctoolbox/memory.h>
#include "deflator.h"
#include "inflator.h"


/* Stream mode */
typedef enum {
	ZSTRM_INFLATE = 0x00100,
	ZSTRM_DEFLATE = 0x00200
} eZSTRMMode;


/* Stream type */
typedef enum {
	ZSTRM_DFLT = 0x01000,
	ZSTRM_ZLIB = 0x02000,
	ZSTRM_GZIP = 0x04000
} eZSTRMType;


/* Extra flags */
#define ZSTRM_DOCRC   0x10000
#define ZSTRM_DOADLER 0x20000


/* Error codes */
typedef enum {
	ZSTRM_OK            =  0,
	ZSTRM_EIOERROR      =  1,
	ZSTRM_EOOM          =  2,
	ZSTRM_EBADDATA      =  3,
	ZSTRM_ECHECKSUM     =  4,
	ZSTRM_EFORMAT       =  5,
	ZSTRM_EMISSINGDICT  =  6,
	ZSTRM_ESRCEXHSTD    =  7,
	ZSTRM_ETGTEXHSTD    =  8,
	ZSTRM_EDEFLATE      =  9,
	ZSTRM_EBADDICT      = 10,
	ZSTRM_EINCORRECTUSE = 11
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


/* Public state */
struct TZStrmPblc {
	/* state */
	uint32 state;
	uint32 error;
	uint32 flags;
	uint32 smode;  /* eZSTRMMode */
	uint32 stype;  /* eZSTRMType */
	uint32 level;

	/* total number of bytes readed or written */
	uintxx total;

	/* dictionary id (adler32 checksum) */
	uint32 dictid;
	uint32 dict;

	/* checksums */
	uint32 crc;
	uint32 adler;

	/* IO callback */
	TZStrmIOFn iofn;

	/* IO callback parameter */
	void* payload;

	/* source buffer */
	const uint8* source;
	const uint8* send;

	/* number of bytes consumed from the source buffer or IO callback, this
	 * may not reflect the actual number of bytes readed if the source callback
	 * is used, but can be used to determine the end of the stream if you
	 * need to know how much valid data has been processed. */
	uintxx usedinput;
};

typedef const struct TZStrmPblc TZStrm;


/*
 * Creates a new stream. */
TZStrm* zstrm_create(uintxx flags, uintxx level, TAllocator* allctr);

/*
 * Destroys the stream. */
void zstrm_destroy(TZStrm*);

/*
 * Sets the source buffer for the stream input. This can't be used together
 * with the callback function. */
CTB_INLINE void zstrm_setsource(TZStrm*, const uint8* source, uintxx size);

/*
 * Sets the source or target callback function for the stream input or output.
 * The callback function will be called when the stream needs more input
 * data or when the stream has output data available. */
CTB_INLINE void zstrm_setsourcefn(TZStrm*, TZStrmIOFn fn, void* payload);
CTB_INLINE void zstrm_settargetfn(TZStrm*, TZStrmIOFn fn, void* payload);

/*
 * Sets the dictionary for the stream.
 * This function can be used to provide a custom dictionary for the
 * compression or decompression process. */
void zstrm_setdctn(TZStrm*, const uint8* dict, uintxx size);

/*
 * Decompresses n bytes of data into the target buffer. */
uintxx zstrm_inflate(TZStrm*, void* target, uintxx n);

/*
 * Compresses n bytes of data from the source buffer. */
uintxx zstrm_deflate(TZStrm*, void* source, uintxx n);

/*
 * Flushes the output to the stream target buffer or callback function.
 * This function can be used to ensure that all data is written
 * to the output. When final is true the stream is finalized and no more data
 * can be written to it. */
void zstrm_flush(TZStrm*, bool final);

/*
 * Resets the stream to its initial state. */
void zstrm_reset(TZStrm*);


/*
 * Inlines */

CTB_INLINE void
zstrm_setsource(TZStrm* state, const uint8* source, uintxx size)
{
	uint8 t[1];
	struct TZStrmPblc* p;
	CTB_ASSERT(state && source && size);

	p = (struct TZStrmPblc*) state;
	if (p->smode != ZSTRM_INFLATE || p->state) {
		p->state = 4;
		if (p->error == 0)
			p->error = ZSTRM_EINCORRECTUSE;
		return;
	}
	p->state++;
	p->source = source;
	p->send = source + size;

	zstrm_inflate(state, t, 0);
}

CTB_INLINE void
zstrm_setsourcefn(TZStrm* state, TZStrmIOFn fn, void* payload)
{
	uint8 t[1];
	struct TZStrmPblc* p;
	CTB_ASSERT(state && fn);

	p = (struct TZStrmPblc*) state;
	if (p->smode != ZSTRM_INFLATE || p->state) {
		p->state = 4;
		if (p->error == 0)
			p->error = ZSTRM_EINCORRECTUSE;
		return;
	}
	p->state++;
	p->payload = payload;
	p->iofn = fn;

	zstrm_inflate(state, t, 0);
}

CTB_INLINE void
zstrm_settargetfn(TZStrm* state, TZStrmIOFn fn, void* payload)
{
	struct TZStrmPblc* p;
	CTB_ASSERT(state && fn);

	p = (struct TZStrmPblc*) state;
	if (p->smode != ZSTRM_DEFLATE || p->state) {
		p->state = 4;
		if (p->error == 0)
			p->error = ZSTRM_EINCORRECTUSE;
		return;
	}
	p->state++;
	p->payload = payload;
	p->iofn = fn;
}

#endif
