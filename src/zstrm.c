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

#include "../zstrm.h"
#include "../deflator.h"
#include "../inflator.h"
#include <crypto/crc32.h>


#define ZIOBFFRSZ 8192


/* */
struct TZStrm {
	/* state */
	uintxx state;
	uintxx stype;  /* stream type */
	uintxx smode;  /* autodetected type of stream */
	uintxx error;
	uint32 crc32;
	uint32 fsize;
	uintxx level;  /* deflator level */
	uintxx aux1;
	
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
};


TZStrm*
zstrm_create(eZSTRMMode mode, eZSTRMType strmtype, uintxx level)
{
	struct TZStrm* state;
	
	if (mode == ZSTRM_WMODE) {
		if (level > 9) {
			/* invalid compression level */
			return NULL;
		}
	}
	else {
		if (mode != ZSTRM_RMODE) {
			return NULL;
		}
	}

	if (strmtype != ZSTRM_GZIP && strmtype != ZSTRM_DEFLATE) {
		if (strmtype == ZSTRM_AUTO) {
			if (mode != ZSTRM_RMODE) {
				return NULL;
			}
		}
		else {
			return NULL;
		}
	}
	
	state = CTB_MALLOC(sizeof(struct TZStrm));
	if (state == NULL) {
		return NULL;
	}
	state->sbgn = CTB_MALLOC(ZIOBFFRSZ);
	state->tbgn = CTB_MALLOC(ZIOBFFRSZ);
	if (state->sbgn == NULL || state->tbgn == NULL) {
		CTB_FREE(state->sbgn);
		CTB_FREE(state->tbgn);
		CTB_FREE(state);
		return NULL;
	}
	state->infltr = NULL;
	state->defltr = NULL;
	
	if (mode == ZSTRM_RMODE) {
		state->infltr = inflator_create();
		if (state->infltr == NULL) {
			zstrm_destroy(state);
			return NULL;
		}
	}
	else {
		state->defltr = deflator_create((state->level = level));
		if (state->defltr == NULL) {
			zstrm_destroy(state);
			return NULL;
		}
	}
	
	state->stype = strmtype;
	zstrm_reset(state);
	return state;
}

void
zstrm_destroy(TZStrm* state)
{
	if (state == NULL) {
		return;
	}
	
	CTB_FREE(state->sbgn);
	CTB_FREE(state->tbgn);

	if (state->infltr)
		inflator_destroy(state->infltr);
	if (state->defltr)
		deflator_destroy(state->defltr);
	
	CTB_FREE(state);
}

void
zstrm_reset(TZStrm* state)
{
	ASSERT(state);
	
	state->state = 0;
	state->error = 0;
	state->fsize = 0;
	state->aux1  = 0;
	state->smode = state->stype;
	
	state->iofn    = NULL;
	state->payload = NULL;

	state->source = state->sbgn;
	state->send   = state->sbgn;
	state->target = state->tbgn;
	state->tend   = state->tbgn;
	if (state->infltr) {
		inflator_reset(state->infltr);
	}
	if (state->defltr) {
		state->send += ZIOBFFRSZ;
		state->tend += ZIOBFFRSZ;
		deflator_reset(state->defltr, state->level);
	}
	
	CRC32_INIT(state->crc32);
}

eZSTRMError
zstrm_geterror(TZStrm* state)
{
	ASSERT(state);

	return state->error;
}


#define SETERROR(ERROR) (state->error = (ERROR))
#define SETSTATE(STATE) (state->state = (STATE))

void
zstrm_setiofn(TZStrm* state, TZStrmIOFn fn, void* payload)
{
	ASSERT(state);
	
	if (state->state) {
		SETSTATE(ZSTRM_BADSTATE);
		if (state->error == 0) {
			SETERROR(ZSTRM_EBADUSE);
		}
	}
	state->iofn = fn;
	state->payload = payload;
}

CTB_INLINE uint8
fetchbyte(struct TZStrm* state)
{
	intxx r;
	
	if (LIKELY(state->source < state->send)) {
		return *state->source++;
	}
	r = state->iofn(state->sbgn, ZIOBFFRSZ, state->payload);
	if (LIKELY(r)) {
		if ((uintxx) r > ZIOBFFRSZ) {
			SETERROR(ZSTRM_EIOERROR);
			return 0;
		}
		state->source = state->sbgn;
		state->send   = state->sbgn + r;
		return *state->source++;
	}
	
	SETERROR(ZSTRM_EBADDATA);
	return 0;
}

