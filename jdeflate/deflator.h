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

#ifndef acf80c9b_c996_4de9_a00d_e9bcdaa45240
#define acf80c9b_c996_4de9_a00d_e9bcdaa45240

/*
 * deflator.h
 * Stream oriented deflate encoder.
 *
 * Usage:
 * do {
 *     ...read a chunk of data into source buffer
  *     if (...no more input)
 *         final = 1;
 *     deflator_setsrc(state, source, sourcesize);
 *     do {
 *         deflator_settgt(state, target, targetsize);
 *         result = deflator_deflate(state, final);
 *
 *         ...target to deflator_tgtend(state) is the compressed data
 *     } while (result == DEFLT_TGTEXHSTD);
 * } while (result == DEFLT_SRCEXHSTD);
 *
 * ...check for errors
 *
 */

#include <ctoolbox/ctoolbox.h>
#include <ctoolbox/memory.h>
#include <jdeflateconfig.h>


/* Return codes for deflate call */
typedef enum {
	DEFLT_OK        = 0,
	DEFLT_SRCEXHSTD = 1,
	DEFLT_TGTEXHSTD = 2,
	DEFLT_ERROR     = 3
} eDEFLTResult;


/* Flush modes */
typedef enum {
	DEFLT_NOFLUSH = 0,
	DEFLT_END     = 1,
	DEFLT_FLUSH   = 2
} eDEFLTFlush;


/* Error codes */
typedef enum {
	DEFLT_EBADSTATE = 1,
	DEFLT_EOOM      = 2,
	DEFLT_ELEVEL    = 3,
	DEFLT_EINCORRECTUSE = 4
} eDEFLTError;


/* Flags */
typedef enum {
	DEFLT_FIXEDCODES = 0x10
} eDEFLTFlags;


/* Public struct */
struct TDeflator {
	/* state */
	const uint32 state;
	const uint32 error;
	const uint32 flags;
	const uint32 flush;

	/* last result from deflate call */
	const uint32 status;

	/* stream buffers */
	const uint8* source;
	const uint8* sbgn;
	const uint8* send;

	uint8* target;
	uint8* tbgn;
	uint8* tend;
};

typedef struct TDeflator TDeflator;


/*
 * Create a deflator instance with the specified compression level. */
TDeflator* deflator_create(uintxx flags, intxx level, const TAllocator*);

/*
 * Destroy a deflator instance. */
void deflator_destroy(TDeflator*);

/*
 * Set the source buffer and size for the deflator instance. */
CTB_INLINE void deflator_setsrc(TDeflator*, const uint8* source, uintxx size);

/*
 * Set the target buffer and size for the deflator instance. */
CTB_INLINE void deflator_settgt(TDeflator*, uint8* target, uintxx size);

/*
 * Get the number of bytes processed in the source buffer. */
CTB_INLINE uintxx deflator_srcend(TDeflator*);

/*
 * Get the number of bytes written to the target buffer. */
CTB_INLINE uintxx deflator_tgtend(TDeflator*);

/*
 * Compress data using the deflator instance. */
eDEFLTResult deflator_deflate(TDeflator*, eDEFLTFlush flush);

/*
 * Set the dictionary for the deflator instance. */
void deflator_setdctnr(TDeflator*, const uint8* dict, uintxx size);

/*
 * Reset the deflator instance to its initial state. */
void deflator_reset(TDeflator*);


/*
 * Inlines */

CTB_INLINE void
deflator_setsrc(TDeflator* state, const uint8* source, uintxx size)
{
	CTB_ASSERT(state && source && size);

	if (CTB_EXPECT0(state->flush)) {
		if (state->error == 0) {
			struct TNonConstInflator {
				uint32 state;
				uint32 error;
			}* p;

			p = (struct TNonConstInflator*) state;
			p->error = DEFLT_EINCORRECTUSE;
			p->state = 0xDEADBEEF;
		}
		return;
	}

	state->source = state->sbgn = state->send = source;
	state->send  += size;
}

CTB_INLINE void
deflator_settgt(TDeflator* state, uint8* target, uintxx size)
{
	CTB_ASSERT(state && target && size);

	state->target = state->tbgn = state->tend = target;
	state->tend  += size;
}

CTB_INLINE uintxx
deflator_srcend(TDeflator* state)
{
	CTB_ASSERT(state);
	return (uintxx) (state->source - state->sbgn);
}

CTB_INLINE uintxx
deflator_tgtend(TDeflator* state)
{
	CTB_ASSERT(state);
	return (uintxx) (state->target - state->tbgn);
}


#endif
