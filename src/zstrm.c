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

#include <jdeflate/zstrm.h>
#include <ctoolbox/crypto/crc32.h>
#include <ctoolbox/crypto/adler32.h>


#define IOBFFRSIZE 32768


/* Private state */
struct TZStrmPrvt {
	/* public fields */
	struct TZStrm public;

	/* IO callback */
	TZStrmIOFn iofn;

	/* IO callback parameter */
	void* payload;

	/* source buffer */
	uint8* input;
	uint8* inputend;

	/* checksum flags */
	uint32 docrc;
	uint32 doadler;

	/* inflator and deflator instances */
	struct TDeflator* defltr;
	struct TInflator* infltr;

	/* last result from inflator_inflate of deflator_deflate */
	uint32 result;

	/* buffers */
	uint8* source;
	uint8* sbgn;
	uint8* send;

	uint8* target;
	uint8* tbgn;
	uint8* tend;

	/* custom allocator */
	const struct TAllocator* allctr;

	/* single IO bufffer */
	uint8 iobuffer[1];
};


#if defined(__GNUC__)
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wcast-qual"
	#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
#endif


#define PRVT ((struct TZStrmPrvt*) state)
#define PBLC ((struct TZStrm*)     state)

CTB_INLINE void*
request_(struct TZStrmPrvt* p, uintxx size)
{
	const struct TAllocator* a;

	a = p->allctr;
	return a->request(size, a->user);
}

CTB_INLINE void
dispose_(struct TZStrmPrvt* p, void* memory, uintxx size)
{
	const struct TAllocator* a;

	a = p->allctr;
	a->dispose(memory, size, a->user);
}


#define ZSTRM_MODEMASK 0x0f00
#define ZSTRM_TYPEMASK 0xf000

const TZStrm*
zstrm_create(uintxx flags, uintxx level, const TAllocator* allctr)
{
	uint32 smode;
	uint32 stype;
	uintxx n;
	struct TZStrm* state;

	smode = flags & ZSTRM_MODEMASK;
	stype = flags & ZSTRM_TYPEMASK;
	if (smode != ZSTRM_INFLATE && smode != ZSTRM_DEFLATE) {
		/* invalid mode */
		return NULL;
	}
	if (stype == 0) {
		if (smode == ZSTRM_DEFLATE) {
			return NULL;
		}
		flags |= (stype = ZSTRM_DFLT | ZSTRM_ZLIB | ZSTRM_GZIP);
	}

	if (smode == ZSTRM_DEFLATE) {
		uintxx invalid;

		if (level > 9) {
			/* invalid compression level */
			return NULL;
		}
		invalid = 0;
		invalid |= ((stype & ZSTRM_DFLT) && (stype & ~((uint32) ZSTRM_DFLT)));
		invalid |= ((stype & ZSTRM_ZLIB) && (stype & ~((uint32) ZSTRM_ZLIB)));
		invalid |= ((stype & ZSTRM_GZIP) && (stype & ~((uint32) ZSTRM_GZIP)));
		if (invalid) {
			return NULL;
		}
	}

	if (allctr == NULL) {
		allctr = ctb_getdefaultallocator();
	}

	n = sizeof(struct TZStrmPrvt) + IOBFFRSIZE;
	state = allctr->request(n, allctr->user);
	if (state == NULL) {
		return NULL;
	}
	PRVT->allctr = allctr;

	if (smode == ZSTRM_INFLATE) {
		PRVT->defltr = NULL;
		PRVT->infltr = inflator_create(0, allctr);
		if (PRVT->infltr == NULL) {
			zstrm_destroy(PBLC);
			return NULL;
		}
	}
	if (smode == ZSTRM_DEFLATE) {
		PRVT->infltr = NULL;
		PRVT->defltr = deflator_create(0, level, allctr);
		if (PRVT->defltr == NULL) {
			zstrm_destroy(PBLC);
			return NULL;
		}
		PBLC->level = (uint32) level;
	}

	PBLC->smode = smode;
	if (smode == ZSTRM_DEFLATE) {
		PBLC->stype = stype;

		PRVT->doadler = (flags & ZSTRM_DOADLER) != 0;
		PRVT->docrc   = (flags & ZSTRM_DOCRC  ) != 0;
		if (stype != ZSTRM_DFLT) {
			if (stype == ZSTRM_ZLIB)
				PRVT->doadler = 1;
			if (stype == ZSTRM_GZIP)
				PRVT->docrc = 1;
		}
	}

	PBLC->flags = (uint32) flags;
	zstrm_reset(state);
	return state;
}