static bool
readheader(struct TZStrm* state)
{
	uint8 id1;
	uint8 id2;
	uint8 flags;
	
	id1 = fetchbyte(state);
	id2 = fetchbyte(state);
	if (id1 != 0x1f || id2 != 0x8b) {
		SETERROR(ZSTRM_EBADDATA);
		return 0;
	}

	/* compression method (deflate only) */
	if (fetchbyte(state) != 0x08) {
		SETERROR(ZSTRM_EBADDATA);
		return 0;
	}

	flags = fetchbyte(state);
	fetchbyte(state);
	fetchbyte(state);
	fetchbyte(state);
	fetchbyte(state);
	fetchbyte(state);
	fetchbyte(state);
	
	/* extra */
	if (flags & 0x04) {
		uint8 a;
		uint8 b;
		uint16 length;
		
		a = fetchbyte(state);
		b = fetchbyte(state);
		for (length = a | (b << 0x08); length; length--)
			fetchbyte(state);
	}

	/* name, comment */
	if (flags & 0x08) while (fetchbyte(state));
	if (flags & 0x10) while (fetchbyte(state));

	/* header crc16 */
	if (flags & 0x02) {
		fetchbyte(state);
		fetchbyte(state);
	}
	
	if (state->error) {
		return 0;
	}
	return 1;
}

CTB_INLINE bool
checktail(struct TZStrm* state)
{
	uint32 crc32;
	uint32 fsize;
	uint8 a;
	uint8 b;
	uint8 c;
	uint8 d;
	
	/* crc32 */
	a = fetchbyte(state);
	b = fetchbyte(state);
	c = fetchbyte(state);
	d = fetchbyte(state);
	crc32 = (a << 0x00) | (b << 0x08) | (c << 0x10) | (d << 0x18);

	if (state->error) {
		return 0;
	}
	else {
		
		if (crc32 != state->crc32) {
			SETERROR(ZSTRM_ECHECKSUM);
			return 0;
		}
	}

	/* isize */
	a = fetchbyte(state);
	b = fetchbyte(state);
	c = fetchbyte(state);
	d = fetchbyte(state);
	
	fsize = (a << 0x00) | (b << 0x08) | (c << 0x10) | (d << 0x18);
	if (state->error) {
		return 0;
	}
	else {
		if (fsize != state->fsize) {
			SETERROR(ZSTRM_EBADDATA);
			return 0;
		}
	}
	return 1;
}


#define sresult state->aux1

static uintxx
inflate(TZStrm* state, uint8* buffer, uintxx size)
{
	uintxx n;
	uintxx maxrun;
	uint8* bbegin;
	uint8* target;
	uint8* tend;
	
	target = state->target;
	tend   = state->tend;
	bbegin = buffer;
	
	while (LIKELY(size)) {
		maxrun = (uintxx) (tend - target);
		if (LIKELY(maxrun)) {
			if (maxrun > size)
				maxrun = size;
			
			for (size -= maxrun; maxrun >= 16; maxrun -= 16) {
				memcpy(buffer, target, 16);
				target += 16;
				buffer += 16;
			}
			for (;maxrun; maxrun--)
				*buffer++ = *target++;
			
			continue;
		}
		
		if (LIKELY(sresult == INFLT_SRCEXHSTD)) {
			intxx r;
			
			r = state->iofn(state->sbgn, ZIOBFFRSZ, state->payload);
			if (LIKELY(r)) {
				if (UNLIKELY((uintxx) r > ZIOBFFRSZ)) {
					SETERROR(ZSTRM_EIOERROR);
					SETSTATE(ZSTRM_BADSTATE);
					return 0;
				}
				
				state->source = state->send = state->sbgn;
				state->send  += r;
				inflator_setsrc(state->infltr, state->sbgn, r);
			}
			else {
				SETERROR(ZSTRM_EBADDATA);
				SETSTATE(ZSTRM_BADSTATE);
				return 0;
			}
		}
		else {
			if (UNLIKELY(sresult == INFLT_OK)) {
				/* end of the stream */
				state->source += inflator_srcend(state->infltr);
				
				if (state->smode == ZSTRM_GZIP) {
					CRC32_FINALIZE(state->crc32);
					if (checktail(state) == 0) {
						SETSTATE(ZSTRM_BADSTATE);
						return 0;
					}
				}
				SETSTATE(3);
				break;
			}
		}
		
		inflator_settgt(state->infltr, state->tbgn, ZIOBFFRSZ);
		sresult = inflator_inflate(state->infltr, 0);
		n = inflator_tgtend(state->infltr);
		
		target = tend = state->tbgn;
		tend  += n;

		if (LIKELY(state->smode == ZSTRM_GZIP)) {
			state->crc32 = crc32_update(state->crc32, target, n);
			state->fsize = state->fsize + (uint32) n;
		}

		if (UNLIKELY(sresult == INFLT_ERROR)) {
			SETERROR(ZSTRM_EDEFLATE);
			SETSTATE(ZSTRM_BADSTATE);
			return 0;
		}
	}
	
	state->target = target;
	state->tend   = tend;
	return (uintxx) (buffer - bbegin);
}

