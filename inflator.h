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

#include <ctoolbox.h>


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
	INFLT_EBADUSE    = 8
} eINFLTError;


#define INFLT_BADSTATE 0xDEADBEEF


/* Public struct */
struct TInflator {
	/* state */
	uintxx state;
	uintxx finalinput;
	uintxx error;

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
 * */
TInflator* inflator_create(void);

/*
 * */
void inflator_destroy(TInflator*);

/*
 * */
CTB_INLINE void inflator_setsrc(TInflator*, uint8* source, uintxx size);

/*
 * */
CTB_INLINE void inflator_settgt(TInflator*, uint8* target, uintxx size);

/*
 * */
CTB_INLINE uintxx inflator_srcend(TInflator*);

/*
 * */
CTB_INLINE uintxx inflator_tgtend(TInflator*);

/*
 * */
CTB_INLINE eINFLTError inflator_geterror(TInflator*);

/*
 * */
eINFLTResult inflator_inflate(TInflator*, uintxx final);

/*
 * */
void inflator_setdctnr(TInflator*, uint8* dict, uintxx size);

/*
 * */
void inflator_reset(TInflator*);


/*
 * Inlines */

CTB_INLINE void
inflator_setsrc(TInflator* state, uint8* source, uintxx size)
{
	ASSERT(state);
	
	if (UNLIKELY(state->finalinput)) {
		if (state->error == 0) {
			state->error = INFLT_EBADUSE;
			state->state = INFLT_EBADSTATE;
		}
		return;
	}

	state->sbgn = state->source = source;
	state->send = source + size;
}

CTB_INLINE void
inflator_settgt(TInflator* state, uint8* target, uintxx size)
{
	ASSERT(state);

	state->tbgn = state->target = target;
	state->tend = target + size;
}

CTB_INLINE uintxx
inflator_srcend(TInflator* state)
{
	ASSERT(state);

	return (uintxx) (state->source - state->sbgn);
}

CTB_INLINE uintxx
inflator_tgtend(TInflator* state)
{
	ASSERT(state);

	return (uintxx) (state->target - state->tbgn);
}

CTB_INLINE eINFLTError
inflator_geterror(TInflator* state)
{
	ASSERT(state);
	
	return state->error;
}

#endif