void
zstrm_destroy(const TZStrm* state)
{
	uintxx n;

	if (state == NULL) {
		return;
	}

	if (PRVT->infltr) {
		inflator_destroy(PRVT->infltr);
	}
	if (PRVT->defltr) {
		deflator_destroy(PRVT->defltr);
	}

	n = sizeof(struct TZStrmPrvt) + IOBFFRSIZE;
	dispose_(PRVT, PRVT, n);
}

void
zstrm_reset(const TZStrm* state)
{
	CTB_ASSERT(state);

	/* public fields */
	PBLC->state = 0;
	PBLC->error = 0;
	if (PBLC->smode == ZSTRM_INFLATE) {
		PBLC->stype = 0;
	}

	PBLC->dictid = 0;
	PBLC->dict  = 0;
	PBLC->crc   = 0xffffffff;
	PBLC->adler = 1;
	PBLC->total = 0;

	PBLC->usedinput = 0;

	/* private fields */
	PRVT->result = 0;
	if (PBLC->smode == ZSTRM_INFLATE) {
		PRVT->doadler = (PBLC->flags & ZSTRM_DOADLER) != 0;
		PRVT->docrc   = (PBLC->flags & ZSTRM_DOCRC  ) != 0;

		inflator_reset(PRVT->infltr);
	}
	else {
		deflator_reset(PRVT->defltr);
	}

	/* IO */
	PRVT->iofn = NULL;
	PRVT->payload  = NULL;
	PRVT->input    = NULL;
	PRVT->inputend = NULL;

	PRVT->source = NULL;
	PRVT->sbgn   = NULL;
	PRVT->send   = NULL;
	PRVT->target = NULL;
	PRVT->tbgn   = NULL;
	PRVT->tend   = NULL;
}


#define SETERROR(ERROR) (PBLC->error = (ERROR))
#define SETSTATE(STATE) (PBLC->state = (STATE))

void
zstrm_setsource(const TZStrm* state, const uint8* source, uintxx size)
{
	uint8 t[1];
	CTB_ASSERT(state && source && size);

	if (PBLC->smode != ZSTRM_INFLATE || PBLC->state) {
		SETSTATE(4);
		if (PBLC->error == 0) {
			SETERROR(ZSTRM_EINCORRECTUSE);
		}
		return;
	}
	SETSTATE(1);
	PRVT->input = PRVT->inputend = source;
	PRVT->inputend += size;

	zstrm_inflate(state, t, 0);
}

void
zstrm_setsourcefn(const TZStrm* state, TZStrmIOFn fn, void* payload)
{
	uint8 t[1];
	CTB_ASSERT(state && fn);

	if (PBLC->smode != ZSTRM_INFLATE || PBLC->state) {
		SETSTATE(4);
		if (PBLC->error == 0) {
			SETERROR(ZSTRM_EINCORRECTUSE);
		}
		return;
	}
	SETSTATE(1);
	PRVT->payload = payload;
	PRVT->iofn    = fn;

	zstrm_inflate(state, t, 0);
}

void
zstrm_settargetfn(const TZStrm* state, TZStrmIOFn fn, void* payload)
{
	CTB_ASSERT(state && fn);

	if (PBLC->smode != ZSTRM_DEFLATE || PBLC->state) {
		SETSTATE(4);
		if (PBLC->error == 0) {
			SETERROR(ZSTRM_EINCORRECTUSE);
		}
		return;
	}
	SETSTATE(1);
	PRVT->payload = payload;
	PRVT->iofn    = fn;
}

static uintxx parsehead(struct TZStrmPrvt*);