uintxx
zstrm_r(TZStrm* state, uint8* buffer, uintxx size)
{
	uintxx total;
	ASSERT(state);
	
	/* check the stream mode */
	if (UNLIKELY(state->infltr == NULL)) {
		SETSTATE(ZSTRM_BADSTATE);
		if (state->error == 0) {
			SETERROR(ZSTRM_EBADUSE);
		}
		return 0;
	}
	
	if (LIKELY(state->state == 1)) {
		return inflate(state, buffer, size);
	}
	if (LIKELY(state->state == 0)) {
		if (UNLIKELY(state->iofn == NULL)) {
			SETSTATE(ZSTRM_BADSTATE);
			SETERROR(ZSTRM_EIOERROR);
			return 0;
		}
		
		if (state->smode == ZSTRM_GZIP || state->smode == ZSTRM_AUTO) {
			if (readheader(state) == 0) {
				if (state->smode == ZSTRM_AUTO) {
					state->source = state->sbgn;
					
					if (state->sbgn[0] == 0x1f && state->sbgn[1] == 0x8b) {
						SETSTATE(ZSTRM_BADSTATE);
						return 0;
					}
					SETERROR(0);
					state->smode = ZSTRM_DEFLATE;
				}
				else {
					SETSTATE(ZSTRM_BADSTATE);
					return 0;
				}
			}
			else {
				if (state->smode == ZSTRM_AUTO) {
					state->smode = ZSTRM_GZIP;
				}
			}
			
			total = state->send - state->source;
			inflator_setsrc(state->infltr, state->source, total);
			sresult = INFLT_TGTEXHSTD;
		}
		else {
			sresult = INFLT_SRCEXHSTD;
		}
		
		SETSTATE(1);
		return zstrm_r(state, buffer, size);
	}
	return 0;
}

#undef sresult


CTB_INLINE void
emittarget(struct TZStrm* state, uintxx count)
{
	intxx r;
	
	if (UNLIKELY(count == 0)) {
		return;
	}
	r = state->iofn(state->tbgn, count, state->payload);
	if (LIKELY(r)) {
		if ((uintxx) r > count) {
			SETERROR(ZSTRM_EIOERROR);
			return;
		}
		state->target = state->tbgn;
	}
}

static void
emitbyte(struct TZStrm* state, uint8 value)
{
	if (LIKELY(state->target < state->tend)) {
		*state->target++ = value;
	}
	else {
		emittarget(state, (uintxx) (state->target - state->tbgn));
		if (UNLIKELY(state->error)) {
			return;
		}
		*state->target++ = value;
	}
}

static bool
emitheader(struct TZStrm* state)
{
	/* file ID */
	emitbyte(state, 0x1f);
	emitbyte(state, 0x8b);

	/* compression method */
	emitbyte(state, 0x08);

	emitbyte(state, 0x00);
	emitbyte(state, 0x00);
	emitbyte(state, 0x00);
	emitbyte(state, 0x00);
	emitbyte(state, 0x00);
	emitbyte(state, 0x00);
	emitbyte(state, 0x00);
	
	emittarget(state, (uintxx) (state->target - state->tbgn));
	if (state->error) {
		return 0;
	}
	return 1;
}

static uintxx
deflate(TZStrm* state, uint8* buffer, uintxx size)
{
	uintxx maxrun;
	uintxx r;
	uint8* bbegin;
	uint8* source;
	uint8* send;
	
	source = state->source;
	send   = state->send;
	bbegin = buffer;
	
	while (LIKELY(size)) {
		maxrun = (uintxx) (send - source);
		if (LIKELY(maxrun)) {
			if (maxrun > size)
				maxrun = size;

			for (size -= maxrun; maxrun >= 16; maxrun -= 16) {
				memcpy(source, buffer, 16);
				source += 16;
				buffer += 16;
			};
			for (;maxrun; maxrun--)
				*source++ = *buffer++;
			
			continue;
		}

		deflator_setsrc(state->defltr, state->sbgn, ZIOBFFRSZ);
		if (LIKELY(state->smode == ZSTRM_GZIP)) {
			state->crc32 = crc32_update(state->crc32, state->sbgn, ZIOBFFRSZ);
			state->fsize = state->fsize + ZIOBFFRSZ;
		}

		do {
			deflator_settgt(state->defltr, state->tbgn, ZIOBFFRSZ);
			r = deflator_deflate(state->defltr, 0);
			
			emittarget(state, deflator_tgtend(state->defltr));
			if (UNLIKELY(state->error)) {
				SETSTATE(ZSTRM_BADSTATE);
				return 0;
			}
		} while (r == DEFLT_TGTEXHSTD);
		
		source = state->sbgn;
	}
	
	state->source = source;
	return (uintxx) (buffer - bbegin);
}


