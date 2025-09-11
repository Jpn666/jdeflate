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

#ifndef d47fa962_664e_4fc0_bd71_1e32b979e58e
#define d47fa962_664e_4fc0_bd71_1e32b979e58e

/*
 * inflator.h
 * Stream oriented deflate decoder.
 *
 * Usage:
 * do {
 *     if (...no more input)
 *         final = 1;
 *
 *     ...read a chunk of compressed data into source buffer
 *     inflator_setsrc(state, source, sourcesize);
 *     do {
 *         inflator_settgt(state, target, targetsize);
 *         result = inflator_inflate(state, final);
 *
 *         ...target to inflator_tgtend(state) is the uncompressed data
 *     } while (result == INFLT_TGTEXHSTD);
 * } while (result == INFLT_SRCEXHSTD);
 *
 * ...check for errors
 */

#include <ctoolbox/ctoolbox.h>
#include <ctoolbox/memory.h>
#include <jdeflateconfig.h>


/* Return codes for inflate call */
typedef enum {
	INFLT_OK        = 0,
	INFLT_SRCEXHSTD = 1,
	INFLT_TGTEXHSTD = 2,
	INFLT_ERROR     = 3
} eINFLTResult;


/* Error codes */
typedef enum {
	INFLT_EBADSTATE  = 1,
	INFLT_EBADCODE   = 2,
	INFLT_EBADTREE   = 3,
	INFLT_EFAROFFSET = 4,
	INFLT_EBADBLOCK  = 5,
	INFLT_EINPUTEND  = 6,
	INFLT_EOOM       = 7,
	INFLT_EINCORRECTUSE = 8
} eINFLTError;


/* Public struct */
struct TInflator {
	/* state */
	uintxx state;
	uintxx error;
	uintxx flags;
	uintxx finalinput;

	/* stream buffers */
	uint8* source;
	uint8* sbgn;
	uint8* send;

	uint8* target;
	uint8* tbgn;
	uint8* tend;
};

typedef struct TInflator TInflator;


/*
 * Create an inflator instance. If allctr is NULL, the default allocator is
 * used. */
TInflator* inflator_create(uintxx flags, TAllocator* allctr);

/*
 * Destroy the inflator instance. */
void inflator_destroy(TInflator*);

/*
 * Set the source buffer for the inflator. */
CTB_INLINE void inflator_setsrc(TInflator*, uint8* source, uintxx size);

/*
 * Set the target buffer for the inflator. */
CTB_INLINE void inflator_settgt(TInflator*, uint8* target, uintxx size);

/*
 * Get the number of bytes read from the source buffer. */
CTB_INLINE uintxx inflator_srcend(TInflator*);

/*
 * Get the number of bytes written to the target buffer. */
CTB_INLINE uintxx inflator_tgtend(TInflator*);

/*
 * Perform inflation. If final is non-zero, it indicates that this is the last
 * chunk of input data. */
eINFLTResult inflator_inflate(TInflator*, uintxx final);

/*
 * Set the dictionary for the inflator. */
void inflator_setdctnr(TInflator*, uint8* dict, uintxx size);

/*
 * Reset the inflator to its initial state. */
void inflator_reset(TInflator*);


/*
 * Inlines */

CTB_INLINE void
inflator_setsrc(TInflator* state, uint8* source, uintxx size)
{
	CTB_ASSERT(state);

	if (CTB_EXPECT0(state->finalinput)) {
		if (state->error == 0) {
			state->error = INFLT_EINCORRECTUSE;
			state->state = 0xDEADBEEF;
		}
		return;
	}

	state->sbgn = state->source = source;
	state->send = source + size;
}

CTB_INLINE void
inflator_settgt(TInflator* state, uint8* target, uintxx size)
{
	CTB_ASSERT(state);

	state->tbgn = state->target = target;
	state->tend = target + size;
}

CTB_INLINE uintxx
inflator_srcend(TInflator* state)
{
	CTB_ASSERT(state);

	return (uintxx) (state->source - state->sbgn);
}

CTB_INLINE uintxx
inflator_tgtend(TInflator* state)
{
	CTB_ASSERT(state);

	return (uintxx) (state->target - state->tbgn);
}

#endif