void
zstrm_setdctn(const TZStrm* state, const uint8* dict, uintxx size)
{
	CTB_ASSERT(state && dict && size);

	if (PBLC->state == 0 || PBLC->state == 4) {
		goto L_ERROR;
	}

	if (PBLC->smode == ZSTRM_INFLATE) {
		if (PBLC->state == 1) {
			if (PRVT->input) {
				PRVT->sbgn = PRVT->input;
				PRVT->send = PRVT->inputend;
			}

			if (parsehead(PRVT) == 0) {
				goto L_ERROR;
			}
		}

		if (PBLC->stype == ZSTRM_GZIP) {
			goto L_ERROR;
		}

		if (PBLC->state == 2) {
			uint32 adler;

			adler = adler32_update(1, dict, size);
			if (adler != PBLC->dictid) {
				SETERROR(ZSTRM_EBADDICT);
				goto L_ERROR;
			}
		}
		else {
			if (PBLC->state == 3) {
				goto L_ERROR;
			}
		}
		SETSTATE(3);
		inflator_setdctnr(PRVT->infltr, dict, size);
	}

	if (PBLC->smode == ZSTRM_DEFLATE) {
		if (PBLC->state != 1) {
			goto L_ERROR;
		}
		if (PBLC->stype & ZSTRM_GZIP || PBLC->dict == 1) {
			goto L_ERROR;
		}

		PBLC->dictid = adler32_update(1, dict, size);
		PBLC->dict   = 1;
		deflator_setdctnr(PRVT->defltr, dict, size);
	}
	return;

L_ERROR:
	if (PBLC->error == 0) {
		SETERROR(ZSTRM_EINCORRECTUSE);
	}
	SETSTATE(4);
}

CTB_INLINE void
updatechecksums(struct TZStrmPrvt* state, const uint8* buffer, uintxx n)
{
	if (PRVT->docrc) {
		PBLC->crc = crc32_update(PBLC->crc, buffer, n);
	}
	if (PRVT->doadler) {
		PBLC->adler = adler32_update(PBLC->adler, buffer, n);
	}
}


/* ***************************************************************************
 * Inflate
 *************************************************************************** */

CTB_INLINE uint8
fetchbyte(struct TZStrmPrvt* state)
{
	if (PBLC->error) {
		return 0;
	}

	if (CTB_EXPECT1(PRVT->sbgn < PRVT->send)) {
		return *PRVT->sbgn++;
	}
	if (PRVT->iofn) {
		intxx n;

		n = PRVT->iofn(PRVT->iobuffer, IOBFFRSIZE, PRVT->payload);
		if (CTB_EXPECT1(n != 0)) {
			if ((uintxx) n > IOBFFRSIZE) {
				SETERROR(ZSTRM_EIOERROR);
				return 0;
			}

			PRVT->sbgn = PRVT->iobuffer;
			PRVT->send = PRVT->iobuffer + n;
			return *PRVT->sbgn++;
		}
	}
	else {
		SETERROR(ZSTRM_ESRCEXHSTD);
	}

	if (PBLC->error == 0) {
		SETERROR(ZSTRM_EBADDATA);
	}
	return 0;
}

static bool
parsegziphead(struct TZStrmPrvt* state)
{
	uint8 id1;
	uint8 id2;
	uint8 flags;

	id1 = fetchbyte(PRVT);
	id2 = fetchbyte(PRVT);
	if (id1 != 0x1f || id2 != 0x8b) {
		if (PBLC->error == 0) {
			SETERROR(ZSTRM_EBADDATA);
        }
		return 0;
	}

	/* compression method (deflate only) */
	if (fetchbyte(PRVT) != 0x08) {
		if (PBLC->error == 0) {
			SETERROR(ZSTRM_EBADDATA);
		}
		return 0;
	}

	flags = fetchbyte(PRVT);
	fetchbyte(PRVT);
	fetchbyte(PRVT);
	fetchbyte(PRVT);
	fetchbyte(PRVT);
	fetchbyte(PRVT);
	fetchbyte(PRVT);

	/* extra */
	if (flags & 0x04) {
		uint8 a;
		uint8 b;
		uint16 length;

		a = fetchbyte(PRVT);
		b = fetchbyte(PRVT);
		for (length = a | (b << 0x08); length; length--) {
			fetchbyte(PRVT);
		}
	}

	/* name, comment */
	if (flags & 0x08) {
		while (fetchbyte(PRVT));
	}
	if (flags & 0x10) {
		while (fetchbyte(PRVT));
	}

	/* header crc16 */
	if (flags & 0x02) {
		fetchbyte(PRVT);
		fetchbyte(PRVT);
	}

	if (PBLC->error) {
		return 0;
	}
	return 1;
}