#define sflushmode state->aux1

uintxx
zstrm_w(TZStrm* state, uint8* buffer, uintxx size)
{
	ASSERT(state);
	
	/* check the stream mode */
	if (UNLIKELY(state->defltr == NULL)) {
		SETSTATE(ZSTRM_BADSTATE);
		if (state->error == 0) {
			SETERROR(ZSTRM_EBADUSE);
		}
		return 0;
	}
	
	if (LIKELY(state->state == 1)) {
		return deflate(state, buffer, size);
	}
	if (LIKELY(state->state == 0)) {
		if (UNLIKELY(state->iofn == NULL)) {
			SETSTATE(ZSTRM_BADSTATE);
			SETERROR(ZSTRM_EIOERROR);
			return 0;
		}
		
		if (state->smode == ZSTRM_GZIP) {
			if (emitheader(state) == 0) {
				SETSTATE(ZSTRM_BADSTATE);
				return 0;
			}
		}
		sflushmode = DEFLT_FLUSH;
		SETSTATE(1);
		
		return zstrm_w(state, buffer, size);
	}
	return 0;
}

bool
zstrm_flush(TZStrm* state)
{
	uintxx total;
	uintxx r;
	ASSERT(state);

	if (state->defltr == NULL || state->state != 1) {
		return 0;
	}

	total = (uintxx) (state->source - state->sbgn);
	if (total) {
		deflator_setsrc(state->defltr, state->sbgn, total);
		if (LIKELY(state->smode == ZSTRM_GZIP)) {
			state->crc32 = crc32_update(state->crc32, state->sbgn, total);
			state->fsize = state->fsize + (uint32)total;
		}
	}

	do {
		deflator_settgt(state->defltr, state->tbgn, ZIOBFFRSZ);
		r = deflator_deflate(state->defltr, sflushmode);
		
		emittarget(state, deflator_tgtend(state->defltr));
		if (UNLIKELY(state->error)) {
			SETSTATE(ZSTRM_BADSTATE);
			return 0;
		}
	} while (r == DEFLT_TGTEXHSTD);

	return 1;
}

bool
zstrm_endstream(TZStrm* state)
{
	ASSERT(state);
	
	if (state->defltr) {
		if (state->state == 3 || state->state == ZSTRM_BADSTATE) {
			return 0;
		}
		
		if (state->state == 0) {
			if (state->iofn == NULL) {
				SETSTATE(ZSTRM_BADSTATE);
				SETSTATE(ZSTRM_EIOERROR);
				return 0;
			}
			
			if (state->smode == ZSTRM_GZIP) {
				if (emitheader(state) == 0) {
					SETSTATE(ZSTRM_BADSTATE);
					return 0;
				}
			}
			SETSTATE(1);
		}
		if (state->state == 1) {
			sflushmode = DEFLT_END;
			zstrm_flush(state);
			if (state->error) {
				goto L_ERROR;
			}
			
			if (state->smode == ZSTRM_GZIP) {
				/* gzip tail */
				uint32 n;
				
				CRC32_FINALIZE(state->crc32);
				n = state->crc32;
				emitbyte(state, (uint8) (n >> 0x00));
				emitbyte(state, (uint8) (n >> 0x08));
				emitbyte(state, (uint8) (n >> 0x10));
				emitbyte(state, (uint8) (n >> 0x18));
				
				n = state->fsize;
				emitbyte(state, (uint8) (n >> 0x00));
				emitbyte(state, (uint8) (n >> 0x08));
				emitbyte(state, (uint8) (n >> 0x10));
				emitbyte(state, (uint8) (n >> 0x18));
				emittarget(state, (uintxx) (state->target - state->tbgn));
				if (state->error) {
					goto L_ERROR;
				}
			}
			
			SETSTATE(3);
		}
		return 1;
	}
	
	if (state->infltr) {
		SETERROR(ZSTRM_EBADUSE);
		goto L_ERROR;
	}
	
L_ERROR:
	SETSTATE(ZSTRM_BADSTATE);
	return 0;
}

bool
zstrm_eof(TZStrm* state)
{
	ASSERT(state);

	if (state->infltr) {
		if (state->state == 3 || state->state == ZSTRM_BADSTATE) {
			return 1;
		}
	}
	return 0;
}


#undef sflushmode

#undef SETSTATE
#undef SETERROR
