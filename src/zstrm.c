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


typedef intxx (*TZStrmIOFn)(uint8*, uintxx, void*);


/* Private state */
struct TZStrmPrvt {
	/* public fields */
	struct TZStrm public;

	/* IO callback */
	TZStrmIOFn iofn;

	/* IO callback parameter */
	void* user;

	/* source buffer */
	const uint8* input;
	const uint8* inputend;

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


#define ZSTRM_MODEMASK 0x0f00
#define ZSTRM_TYPEMASK 0xf000

const TZStrm*
zstrm_create(uintxx flags, intxx level, const TAllocator* allctr)
{
	uint32 smode;
	uint32 stype;
	uintxx n;
	struct TZStrmPrvt* zstrm;

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

		if (level > 9 || level < 0) {
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
	zstrm = allctr->request(n, allctr->user);
	if (zstrm == NULL) {
		return NULL;
	}
	zstrm->allctr = allctr;

	if (smode == ZSTRM_INFLATE) {
		zstrm->defltr = NULL;
		zstrm->infltr = inflator_create(0, allctr);
		if (zstrm->infltr == NULL) {
			zstrm_destroy(&zstrm->public);
			return NULL;
		}
	}
	if (smode == ZSTRM_DEFLATE) {
		zstrm->infltr = NULL;
		zstrm->defltr = deflator_create(0, level, allctr);
		if (zstrm->defltr == NULL) {
			zstrm_destroy(&zstrm->public);
			return NULL;
		}
		zstrm->public.level = (int32) level;
	}

	zstrm->public.smode = smode;
	if (smode == ZSTRM_DEFLATE) {
		zstrm->public.stype = stype;

		zstrm->doadler = (flags & ZSTRM_DOADLER) != 0;
		zstrm->docrc   = (flags & ZSTRM_DOCRC  ) != 0;
		if (stype != ZSTRM_DFLT) {
			if (stype == ZSTRM_ZLIB)
				zstrm->doadler = 1;
			if (stype == ZSTRM_GZIP)
				zstrm->docrc = 1;
		}
	}

	zstrm->public.flags = (uint32) flags;
	zstrm_reset(&zstrm->public);
	return (const struct TZStrm*) zstrm;
}

void
zstrm_destroy(const TZStrm* state)
{
	struct TZStrmPrvt* zstrm;
	uintxx n;

	if (state == NULL) {
		return;
	}

	zstrm = CTB_CONSTCAST(state);
	if (zstrm->infltr) {
		inflator_destroy(zstrm->infltr);
	}
	if (zstrm->defltr) {
		deflator_destroy(zstrm->defltr);
	}

	n = sizeof(struct TZStrmPrvt) + IOBFFRSIZE;
	zstrm->allctr->dispose(zstrm, n, zstrm->allctr->user);
}

void
zstrm_reset(const TZStrm* state)
{
	struct TZStrmPrvt* zstrm;
	CTB_ASSERT(state);

	zstrm = CTB_CONSTCAST(state);

	/* public fields */
	zstrm->public.state = 0;
	zstrm->public.error = 0;
	if (zstrm->public.smode == ZSTRM_INFLATE) {
		zstrm->public.stype = 0;
	}
	zstrm->public.dictid = 0;
	zstrm->public.dict   = 0;

	zstrm->public.crc   = 0xffffffffu;
	zstrm->public.adler = 1;
	zstrm->public.total = 0;
	zstrm->public.usedinput = 0;

	/* private fields */
	zstrm->result = 0;
	if (zstrm->public.smode == ZSTRM_INFLATE) {
		zstrm->doadler = (zstrm->public.flags & ZSTRM_DOADLER) != 0;
		zstrm->docrc   = (zstrm->public.flags & ZSTRM_DOCRC  ) != 0;
		inflator_reset(zstrm->infltr);
	}
	else {
		deflator_reset(zstrm->defltr);
	}

	/* IO */
	zstrm->iofn = NULL;
	zstrm->user = NULL;
	zstrm->input    = NULL;
	zstrm->inputend = NULL;

	zstrm->source = NULL;
	zstrm->sbgn = NULL;
	zstrm->send = NULL;
	zstrm->target = NULL;
	zstrm->tbgn = NULL;
	zstrm->tend = NULL;
}


#define SETERROR(ERROR) (zstrm->public.error = (ERROR))
#define SETSTATE(STATE) (zstrm->public.state = (STATE))

void
zstrm_setsource(const TZStrm* state, const uint8* source, uintxx size)
{
	uint8 t[1];
	struct TZStrmPrvt* zstrm;
	CTB_ASSERT(state && source && size);

	zstrm = CTB_CONSTCAST(state);
	if (zstrm->public.smode != ZSTRM_INFLATE || zstrm->public.state) {
		SETSTATE(4);
		if (zstrm->public.error == 0) {
			SETERROR(ZSTRM_EINCORRECTUSE);
		}
		return;
	}
	SETSTATE(1);
	zstrm->input = zstrm->inputend = source;
	zstrm->inputend += size;

	zstrm_inflate(state, t, 0);
}

void
zstrm_setsourcefn(const TZStrm* state, TZStrmIFn fn, void* user)
{
	uint8 t[1];
	struct TZStrmPrvt* zstrm;
	CTB_ASSERT(state && fn);

	zstrm = CTB_CONSTCAST(state);
	if (zstrm->public.smode != ZSTRM_INFLATE || zstrm->public.state) {
		SETSTATE(4);
		if (zstrm->public.error == 0) {
			SETERROR(ZSTRM_EINCORRECTUSE);
		}
		return;
	}
	SETSTATE(1);
	zstrm->user = user;
	zstrm->iofn = (TZStrmIOFn) fn;

	zstrm_inflate(state, t, 0);
}

void
zstrm_settargetfn(const TZStrm* state, TZStrmOFn fn, void* user)
{
	struct TZStrmPrvt* zstrm;
	CTB_ASSERT(state && fn);

	zstrm = CTB_CONSTCAST(state);
	if (zstrm->public.smode != ZSTRM_DEFLATE || zstrm->public.state) {
		SETSTATE(4);
		if (zstrm->public.error == 0) {
			SETERROR(ZSTRM_EINCORRECTUSE);
		}
		return;
	}
	SETSTATE(1);
	zstrm->user = user;
	zstrm->iofn = (TZStrmIOFn) fn;
}

static uintxx parsehead(struct TZStrmPrvt*);

void
zstrm_setdctn(const TZStrm* state, const uint8* dict, uintxx size)
{
	struct TZStrmPrvt* zstrm;
	CTB_ASSERT(state && dict && size);

	zstrm = CTB_CONSTCAST(state);
	if (zstrm->public.state == 0 || zstrm->public.state == 4) {
		goto L_ERROR;
	}

	if (zstrm->public.smode == ZSTRM_INFLATE) {
		if (zstrm->public.state == 1) {
			if (zstrm->input) {
				zstrm->sbgn = CTB_CONSTCAST(zstrm->input);
				zstrm->send = CTB_CONSTCAST(zstrm->inputend);
			}

			if (parsehead(zstrm) == 0) {
				goto L_ERROR;
			}
		}

		if (zstrm->public.stype == ZSTRM_GZIP) {
			goto L_ERROR;
		}

		if (zstrm->public.state == 2) {
			uint32 adler;

			adler = adler32_update(1, dict, size);
			if (adler != zstrm->public.dictid) {
				SETERROR(ZSTRM_EBADDICT);
				goto L_ERROR;
			}
		}
		else {
			if (zstrm->public.state == 3) {
				goto L_ERROR;
			}
		}
		SETSTATE(3);
		inflator_setdctnr(zstrm->infltr, dict, size);
	}

	if (zstrm->public.smode == ZSTRM_DEFLATE) {
		if (zstrm->public.state != 1) {
			goto L_ERROR;
		}
		if (zstrm->public.stype & ZSTRM_GZIP || zstrm->public.dict == 1) {
			goto L_ERROR;
		}

		zstrm->public.dictid = adler32_update(1, dict, size);
		zstrm->public.dict   = 1;
		deflator_setdctnr(zstrm->defltr, dict, size);
	}
	return;

L_ERROR:
	if (zstrm->public.error == 0) {
		SETERROR(ZSTRM_EINCORRECTUSE);
	}
	SETSTATE(4);
}

CTB_INLINE void
updatechecksums(struct TZStrmPrvt* zstrm, const uint8* buffer, uintxx n)
{
	if (zstrm->docrc) {
		zstrm->public.crc = crc32_update(zstrm->public.crc, buffer, n);
	}
	if (zstrm->doadler) {
		zstrm->public.adler = adler32_update(zstrm->public.adler, buffer, n);
	}
}


/* ***************************************************************************
 * Inflate
 *************************************************************************** */

CTB_INLINE uint8
fetchbyte(struct TZStrmPrvt* zstrm)
{
	if (zstrm->public.error) {
		return 0;
	}

	if (CTB_EXPECT1(zstrm->sbgn < zstrm->send)) {
		return *zstrm->sbgn++;
	}
	if (zstrm->iofn) {
		intxx n;

		n = zstrm->iofn(zstrm->iobuffer, IOBFFRSIZE, zstrm->user);
		if (CTB_EXPECT1(n != 0)) {
			if ((uintxx) n > IOBFFRSIZE) {
				SETERROR(ZSTRM_EIOERROR);
				return 0;
			}

			zstrm->sbgn = zstrm->iobuffer;
			zstrm->send = zstrm->iobuffer + n;
			return *zstrm->sbgn++;
		}
	}
	else {
		SETERROR(ZSTRM_ESRCEXHSTD);
	}

	if (zstrm->public.error == 0) {
		SETERROR(ZSTRM_EBADDATA);
	}
	return 0;
}

static bool
parsegziphead(struct TZStrmPrvt* zstrm)
{
	uint32 id1;
	uint32 id2;
	uint32 flags;

	id1 = fetchbyte(zstrm);
	id2 = fetchbyte(zstrm);
	if (id1 != 0x1f || id2 != 0x8b) {
		if (zstrm->public.error == 0) {
			SETERROR(ZSTRM_EBADDATA);
        }
		return 0;
	}

	/* compression method (deflate only) */
	if (fetchbyte(zstrm) != 0x08) {
		if (zstrm->public.error == 0) {
			SETERROR(ZSTRM_EBADDATA);
		}
		return 0;
	}

	flags = fetchbyte(zstrm);
	fetchbyte(zstrm);
	fetchbyte(zstrm);
	fetchbyte(zstrm);
	fetchbyte(zstrm);
	fetchbyte(zstrm);
	fetchbyte(zstrm);

	/* extra */
	if (flags & 0x04) {
		uint32 a;
		uint32 b;
		uint32 length;

		a = fetchbyte(zstrm);
		b = fetchbyte(zstrm);
		for (length = a | (b << 0x08); length; length--) {
			fetchbyte(zstrm);
		}
	}

	/* name, comment */
	if (flags & 0x08) {
		while (fetchbyte(zstrm));
	}
	if (flags & 0x10) {
		while (fetchbyte(zstrm));
	}

	/* header crc16 */
	if (flags & 0x02) {
		fetchbyte(zstrm);
		fetchbyte(zstrm);
	}

	if (zstrm->public.error) {
		return 0;
	}
	return 1;
}

#define TOI32(A, B, C, D)  ((A) | (B << 0x08) | (C << 0x10) | (D << 0x18))

static bool
parsezlibhead(struct TZStrmPrvt* zstrm)
{
	uint32 a;
	uint32 b;

	a = fetchbyte(zstrm);
	b = fetchbyte(zstrm);
	if (zstrm->public.error == 0) {
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
				uint32 c;
				uint32 d;

				d = fetchbyte(zstrm);
				c = fetchbyte(zstrm);
				b = fetchbyte(zstrm);
				a = fetchbyte(zstrm);
				if (zstrm->public.error) {
					return 0;
				}
				zstrm->public.dictid = TOI32(a, b, c, d);

				SETSTATE(2);
				return 1;
			}
		}
		else {
			if (zstrm->public.error == 0) {
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
parsehead(struct TZStrmPrvt* zstrm)
{
	uint32 stype;
	uint32 head;

	head = fetchbyte(zstrm);
	if (zstrm->public.error) {
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

	if ((zstrm->public.flags & stype) == 0) {
		SETERROR(ZSTRM_EFORMAT);
		return 0;
	}
	zstrm->public.stype = stype;

	zstrm->sbgn--;
	switch (zstrm->public.stype) {
		case ZSTRM_GZIP: zstrm->docrc   = 1; parsegziphead(zstrm); break;
		case ZSTRM_ZLIB: zstrm->doadler = 1; parsezlibhead(zstrm); break;
		case ZSTRM_DFLT:
			break;
	}
	if (zstrm->public.error) {
		return 0;
	}

	zstrm->public.usedinput += (uintxx) (zstrm->sbgn - zstrm->source);
	return 1;
}

CTB_INLINE void
checkgziptail(struct TZStrmPrvt* zstrm)
{
	uint32 crc;
	uint32 total;
	uint32 a;
	uint32 b;
	uint32 c;
	uint32 d;

	/* crc32 */
	a = fetchbyte(zstrm);
	b = fetchbyte(zstrm);
	c = fetchbyte(zstrm);
	d = fetchbyte(zstrm);
	crc = TOI32(a, b, c, d);

	if (zstrm->public.error == 0) {
		if (crc != zstrm->public.crc) {
			SETERROR(ZSTRM_ECHECKSUM);
			return;
		}
	}
	else {
		return;
	}

	/* isize */
	a = fetchbyte(zstrm);
	b = fetchbyte(zstrm);
	c = fetchbyte(zstrm);
	d = fetchbyte(zstrm);
	total = TOI32(a, b, c, d);

	if (total != zstrm->public.total) {
		if (zstrm->public.error) {
			return;
		}
		SETERROR(ZSTRM_EBADDATA);
	}
}

CTB_INLINE void
checkzlibtail(struct TZStrmPrvt* zstrm)
{
	uint32 a;
	uint32 b;
	uint32 c;
	uint32 d;
	uint32 adler;

	d = fetchbyte(zstrm);
	c = fetchbyte(zstrm);
	b = fetchbyte(zstrm);
	a = fetchbyte(zstrm);
	adler = TOI32(a, b, c, d);

	if (adler != zstrm->public.adler) {
		if (zstrm->public.error == 0) {
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
	struct TZStrmPrvt* zstrm;
	CTB_ASSERT(state && target);

	zstrm = CTB_CONSTCAST(state);

	/* check the stream mode */
	if (CTB_EXPECT0(zstrm->infltr == NULL)) {
		SETSTATE(4);
		if (zstrm->public.error == 0) {
			SETERROR(ZSTRM_EINCORRECTUSE);
		}
		return 0;
	}

	if (CTB_EXPECT1(zstrm->public.state == 3)) {
		return inflate(zstrm, target, n);
	}

	if (zstrm->public.state == 1) {
		uintxx total;

		if (zstrm->input) {
			zstrm->sbgn = CTB_CONSTCAST(zstrm->input);
			zstrm->send = CTB_CONSTCAST(zstrm->inputend);
		}
		zstrm->result = INFLT_TGTEXHSTD;

		if (parsehead(zstrm) == 0) {
			SETSTATE(4);
		}
		else {
			if (zstrm->public.state == 2) {
				/* this allows us to check if we need a dictionary by passing
				 * n = 0 when the state is 1 */
				if (n == 0) {
					return 0;
				}
				SETERROR(ZSTRM_EMISSINGDICT);
			}
		}
		if (zstrm->public.error) {
			SETSTATE(4);
			return 0;
		}

		total = (uintxx) (zstrm->send - zstrm->sbgn);
		inflator_setsrc(zstrm->infltr, zstrm->sbgn, total);

		SETSTATE(3);
		if (n != 0) {
			return inflate(zstrm, target, n);
		}
	}
	else {
		if (zstrm->public.state == 2) {
			SETERROR(ZSTRM_EMISSINGDICT);
			SETSTATE(4);
		}
	}

	return 0;
}


#if defined(__clang__) && defined(CTB_FASTUNALIGNED)
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wcast-align"
#endif


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
inflate(struct TZStrmPrvt* zstrm, uint8* buffer, uintxx total)
{
	uint8* bbgn;
	uint8* tbgn;
	uint8* tend;
	uintxx n;
	struct TInflator* infltr;

	infltr = zstrm->infltr;

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

		if (zstrm->result == INFLT_SRCEXHSTD) {
			if (zstrm->iofn) {
				intxx r;

				r = zstrm->iofn(zstrm->iobuffer, IOBFFRSIZE, zstrm->user);
				if (CTB_EXPECT1(r != 0)) {
					if (CTB_EXPECT0((uintxx) r > IOBFFRSIZE)) {
						SETERROR(ZSTRM_EIOERROR);
						SETSTATE(4);
						break;
					}

					inflator_setsrc(infltr, zstrm->iobuffer, (uintxx) r);
					zstrm->sbgn = CTB_CONSTCAST(infltr->sbgn);
					zstrm->send = CTB_CONSTCAST(infltr->send);
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
			if (zstrm->result == INFLT_OK) {
				uintxx used;

				/* end of the stream */
				zstrm->sbgn += inflator_srcend(infltr);
				if (zstrm->docrc) {
					zstrm->public.crc = zstrm->public.crc ^ 0xffffffffu;
				}

				n = (uintxx) (buffer - bbgn);
				zstrm->public.total += n;
				switch (zstrm->public.stype) {
					case ZSTRM_GZIP: checkgziptail(zstrm); break;
					case ZSTRM_ZLIB: checkzlibtail(zstrm); break;
				}

				used = (uintxx) (zstrm->sbgn - zstrm->source);
				zstrm->public.usedinput = used;
				SETSTATE(4);
				return n;
			}

			if (zstrm->result == INFLT_ERROR) {
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

		zstrm->result = inflator_inflate(infltr, zstrm->source != NULL);
		n = inflator_tgtend(infltr);

		zstrm->public.usedinput += inflator_srcend(infltr);
		if (zstrm->result == INFLT_ERROR) {
			if (n != 0) {
				SETERROR(ZSTRM_EDEFLATE);
				SETSTATE(4);
				break;
			}
			/* we have an error but there is output available */
		}

		if (towindow == 0) {
			updatechecksums(zstrm, buffer, n);
			buffer += n; total -= n;
			tbgn = NULL;
			tend = NULL;
			continue;
		}

		tbgn = infltr->tbgn;
		tend = infltr->tbgn + n;
		updatechecksums(zstrm, tbgn, n);
	}

	infltr->target = tbgn;
	infltr->tend   = tend;

	n = (uintxx) (buffer - bbgn);
	zstrm->public.total += n;
	return n;
}


/* ***************************************************************************
 * Deflate
 *************************************************************************** */

CTB_INLINE void
emittarget(struct TZStrmPrvt* zstrm)
{
	uintxx total;
	intxx r;

	total = (uintxx) (zstrm->tbgn - zstrm->target);
	if (total == 0) {
		return;
	}

	r = zstrm->iofn(zstrm->target, total, zstrm->user);
	if ((uintxx) r > total || (uintxx) r != total) {
		SETERROR(ZSTRM_EIOERROR);
		return;
	}
	zstrm->tbgn = zstrm->target;
}

static void
emitbyte(struct TZStrmPrvt* zstrm, uint8 value)
{
	if (zstrm->public.error != 0) {
		return;
	}

	if (CTB_EXPECT1(zstrm->tbgn < zstrm->tend)) {
		*zstrm->tbgn++ = value;
	}
	else {
		emittarget(zstrm);
		if (CTB_EXPECT0(zstrm->public.error)) {
			return;
		}
		*zstrm->tbgn++ = value;
	}
}

CTB_INLINE void
emitgziphead(struct TZStrmPrvt* zstrm)
{
	/* file ID */
	emitbyte(zstrm, 0x1f);
	emitbyte(zstrm, 0x8b);

	/* compression method */
	emitbyte(zstrm, 0x08);

	emitbyte(zstrm, 0x00);
	emitbyte(zstrm, 0x00);
	emitbyte(zstrm, 0x00);
	emitbyte(zstrm, 0x00);
	emitbyte(zstrm, 0x00);
	emitbyte(zstrm, 0x00);
	emitbyte(zstrm, 0x00);

	emittarget(zstrm);
}

CTB_INLINE void
emitzlibhead(struct TZStrmPrvt* zstrm)
{
	uintxx a;
	uintxx b;

	/* compression method + log(window size) - 8 */
	a = 0x78;
	b = 0;
	if (zstrm->public.dict) {
		b |= 1 << 5;
	}

	/* fcheck */
	b = b + (31 - ((a << 8) | b % 31));

	emitbyte(zstrm, (uint8) a);
	emitbyte(zstrm, (uint8) b);
	if (zstrm->public.dict) {
		uint32 n;

		n = zstrm->public.dictid;
		emitbyte(zstrm, (uint8) (n >> 0x18));
		emitbyte(zstrm, (uint8) (n >> 0x10));
		emitbyte(zstrm, (uint8) (n >> 0x08));
		emitbyte(zstrm, (uint8) (n >> 0x00));
	}
	emittarget(zstrm);
}


static uintxx deflate(struct TZStrmPrvt*, const uint8*, uintxx);


#define DEFLTBFFRSIZE (IOBFFRSIZE >> 1)

uintxx
zstrm_deflate(const TZStrm* state, const void* source, uintxx n)
{
	struct TZStrmPrvt* zstrm;
	CTB_ASSERT(state && source);

	zstrm = CTB_CONSTCAST(state);
	/* check the stream mode */
	if (CTB_EXPECT0(zstrm->defltr == NULL)) {
		SETSTATE(4);
		if (zstrm->public.error == 0) {
			SETERROR(ZSTRM_EINCORRECTUSE);
		}
		return 0;
	}

	if (CTB_EXPECT1(zstrm->public.state == 3)) {
		uintxx r;

		r = deflate(zstrm, source, n);
		zstrm->public.total += r;
		return r;
	}
	if (CTB_EXPECT1(zstrm->public.state == 1 || zstrm->public.state == 2)) {
		zstrm->source = zstrm->iobuffer;
		zstrm->target = zstrm->iobuffer + DEFLTBFFRSIZE;

		zstrm->sbgn = zstrm->source;
		zstrm->tbgn = zstrm->target;
		zstrm->send = zstrm->source + DEFLTBFFRSIZE;
		zstrm->tend = zstrm->target + DEFLTBFFRSIZE;

		switch (zstrm->public.stype) {
			case ZSTRM_GZIP: emitgziphead(zstrm); break;
			case ZSTRM_ZLIB: emitzlibhead(zstrm); break;
		}
		if (zstrm->public.error) {
			SETSTATE(4);
			return 0;
		}

		SETSTATE(3);
		return zstrm_deflate(state, source, n);
	}
	return 0;
}

static void
dochunk(struct TZStrmPrvt* zstrm, uintxx flush, const uint8* source, uintxx n)
{
	uintxx result;
	uintxx total;
	struct TDeflator* defltr;

	defltr = zstrm->defltr;
	deflator_setsrc(defltr, source, n);
	do {
		deflator_settgt(defltr, zstrm->target, DEFLTBFFRSIZE);
		result = deflator_deflate(defltr, (uint32) flush);

		total = deflator_tgtend(defltr);
		if (total != 0) {
			intxx r;

			r = zstrm->iofn(zstrm->target, total, zstrm->user);
			if ((uintxx) r > total || (uintxx) r != total) {
				SETERROR(ZSTRM_EIOERROR);
				break;
			}
			zstrm->tbgn = zstrm->target;
		}
	} while (result == DEFLT_TGTEXHSTD);

	updatechecksums(zstrm, source, deflator_srcend(defltr));
}

static uintxx
deflate(struct TZStrmPrvt* zstrm, const uint8* buffer, uintxx total)
{
	const uint8* bbgn;
	uint8* send;
	uint8* sbgn;

	bbgn = buffer;
	send = zstrm->send;
	sbgn = zstrm->sbgn;
	while (total) {
		uintxx maxrun;

		maxrun = (uintxx) (send - sbgn);
		if (CTB_EXPECT1(maxrun)) {
			if (maxrun > total) {
				maxrun = total;
			}
			else {
				if (maxrun == DEFLTBFFRSIZE) {
					dochunk(zstrm, 0, buffer, total);
					if (zstrm->public.error) {
						SETSTATE(4);
					}
					buffer += total;
					break;
				}
			}

			for (total -= maxrun; maxrun >= 16; maxrun -= 16) {
#if defined(CTB_FASTUNALIGNED)
#if defined(CTB_ENV64)
				((uint64*) sbgn)[0] = ((const uint64*) buffer)[0];
				((uint64*) sbgn)[1] = ((const uint64*) buffer)[1];
#else
				((uint32*) sbgn)[0] = ((const uint32*) buffer)[0];
				((uint32*) sbgn)[1] = ((const uint32*) buffer)[1];
				((uint32*) sbgn)[2] = ((const uint32*) buffer)[2];
				((uint32*) sbgn)[3] = ((const uint32*) buffer)[3];
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

		dochunk(zstrm, 0, zstrm->source, DEFLTBFFRSIZE);
		if (zstrm->public.error) {
			SETSTATE(4);
			break;
		}
		sbgn = zstrm->source;
	}

	zstrm->sbgn = sbgn;
	return (uintxx) (buffer - bbgn);
}

#if defined(__clang__) && defined(CTB_FASTUNALIGNED)
	#pragma clang diagnostic pop
#endif


CTB_INLINE void
emitgziptail(struct TZStrmPrvt* zstrm)
{
	uint32 n;

	zstrm->public.crc = zstrm->public.crc ^ 0xffffffffu;
	n = zstrm->public.crc;
	emitbyte(zstrm, (uint8) (n >> 0x00));
	emitbyte(zstrm, (uint8) (n >> 0x08));
	emitbyte(zstrm, (uint8) (n >> 0x10));
	emitbyte(zstrm, (uint8) (n >> 0x18));

	n = (uint32) zstrm->public.total;
	emitbyte(zstrm, (uint8) (n >> 0x00));
	emitbyte(zstrm, (uint8) (n >> 0x08));
	emitbyte(zstrm, (uint8) (n >> 0x10));
	emitbyte(zstrm, (uint8) (n >> 0x18));
	emittarget(zstrm);
	
}

CTB_INLINE void
emitzlibtail(struct TZStrmPrvt* zstrm)
{
	uint32 n;

	n = zstrm->public.adler;
	emitbyte(zstrm, (uint8) (n >> 0x18));
	emitbyte(zstrm, (uint8) (n >> 0x10));
	emitbyte(zstrm, (uint8) (n >> 0x08));
	emitbyte(zstrm, (uint8) (n >> 0x00));
	emittarget(zstrm);
}

void
zstrm_flush(const TZStrm* state, uintxx final)
{
	uintxx total;
	uintxx flush;
	struct TZStrmPrvt* zstrm;
	CTB_ASSERT(state);

	zstrm = CTB_CONSTCAST(state);
	if (CTB_EXPECT0(zstrm->defltr == NULL || zstrm->public.state ^ 3)) {
		if (zstrm->infltr) {
			if (zstrm->public.error == 0) {
				SETERROR(ZSTRM_EINCORRECTUSE);
			}
			SETSTATE(4);
		}

		if (zstrm->public.state == 1) {
			/* empty stream */
			goto L1;
		}
		return;
	}

	total = (uintxx) (zstrm->sbgn - zstrm->source);

	flush = DEFLT_FLUSH;
	if (final) {
		flush = DEFLT_END;
	}
	dochunk(zstrm, flush, zstrm->source, total);
	if (zstrm->public.error) {
		SETSTATE(4);
		return;
	}

	if (final == 0) {
		return;
	}

L1:
	switch (zstrm->public.stype) {
		case ZSTRM_GZIP: emitgziptail(zstrm); break;
		case ZSTRM_ZLIB: emitzlibtail(zstrm); break;
	}
	SETSTATE(4);
}