#define TOI32(A, B, C, D)  ((A) | (B << 0x08) | (C << 0x10) | (D << 0x18))

static bool
parsezlibhead(struct TZStrmPrvt* state)
{
	uint8 a;
	uint8 b;

	a = fetchbyte(PRVT);
	b = fetchbyte(PRVT);
	if (PBLC->error == 0) {
		uintxx cm;
		uintxx ci;

		/* CM and CINFO */
		cm = (a >> 0) & 0x0f;
		ci = (a >> 4) & 0x0f;
		if (cm == 8 && ci <= 7) {
			uintxx fchck;
			uintxx fdict;

			/* FLG (FLaGs) */
			fchck = (b >> 0) & 0x1f;
			fdict = (b >> 5) & 0x01;
			(void) fchck;
			if (fdict) {
				uint8 c;
				uint8 d;

				d = fetchbyte(PRVT);
				c = fetchbyte(PRVT);
				b = fetchbyte(PRVT);
				a = fetchbyte(PRVT);
				if (PBLC->error) {
					return 0;
				}
				PBLC->dictid = TOI32(a, b, c, d);

				SETSTATE(2);
				return 1;
			}
		}
		else {
			if (PBLC->error == 0) {
				SETERROR(ZSTRM_EBADDATA);
			}
			return 0;
		}
	}
	else {
		return 0;
	}

	return 1;
}

static uintxx
parsehead(struct TZStrmPrvt* state)
{
	uint32 stype;
	uint32 head;

	head = fetchbyte(PRVT);
	if (PBLC->error) {
		return 0;
	}

	stype = 0;
	if (head == 0x1f) {
		stype = ZSTRM_GZIP;
	}
	else {
		head = head & 0x0f;
		if (head == 0x08) {
			stype = ZSTRM_ZLIB;
		}
		else {
			head = head & 0x07;
			if (head == 0x06 || head == 0x07) {
				/* invalid block type 11 (reserved) */
				SETERROR(ZSTRM_EBADDATA);
				return 0;
			}
			stype = ZSTRM_DFLT;
		}
	}

	if ((PBLC->flags & stype) == 0) {
		SETERROR(ZSTRM_EFORMAT);
		return 0;
	}
	PBLC->stype = stype;

	PRVT->sbgn--;
	switch (PBLC->stype) {
		case ZSTRM_GZIP: PRVT->docrc   = 1; parsegziphead(PRVT); break;
		case ZSTRM_ZLIB: PRVT->doadler = 1; parsezlibhead(PRVT); break;
		case ZSTRM_DFLT:
			break;
	}
	if (PBLC->error) {
		return 0;
	}

	PBLC->usedinput += (uintxx) (PRVT->sbgn - PRVT->source);
	return 1;
}

CTB_INLINE void
checkgziptail(struct TZStrmPrvt* state)
{
	uint32 crc;
	uint32 total;
	uint8 a;
	uint8 b;
	uint8 c;
	uint8 d;

	/* crc32 */
	a = fetchbyte(PRVT);
	b = fetchbyte(PRVT);
	c = fetchbyte(PRVT);
	d = fetchbyte(PRVT);
	crc = TOI32(a, b, c, d);

	if (PBLC->error == 0) {
		if (crc != PBLC->crc) {
			SETERROR(ZSTRM_ECHECKSUM);
			return;
		}
	}
	else {
		return;
	}

	/* isize */
	a = fetchbyte(PRVT);
	b = fetchbyte(PRVT);
	c = fetchbyte(PRVT);
	d = fetchbyte(PRVT);
	total = TOI32(a, b, c, d);

	if (total != PBLC->total) {
		if (PBLC->error) {
			return;
		}
		SETERROR(ZSTRM_EBADDATA);
	}
}

CTB_INLINE void
checkzlibtail(struct TZStrmPrvt* state)
{
	uint8 a;
	uint8 b;
	uint8 c;
	uint8 d;
	uint32 adler;

	d = fetchbyte(PRVT);
	c = fetchbyte(PRVT);
	b = fetchbyte(PRVT);
	a = fetchbyte(PRVT);
	adler = TOI32(a, b, c, d);

	if (adler != PBLC->adler) {
		if (PBLC->error == 0) {
			SETERROR(ZSTRM_ECHECKSUM);
		}
		return;
	}
}

#undef TOI32


