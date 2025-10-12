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
 * IO function prototypes.
 * Return value must be the number of bytes read or written to the buffer
 * (zero if there is no more input available or -1 if there is an error). */

/* Input function */
typedef intxx (*TZStrmIFn)(      uint8* buffer, uintxx size, void* user);

/* Output function */
typedef intxx (*TZStrmOFn)(const uint8* buffer, uintxx size, void* user);


/* Public state */
struct TZStrm {
	/* state */
	uint32 state;
	uint32 error;
	uint32 flags;
	uint32 smode;  /* eZSTRMMode */
	uint32 stype;  /* eZSTRMType */
	 int32 level;

	/* total number of bytes readed or written */
	uintxx total;

	/* dictionary id (adler32 checksum) */
	uint32 dictid;
	uint32 dict;

	/* checksums */
	uint32 crc;
	uint32 adler;

	/* number of bytes consumed from the source buffer or IO callback, this
	 * may not reflect the actual number of bytes readed if the source callback
	 * is used, but can be used to determine the end of the stream if you
	 * need to know how much valid data has been processed. */
	uintxx usedinput;
};

typedef struct TZStrm TZStrm;


/*
 * Creates a new stream. */
JDEFLATE_API
const TZStrm* zstrm_create(uintxx flags, intxx level, const TAllocator*);

/*
 * Destroys the stream. */
JDEFLATE_API
void zstrm_destroy(const TZStrm*);

/*
 * Sets the source buffer for the stream input. This can't be used together
 * with the callback function. */
JDEFLATE_API
void zstrm_setsource(const TZStrm*, const uint8* source, uintxx size);

/*
 * Sets the source or target callback function for the stream input or output.
 * The callback function will be called when the stream needs more input
 * data or when the stream has output data available. */
JDEFLATE_API
void zstrm_setsourcefn(const TZStrm*, TZStrmIFn fn, void* user);

JDEFLATE_API
void zstrm_settargetfn(const TZStrm*, TZStrmOFn fn, void* user);

/*
 * Sets the dictionary for the stream.
 * This function can be used to provide a custom dictionary for the
 * compression or decompression process. */
JDEFLATE_API
void zstrm_setdctn(const TZStrm*, const uint8* dict, uintxx size);

/*
 * Decompresses up to n bytes of data into the target buffer.
 * Returns the number of bytes written to the target buffer. */
JDEFLATE_API
uintxx zstrm_inflate(const TZStrm*, void* target, uintxx n);

/*
 * Compresses n bytes of data from the source buffer.
 *
 * Returns the number of bytes written. This function will always return the
 * same number of input bytes (n) unless there is an error in the output
 * callback function. */
JDEFLATE_API
uintxx zstrm_deflate(const TZStrm*, const void* source, uintxx n);

/*
 * Flushes the output to the stream output callback function.
 *
 * This function can be used to ensure that all data is written
 * to the output. When final is true the stream is finalized and no more data
 * can be written to it. */
JDEFLATE_API
void zstrm_flush(const TZStrm*, uint32 final);

/*
 * Resets the stream to its initial state. */
JDEFLATE_API
void zstrm_reset(const TZStrm*);


/* ***************************************************************************
 * These functions are not related to compression but are exported
 * anyway because they might be useful in applications using the compression
 * library, for example to compute checksums of files. 
 *************************************************************************** */

/*
 * This function combines two CRC32 checksums into one, as if the two
 * corresponding data blocks were concatenated. */
JDEFLATE_API
uint32 zstrm_crc32combine(uint32 crc1, uint32 crc2, uintxx size2);

/*
 * Updates a CRC32 checksum with new data. */
JDEFLATE_API
uint32 zstrm_crc32update(uint32 chcksm, const uint8* source, uintxx size);

/*
 * Updates an Adler32 checksum with new data. */
JDEFLATE_API
uint32 zstrm_adler32update(uint32 chcksm, const uint8* source, uintxx size);


#endif