static uintxx inflate(struct TZStrmPrvt*, uint8*, uintxx);

uintxx
zstrm_inflate(const TZStrm* state, void* target, uintxx n)
{
	CTB_ASSERT(state && target);

	/* check the stream mode */
	if (CTB_EXPECT0(PRVT->infltr == NULL)) {
		SETSTATE(4);
		if (PBLC->error == 0) {
			SETERROR(ZSTRM_EINCORRECTUSE);
		}
		return 0;
	}

	if (CTB_EXPECT1(PBLC->state == 3)) {
		return inflate(PRVT, target, n);
	}

	if (PBLC->state == 1) {
		uintxx total;

		if (PRVT->input) {
			PRVT->sbgn = PRVT->input;
			PRVT->send = PRVT->inputend;
		}
		PRVT->result = INFLT_TGTEXHSTD;

		if (parsehead(PRVT) == 0) {
			SETSTATE(4);
		}
		else {
			if (PBLC->state == 2) {
				/* this allows us to check if we need a dictionary by passing
				 * n = 0 when the state is 1 */
				if (n == 0) {
					return 0;
				}
				SETERROR(ZSTRM_EMISSINGDICT);
			}
		}
		if (PBLC->error) {
			SETSTATE(4);
			return 0;
		}

		total = (uintxx) (PRVT->send - PRVT->sbgn);
		inflator_setsrc(PRVT->infltr, PRVT->sbgn, total);

		SETSTATE(3);
		if (n != 0) {
			return inflate(PRVT, target, n);
		}
	}
	else {
		if (PBLC->state == 2) {
			SETERROR(ZSTRM_EMISSINGDICT);
			SETSTATE(4);
		}
	}

	return 0;
}


/* This struct should have the same layout as the one in inflator.c, if the
 * layout changes we must reflect those changes here. */
struct TINFLTPrvt {
	struct TInflator public;

	/*
	 * Setting this to 1 will make the inflator use the internal window buffer
	 * to decompress data and the window buffer will be exposed as target
	 * buffer after each inflate call. */
	uintxx towindow;
};

static uintxx
inflate(struct TZStrmPrvt* state, uint8* buffer, uintxx total)
{
	uint8* bbgn;
	uint8* tbgn;
	uint8* tend;
	uintxx n;
	struct TInflator* infltr;

	infltr = PRVT->infltr;

	bbgn = buffer;
	tbgn = infltr->target;
	tend = infltr->tend;
	while (total) {
		uintxx maxrun;
		uintxx towindow;

		maxrun = (uintxx) (tend - tbgn);
		if (maxrun) {
			if (maxrun > total) {
				maxrun = total;
			}

			for (total -= maxrun; maxrun >= 16; maxrun -= 16) {
#if defined(CTB_FASTUNALIGNED)
#if defined(CTB_ENV64)
				((uint64*) buffer)[0] = ((uint64*) tbgn)[0];
				((uint64*) buffer)[1] = ((uint64*) tbgn)[1];
#else
				((uint32*) buffer)[0] = ((uint32*) tbgn)[0];
				((uint32*) buffer)[1] = ((uint32*) tbgn)[1];
				((uint32*) buffer)[2] = ((uint32*) tbgn)[2];
				((uint32*) buffer)[3] = ((uint32*) tbgn)[3];
#endif
				buffer += 16;
				tbgn   += 16;
#else
				buffer[0] = tbgn[0];
				buffer[1] = tbgn[1];
				buffer[2] = tbgn[2];
				buffer[3] = tbgn[3];
				buffer[4] = tbgn[4];
				buffer[5] = tbgn[5];
				buffer[6] = tbgn[6];
				buffer[7] = tbgn[7];
				buffer += 8;
				tbgn   += 8;
				buffer[0] = tbgn[0];
				buffer[1] = tbgn[1];
				buffer[2] = tbgn[2];
				buffer[3] = tbgn[3];
				buffer[4] = tbgn[4];
				buffer[5] = tbgn[5];
				buffer[6] = tbgn[6];
				buffer[7] = tbgn[7];
				buffer += 8;
				tbgn   += 8;
#endif
			}

			for (;maxrun; maxrun--) {
				*buffer++ = *tbgn++;
			}
			continue;
		}

		if (PRVT->result == INFLT_SRCEXHSTD) {
			if (PRVT->iofn) {
				intxx r;

				r = PRVT->iofn(PRVT->iobuffer, IOBFFRSIZE, PRVT->payload);
				if (CTB_EXPECT1(r != 0)) {
					if (CTB_EXPECT0((uintxx) r > IOBFFRSIZE)) {
						SETERROR(ZSTRM_EIOERROR);
						SETSTATE(4);
						break;
					}

					inflator_setsrc(infltr, PRVT->iobuffer, (uintxx) r);
					PRVT->sbgn = infltr->sbgn;
					PRVT->send = infltr->send;
				}
				else {
					SETERROR(ZSTRM_EBADDATA);
					SETSTATE(4);
					break;
				}
			}
			else {
				SETERROR(ZSTRM_ESRCEXHSTD);
				SETSTATE(4);
				break;
			}
		}
		else {
			if (PRVT->result == INFLT_OK) {
				/* end of the stream */
				PRVT->sbgn += inflator_srcend(infltr);
				if (PRVT->docrc) {
					CRC32_FINALIZE(PBLC->crc);
				}

				n = (uintxx) (buffer - bbgn);
				PBLC->total += n;
				switch (PBLC->stype) {
					case ZSTRM_GZIP: checkgziptail(PRVT); break;
					case ZSTRM_ZLIB: checkzlibtail(PRVT); break;
				}

				PBLC->usedinput = (uintxx) (PRVT->sbgn - PRVT->source);
				SETSTATE(4);
				return n;
			}

			if (PRVT->result == INFLT_ERROR) {
				SETERROR(ZSTRM_EDEFLATE);
				SETSTATE(4);
				break;
			}
		}

		if (total >= IOBFFRSIZE) {
			towindow = 0;
			inflator_settgt(infltr, buffer, total);
		}
		else {
			towindow = 1;
		}
		((struct TINFLTPrvt*) infltr)->towindow = towindow;

		PRVT->result = inflator_inflate(infltr, PRVT->source != NULL);
		n = inflator_tgtend(infltr);

		PBLC->usedinput += inflator_srcend(infltr);
		if (PRVT->result == INFLT_ERROR) {
			if (n != 0) {
				SETERROR(ZSTRM_EDEFLATE);
				SETSTATE(4);
				break;
			}
			/* we have an error but there is output avaible */
		}

		if (towindow == 0) {
			updatechecksums(PRVT, buffer, n);
			buffer += n; total -= n;
			tbgn = NULL;
			tend = NULL;
			continue;
		}

		tbgn = infltr->tbgn;
		tend = infltr->tbgn + n;
		updatechecksums(PRVT, tbgn, n);
	}

	infltr->target = tbgn;
	infltr->tend   = tend;

	n = (uintxx) (buffer - bbgn);
	PBLC->total += n;
	return n;
}


/* ***************************************************************************
 * Deflate
 *************************************************************************** */

CTB_INLINE void
emittarget(struct TZStrmPrvt* state)
{
	uintxx total;
	intxx r;

	total = (uintxx) (PRVT->tbgn - PRVT->target);
	if (total == 0) {
		return;
	}

	r = PRVT->iofn(PRVT->target, total, PRVT->payload);
	if ((uintxx) r > total || (uintxx) r != total) {
		SETERROR(ZSTRM_EIOERROR);
		return;
	}
	PRVT->tbgn = PRVT->target;
}

static void
emitbyte(struct TZStrmPrvt* state, uint8 value)
{
	if (PBLC->error != 0) {
		return;
	}

	if (CTB_EXPECT1(PRVT->tbgn < PRVT->tend)) {
		*PRVT->tbgn++ = value;
	}
	else {
		emittarget(PRVT);
		if (CTB_EXPECT0(PBLC->error)) {
			return;
		}
		*PRVT->tbgn++ = value;
	}
}

CTB_INLINE void
emitgziphead(struct TZStrmPrvt* state)
{
	/* file ID */
	emitbyte(PRVT, 0x1f);
	emitbyte(PRVT, 0x8b);

	/* compression method */
	emitbyte(PRVT, 0x08);

	emitbyte(PRVT, 0x00);
	emitbyte(PRVT, 0x00);
	emitbyte(PRVT, 0x00);
	emitbyte(PRVT, 0x00);
	emitbyte(PRVT, 0x00);
	emitbyte(PRVT, 0x00);
	emitbyte(PRVT, 0x00);

	emittarget(PRVT);
}

CTB_INLINE void
emitzlibhead(struct TZStrmPrvt* state)
{
	uintxx a;
	uintxx b;

	/* compression method + log(window size) - 8 */
	a = 0x78;
	b = 0;
	if (PBLC->dict) {
		b |= 1 << 5;
	}

	/* fcheck */
	b = b + (31 - ((a << 8) | b % 31));

	emitbyte(PRVT, (uint8) a);
	emitbyte(PRVT, (uint8) b);
	if (PBLC->dict) {
		uint32 n;

		n = PBLC->dictid;
		emitbyte(PRVT, (uint8) (n >> 0x18));
		emitbyte(PRVT, (uint8) (n >> 0x10));
		emitbyte(PRVT, (uint8) (n >> 0x08));
		emitbyte(PRVT, (uint8) (n >> 0x00));
	}
	emittarget(PRVT);
}


static uintxx deflate(struct TZStrmPrvt*, const uint8*, uintxx);


#define DEFLTBFFRSIZE (IOBFFRSIZE >> 1)

uintxx
zstrm_deflate(const TZStrm* state, const void* source, uintxx n)
{
	CTB_ASSERT(state && source);

	/* check the stream mode */
	if (CTB_EXPECT0(PRVT->defltr == NULL)) {
		SETSTATE(4);
		if (PBLC->error == 0) {
			SETERROR(ZSTRM_EINCORRECTUSE);
		}
		return 0;
	}

	if (CTB_EXPECT1(PBLC->state == 3)) {
		uintxx r;

		r = deflate(PRVT, source, n);
		PBLC->total += r;
		return r;
	}
	if (CTB_EXPECT1(PBLC->state == 1 || PBLC->state == 2)) {
		PRVT->source = PRVT->iobuffer;
		PRVT->target = PRVT->iobuffer + DEFLTBFFRSIZE;

		PRVT->sbgn = PRVT->source;
		PRVT->tbgn = PRVT->target;
		PRVT->send = PRVT->source + DEFLTBFFRSIZE;
		PRVT->tend = PRVT->target + DEFLTBFFRSIZE;

		switch (PBLC->stype) {
			case ZSTRM_GZIP: emitgziphead(PRVT); break;
			case ZSTRM_ZLIB: emitzlibhead(PRVT); break;
		}
		if (PBLC->error) {
			SETSTATE(4);
			return 0;
		}

		SETSTATE(3);
		return zstrm_deflate(PBLC, source, n);
	}
	return 0;
}

static void
dochunk(struct TZStrmPrvt* state, uintxx flush, const uint8* source, uintxx n)
{
	uintxx result;
	uintxx total;
	struct TDeflator* defltr;

	defltr = PRVT->defltr;
	deflator_setsrc(defltr, source, n);
	do {
		deflator_settgt(defltr, PRVT->target, DEFLTBFFRSIZE);
		result = deflator_deflate(defltr, flush);

		total = deflator_tgtend(defltr);
		if (total != 0) {
			intxx r;

			r = PRVT->iofn(PRVT->target, total, PRVT->payload);
			if ((uintxx) r > total || (uintxx) r != total) {
				SETERROR(ZSTRM_EIOERROR);
				break;
			}
			PRVT->tbgn = PRVT->target;
		}
	} while (result == DEFLT_TGTEXHSTD);

	updatechecksums(PRVT, source, deflator_srcend(defltr));
}

static uintxx
deflate(struct TZStrmPrvt* state, const uint8* buffer, uintxx total)
{
	const uint8* bbgn;
	uint8* send;
	uint8* sbgn;

	bbgn = buffer;
	send = PRVT->send;
	sbgn = PRVT->sbgn;
	while (total) {
		uintxx maxrun;

		maxrun = (uintxx) (send - sbgn);
		if (CTB_EXPECT1(maxrun)) {
			if (maxrun > total) {
				maxrun = total;
			}
			else {
				if (maxrun == DEFLTBFFRSIZE) {
					dochunk(PRVT, 0, buffer, total);
					if (PBLC->error) {
						SETSTATE(4);
					}
					buffer += total;
					break;
				}
			}

			for (total -= maxrun; maxrun >= 16; maxrun -= 16) {
#if defined(CTB_FASTUNALIGNED)
#if defined(CTB_ENV64)
				((uint64*) sbgn)[0] = ((uint64*) buffer)[0];
				((uint64*) sbgn)[1] = ((uint64*) buffer)[1];
#else
				((uint32*) sbgn)[0] = ((uint32*) buffer)[0];
				((uint32*) sbgn)[1] = ((uint32*) buffer)[1];
				((uint32*) sbgn)[2] = ((uint32*) buffer)[2];
				((uint32*) sbgn)[3] = ((uint32*) buffer)[3];
#endif
				buffer += 16;
				sbgn   += 16;
#else
				sbgn[0] = buffer[0];
				sbgn[1] = buffer[1];
				sbgn[2] = buffer[2];
				sbgn[3] = buffer[3];
				sbgn[4] = buffer[4];
				sbgn[5] = buffer[5];
				sbgn[6] = buffer[6];
				sbgn[7] = buffer[7];
				buffer += 8;
				sbgn   += 8;
				sbgn[0] = buffer[0];
				sbgn[1] = buffer[1];
				sbgn[2] = buffer[2];
				sbgn[3] = buffer[3];
				sbgn[4] = buffer[4];
				sbgn[5] = buffer[5];
				sbgn[6] = buffer[6];
				sbgn[7] = buffer[7];
				buffer += 8;
				sbgn   += 8;
#endif
			};

			for (;maxrun; maxrun--) {
				*sbgn++ = *buffer++;
			}
			continue;
		}

		dochunk(PRVT, 0, PRVT->source, DEFLTBFFRSIZE);
		if (PBLC->error) {
			SETSTATE(4);
			break;
		}
		sbgn = PRVT->source;
	}

	PRVT->sbgn = sbgn;
	return (uintxx) (buffer - bbgn);
}


CTB_INLINE void
emitgziptail(struct TZStrmPrvt* state)
{
	uint32 n;

	CRC32_FINALIZE(PBLC->crc);
	n = PBLC->crc;
	emitbyte(PRVT, (uint8) (n >> 0x00));
	emitbyte(PRVT, (uint8) (n >> 0x08));
	emitbyte(PRVT, (uint8) (n >> 0x10));
	emitbyte(PRVT, (uint8) (n >> 0x18));

	n = (uint32) PBLC->total;
	emitbyte(PRVT, (uint8) (n >> 0x00));
	emitbyte(PRVT, (uint8) (n >> 0x08));
	emitbyte(PRVT, (uint8) (n >> 0x10));
	emitbyte(PRVT, (uint8) (n >> 0x18));
	emittarget(PRVT);
	
}

CTB_INLINE void
emitzlibtail(struct TZStrmPrvt* state)
{
	uint32 n;

	n = PBLC->adler;
	emitbyte(PRVT, (uint8) (n >> 0x18));
	emitbyte(PRVT, (uint8) (n >> 0x10));
	emitbyte(PRVT, (uint8) (n >> 0x08));
	emitbyte(PRVT, (uint8) (n >> 0x00));
	emittarget(PRVT);
}

void
zstrm_flush(const TZStrm* state, bool final)
{
	uintxx total;
	uintxx flush;
	CTB_ASSERT(state);

	if (CTB_EXPECT0(PRVT->defltr == NULL || PBLC->state ^ 3)) {
		if (PRVT->infltr) {
			if (PBLC->error == 0) {
				SETERROR(ZSTRM_EINCORRECTUSE);
			}
			SETSTATE(4);
		}

		if (PBLC->state == 1) {
			/* empty stream */
			goto L1;
		}
		return;
	}

	total = (uintxx) (PRVT->sbgn - PRVT->source);

	flush = DEFLT_FLUSH;
	if (final) {
		flush = DEFLT_END;
	}
	dochunk(PRVT, flush, PRVT->source, total);
	if (PBLC->error) {
		SETSTATE(4);
		return;
	}

	if (final == 0) {
		return;
	}

L1:
	switch (PBLC->stype) {
		case ZSTRM_GZIP: emitgziptail(PRVT); break;
		case ZSTRM_ZLIB: emitzlibtail(PRVT); break;
	}
	SETSTATE(4);
}


#if defined(__GNUC__)
	#pragma GCC diagnostic pop
#endif

#undef PRVT
#undef PBLC
