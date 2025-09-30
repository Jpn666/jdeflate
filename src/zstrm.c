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


#if defined(ZSTRM_CRC32EXTERNALASM)
extern uint32 zstrm_crc32updateASM(uint32, const uint8*, uintxx);
#endif

#if defined(ZSTRM_ADLER32EXTERNALASM)
extern uint32 zstrm_adler32updateASM(uint32, const uint8*, uintxx);
#endif


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
	zstrm->public.adler = 1u;
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
	zstrm->target = NULL;
	zstrm->sbgn = NULL;
	zstrm->send = NULL;
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


#if defined(ZSTRM_CRC32EXTERNALASM)
	#define CRC32UPDATE zstrm_crc32updateASM
#else
	#define CRC32UPDATE zstrm_crc32update
#endif

#if defined(ZSTRM_ADLER32EXTERNALASM)
	#define ADLER32UPDATE zstrm_adler32updateASM
#else
	#define ADLER32UPDATE zstrm_adler32update
#endif


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

			adler = ADLER32UPDATE(1, dict, size);
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

		zstrm->public.dictid = ADLER32UPDATE(1, dict, size);
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
		zstrm->public.crc = CRC32UPDATE(zstrm->public.crc, buffer, n);
	}
	if (zstrm->doadler) {
		zstrm->public.adler = ADLER32UPDATE(zstrm->public.adler, buffer, n);
	}
}

#undef CRC32UPDATE
#undef ADLER32UPDATE


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


/* ****************************************************************************
 * Adler32
 *************************************************************************** */

#if defined(ZSTRM_ADLER32EXTERNALASM)

uint32
zstrm_adler32update(uint32 adler, const uint8* data, uintxx size)
{
	CTB_ASSERT(data);
	return zstrm_adler32updateASM(adler, data, size);
}

#else

/* Largest prime smaller than 65536 */
#define ADLER_BASE 65521


#define ADLER32_SLICEBY8 \
	b += (a += *data++); \
	b += (a += *data++); \
	b += (a += *data++); \
	b += (a += *data++); \
	b += (a += *data++); \
	b += (a += *data++); \
	b += (a += *data++); \
	b += (a += *data++);


uint32
zstrm_adler32update(uint32 adler, const uint8* data, uintxx size)
{
	uint32 a;
	uint32 b;
	uint32 ra;
	uint32 rb;
	uintxx i;
	CTB_ASSERT(data);

	a = 0xffff & adler;
	b = 0xffff & adler >> 16;

	i = 32;
	for (; size >= 512; size -= 512) {
		do {
			ADLER32_SLICEBY8
			ADLER32_SLICEBY8
		} while(--i);
		i = 32;

		/* modulo reduction */
		ra = a >> 16;
		rb = b >> 16;
		a = (a & 0xffff) + ((ra << 4) - ra);
		b = (b & 0xffff) + ((rb << 4) - rb);
	}

	for (; size >= 16; size -= 16) {
		ADLER32_SLICEBY8
		ADLER32_SLICEBY8
	}

	while (size) {
		b += (a += *data++);
		size--;
	}

	/* modulo reduction */
	ra = a >> 16;
	rb = b >> 16;
	a = (a & 0xffff) + ((ra << 4) - ra);
	b = (b & 0xffff) + ((rb << 4) - rb);
	if (a >= ADLER_BASE)
		a -= ADLER_BASE;
	if (b >= ADLER_BASE)
		b -= ADLER_BASE;

	return (b << 16) | a;
}

#undef ADLER32_SLICEBY8

#endif


/* ****************************************************************************
 * CRC32
 * Based on: https://create.stephan-brumme.com/crc32/ 
 *************************************************************************** */

static const uint32 (*crc32_combinetable)[32];

CTB_INLINE uint32
GF2_matrixtimes(const uint32* matrix, uint32 index)
{
	uint32 result = 0;
	while (index) {
		if (index & 1) {
			result ^= *matrix;
		}
		index >>= 1;
		matrix++;
	}
	return result;
}

uint32
crc32_ncombine(uint32 crc1, uint32 crc2, uint32 size2)
{
	uintxx i;

	for (i = 0; i < 32; i++) {
		if (size2 == 0) {
			break;
		}
		if (size2 & 1) {
			crc1 = GF2_matrixtimes(crc32_combinetable[i], crc1);
		}
		size2 >>= 1;
	}

	return crc1 ^ crc2;
}


#if defined(ZSTRM_CRC32EXTERNALASM)

uint32
zstrm_crc32update(uint32 crc, const uint8* data, uintxx size)
{
	CTB_ASSERT(data);
	return zstrm_crc32updateASM(crc, data, size);
}

#else

#define CRC32TABLES

static const uint32 (*crc32_table)[256];

#if CTB_IS_LITTLEENDIAN
	#define CRC32_4BYTE1_OFFSET 0x00
	#define CRC32_4BYTE2_OFFSET 0x08
	#define CRC32_4BYTE3_OFFSET 0x10
	#define CRC32_4BYTE4_OFFSET 0x18
#else
	#define CRC32_4BYTE1_OFFSET 0x18
	#define CRC32_4BYTE2_OFFSET 0x10
	#define CRC32_4BYTE3_OFFSET 0x08
	#define CRC32_4BYTE4_OFFSET 0x00
#endif


#if defined(CTB_ENV64)

#define CRC32SLICEBY8 \
	rg1 = CTB_SWAP32ONBE(crc) ^ *ptr32++; \
	rg2 = *ptr32++; \
	crc = crc32_table[7][0xFF & (rg1 >> CRC32_4BYTE1_OFFSET)] ^ \
	      crc32_table[6][0xFF & (rg1 >> CRC32_4BYTE2_OFFSET)] ^ \
	      crc32_table[5][0xFF & (rg1 >> CRC32_4BYTE3_OFFSET)] ^ \
	      crc32_table[4][0xFF & (rg1 >> CRC32_4BYTE4_OFFSET)] ^ \
	      crc32_table[3][0xFF & (rg2 >> CRC32_4BYTE1_OFFSET)] ^ \
	      crc32_table[2][0xFF & (rg2 >> CRC32_4BYTE2_OFFSET)] ^ \
	      crc32_table[1][0xFF & (rg2 >> CRC32_4BYTE3_OFFSET)] ^ \
	      crc32_table[0][0xFF & (rg2 >> CRC32_4BYTE4_OFFSET)];


uint32
zstrm_crc32update(uint32 crc, const uint8* data, uintxx size)
{
	const uint32* ptr32;
	uint32 rg1;
	uint32 rg2;
	CTB_ASSERT(data);

	for (; size; size--) {
		if ((((uintxx) data) & (sizeof(uintxx) - 1)) == 0) {
			break;
		}
		crc = (crc >> 8) ^ crc32_table[0][(crc & 0xFF) ^ *data++];
	}
	ptr32 = (const void*) data;
	for (; size >= 64; size -= 64) {
		CRC32SLICEBY8
		CRC32SLICEBY8
		CRC32SLICEBY8
		CRC32SLICEBY8
		CRC32SLICEBY8
		CRC32SLICEBY8
		CRC32SLICEBY8
		CRC32SLICEBY8
	}
	for (; size >= 8; size -= 8) {
		CRC32SLICEBY8
	}
	if (size) {
		data = (const void*) ptr32;
		while (size--) {
			crc = (crc >> 8) ^ crc32_table[0][(crc & 0xFF) ^ *data++];
		}
	}
	return crc;
}

#undef CRC32_SLICEBY8

#else

#define CRC32_SLICEBY4 \
	crc = CTB_SWAP32ONBE(crc) ^ *ptr32++; \
	crc = crc32_table[3][0xFF & (crc >> CRC32_4BYTE1_OFFSET)] ^ \
	      crc32_table[2][0xFF & (crc >> CRC32_4BYTE2_OFFSET)] ^ \
	      crc32_table[1][0xFF & (crc >> CRC32_4BYTE3_OFFSET)] ^ \
	      crc32_table[0][0xFF & (crc >> CRC32_4BYTE4_OFFSET)];


uint32
zstrm_crc32update(uint32 crc, const uint8* data, uintxx size)
{
	const uint32* ptr32;
	CTB_ASSERT(data);

	for (; size; size--) {
		if ((((uintxx) data) & (sizeof(uintxx) - 1)) == 0) {
			break;
		}
		crc = (crc >> 8) ^ crc32_table[0][(crc & 0xFF) ^ *data++];
	}
	ptr32 = (const void*) data;
	for (; size >= 16; size -= 16) {
		CRC32_SLICEBY4
		CRC32_SLICEBY4
		CRC32_SLICEBY4
		CRC32_SLICEBY4
	}
	for (; size >= 4; size -= 4) {
		CRC32_SLICEBY4
	}
	if (size) {
		data = (const void*) ptr32;
		while (size--) {
			crc = (crc >> 8) ^ crc32_table[0][(crc & 0xFF) ^ *data++];
		}
	}
	return crc;
}

#undef CRC32_SLICEBY4

#endif
#endif


#if 0

static uint32
crc32_reflect(uint32 value, uint8 size)
{
	uint8 i;
	uint32 result = 0;
	size--;
	for (i = 0; size + 1 > i; i++) {
		if (value & 1) {
			result |= 1 << (size - i);
		}
		value >>= 1;
	}
	return result;
}

void
crc32_createtable(uint32 table[8][256])
{
	uint32 i;
	uintxx j;
	uint32 x;

	for (i = 0; 256 > i; i++) {
		table[0][i] = crc32_reflect(i, 8) << 24;
		for (j = 0; 8 > j; j++) {
			x = table[0][i];
			if (x & (1u << 31)) {
				table[0][i] = (x << 1) ^ CRC32_POLYNOMIAL;
				continue;
			}
			table[0][i] = x << 1;
		}
		table[0][i] = crc32_reflect(table[0][i], 32);
	}
	for (i = 0; 256 > i; i++) {
		table[1][i] = table[0][table[0][i] & 0xFF] ^ (table[0][i] >> 8);
		table[2][i] = table[0][table[1][i] & 0xFF] ^ (table[1][i] >> 8);
		table[3][i] = table[0][table[2][i] & 0xFF] ^ (table[2][i] >> 8);
		table[4][i] = table[0][table[3][i] & 0xFF] ^ (table[3][i] >> 8);
		table[5][i] = table[0][table[4][i] & 0xFF] ^ (table[4][i] >> 8);
		table[6][i] = table[0][table[5][i] & 0xFF] ^ (table[5][i] >> 8);
		table[7][i] = table[0][table[6][i] & 0xFF] ^ (table[6][i] >> 8);
	}
}

#endif


/* ****************************************************************************
 * CRC32 Lookup Tables
 *************************************************************************** */

#if defined(CRC32TABLES)

static const uint32 crc32_table_[8][256] =
{
	{
		0X00000000u, 0x77073096u, 0xEE0E612Cu, 0x990951BAu, 0x076DC419u,
		0X706AF48Fu, 0xE963A535u, 0x9E6495A3u, 0x0EDB8832u, 0x79DCB8A4u,
		0XE0D5E91Eu, 0x97D2D988u, 0x09B64C2Bu, 0x7EB17CBDu, 0xE7B82D07u,
		0X90BF1D91u, 0x1DB71064u, 0x6AB020F2u, 0xF3B97148u, 0x84BE41DEu,
		0X1ADAD47Du, 0x6DDDE4EBu, 0xF4D4B551u, 0x83D385C7u, 0x136C9856u,
		0X646BA8C0u, 0xFD62F97Au, 0x8A65C9ECu, 0x14015C4Fu, 0x63066CD9u,
		0XFA0F3D63u, 0x8D080DF5u, 0x3B6E20C8u, 0x4C69105Eu, 0xD56041E4u,
		0XA2677172u, 0x3C03E4D1u, 0x4B04D447u, 0xD20D85FDu, 0xA50AB56Bu,
		0X35B5A8FAu, 0x42B2986Cu, 0xDBBBC9D6u, 0xACBCF940u, 0x32D86CE3u,
		0X45DF5C75u, 0xDCD60DCFu, 0xABD13D59u, 0x26D930ACu, 0x51DE003Au,
		0XC8D75180u, 0xBFD06116u, 0x21B4F4B5u, 0x56B3C423u, 0xCFBA9599u,
		0XB8BDA50Fu, 0x2802B89Eu, 0x5F058808u, 0xC60CD9B2u, 0xB10BE924u,
		0X2F6F7C87u, 0x58684C11u, 0xC1611DABu, 0xB6662D3Du, 0x76DC4190u,
		0X01DB7106u, 0x98D220BCu, 0xEFD5102Au, 0x71B18589u, 0x06B6B51Fu,
		0X9FBFE4A5u, 0xE8B8D433u, 0x7807C9A2u, 0x0F00F934u, 0x9609A88Eu,
		0XE10E9818u, 0x7F6A0DBBu, 0x086D3D2Du, 0x91646C97u, 0xE6635C01u,
		0X6B6B51F4u, 0x1C6C6162u, 0x856530D8u, 0xF262004Eu, 0x6C0695EDu,
		0X1B01A57Bu, 0x8208F4C1u, 0xF50FC457u, 0x65B0D9C6u, 0x12B7E950u,
		0X8BBEB8EAu, 0xFCB9887Cu, 0x62DD1DDFu, 0x15DA2D49u, 0x8CD37CF3u,
		0XFBD44C65u, 0x4DB26158u, 0x3AB551CEu, 0xA3BC0074u, 0xD4BB30E2u,
		0X4ADFA541u, 0x3DD895D7u, 0xA4D1C46Du, 0xD3D6F4FBu, 0x4369E96Au,
		0X346ED9FCu, 0xAD678846u, 0xDA60B8D0u, 0x44042D73u, 0x33031DE5u,
		0XAA0A4C5Fu, 0xDD0D7CC9u, 0x5005713Cu, 0x270241AAu, 0xBE0B1010u,
		0XC90C2086u, 0x5768B525u, 0x206F85B3u, 0xB966D409u, 0xCE61E49Fu,
		0X5EDEF90Eu, 0x29D9C998u, 0xB0D09822u, 0xC7D7A8B4u, 0x59B33D17u,
		0X2EB40D81u, 0xB7BD5C3Bu, 0xC0BA6CADu, 0xEDB88320u, 0x9ABFB3B6u,
		0X03B6E20Cu, 0x74B1D29Au, 0xEAD54739u, 0x9DD277AFu, 0x04DB2615u,
		0X73DC1683u, 0xE3630B12u, 0x94643B84u, 0x0D6D6A3Eu, 0x7A6A5AA8u,
		0XE40ECF0Bu, 0x9309FF9Du, 0x0A00AE27u, 0x7D079EB1u, 0xF00F9344u,
		0X8708A3D2u, 0x1E01F268u, 0x6906C2FEu, 0xF762575Du, 0x806567CBu,
		0X196C3671u, 0x6E6B06E7u, 0xFED41B76u, 0x89D32BE0u, 0x10DA7A5Au,
		0X67DD4ACCu, 0xF9B9DF6Fu, 0x8EBEEFF9u, 0x17B7BE43u, 0x60B08ED5u,
		0XD6D6A3E8u, 0xA1D1937Eu, 0x38D8C2C4u, 0x4FDFF252u, 0xD1BB67F1u,
		0XA6BC5767u, 0x3FB506DDu, 0x48B2364Bu, 0xD80D2BDAu, 0xAF0A1B4Cu,
		0X36034AF6u, 0x41047A60u, 0xDF60EFC3u, 0xA867DF55u, 0x316E8EEFu,
		0X4669BE79u, 0xCB61B38Cu, 0xBC66831Au, 0x256FD2A0u, 0x5268E236u,
		0XCC0C7795u, 0xBB0B4703u, 0x220216B9u, 0x5505262Fu, 0xC5BA3BBEu,
		0XB2BD0B28u, 0x2BB45A92u, 0x5CB36A04u, 0xC2D7FFA7u, 0xB5D0CF31u,
		0X2CD99E8Bu, 0x5BDEAE1Du, 0x9B64C2B0u, 0xEC63F226u, 0x756AA39Cu,
		0X026D930Au, 0x9C0906A9u, 0xEB0E363Fu, 0x72076785u, 0x05005713u,
		0X95BF4A82u, 0xE2B87A14u, 0x7BB12BAEu, 0x0CB61B38u, 0x92D28E9Bu,
		0XE5D5BE0Du, 0x7CDCEFB7u, 0x0BDBDF21u, 0x86D3D2D4u, 0xF1D4E242u,
		0X68DDB3F8u, 0x1FDA836Eu, 0x81BE16CDu, 0xF6B9265Bu, 0x6FB077E1u,
		0X18B74777u, 0x88085AE6u, 0xFF0F6A70u, 0x66063BCAu, 0x11010B5Cu,
		0X8F659EFFu, 0xF862AE69u, 0x616BFFD3u, 0x166CCF45u, 0xA00AE278u,
		0XD70DD2EEu, 0x4E048354u, 0x3903B3C2u, 0xA7672661u, 0xD06016F7u,
		0X4969474Du, 0x3E6E77DBu, 0xAED16A4Au, 0xD9D65ADCu, 0x40DF0B66u,
		0X37D83BF0u, 0xA9BCAE53u, 0xDEBB9EC5u, 0x47B2CF7Fu, 0x30B5FFE9u,
		0XBDBDF21Cu, 0xCABAC28Au, 0x53B39330u, 0x24B4A3A6u, 0xBAD03605u,
		0XCDD70693u, 0x54DE5729u, 0x23D967BFu, 0xB3667A2Eu, 0xC4614AB8u,
		0X5D681B02u, 0x2A6F2B94u, 0xB40BBE37u, 0xC30C8EA1u, 0x5A05DF1Bu,
		0X2D02EF8Du
	},
	{
		0X00000000u, 0x191B3141u, 0x32366282u, 0x2B2D53C3u, 0x646CC504u,
		0X7D77F445u, 0x565AA786u, 0x4F4196C7u, 0xC8D98A08u, 0xD1C2BB49u,
		0XFAEFE88Au, 0xE3F4D9CBu, 0xACB54F0Cu, 0xB5AE7E4Du, 0x9E832D8Eu,
		0X87981CCFu, 0x4AC21251u, 0x53D92310u, 0x78F470D3u, 0x61EF4192u,
		0X2EAED755u, 0x37B5E614u, 0x1C98B5D7u, 0x05838496u, 0x821B9859u,
		0X9B00A918u, 0xB02DFADBu, 0xA936CB9Au, 0xE6775D5Du, 0xFF6C6C1Cu,
		0XD4413FDFu, 0xCD5A0E9Eu, 0x958424A2u, 0x8C9F15E3u, 0xA7B24620u,
		0XBEA97761u, 0xF1E8E1A6u, 0xE8F3D0E7u, 0xC3DE8324u, 0xDAC5B265u,
		0X5D5DAEAAu, 0x44469FEBu, 0x6F6BCC28u, 0x7670FD69u, 0x39316BAEu,
		0X202A5AEFu, 0x0B07092Cu, 0x121C386Du, 0xDF4636F3u, 0xC65D07B2u,
		0XED705471u, 0xF46B6530u, 0xBB2AF3F7u, 0xA231C2B6u, 0x891C9175u,
		0X9007A034u, 0x179FBCFBu, 0x0E848DBAu, 0x25A9DE79u, 0x3CB2EF38u,
		0X73F379FFu, 0x6AE848BEu, 0x41C51B7Du, 0x58DE2A3Cu, 0xF0794F05u,
		0XE9627E44u, 0xC24F2D87u, 0xDB541CC6u, 0x94158A01u, 0x8D0EBB40u,
		0XA623E883u, 0xBF38D9C2u, 0x38A0C50Du, 0x21BBF44Cu, 0x0A96A78Fu,
		0X138D96CEu, 0x5CCC0009u, 0x45D73148u, 0x6EFA628Bu, 0x77E153CAu,
		0XBABB5D54u, 0xA3A06C15u, 0x888D3FD6u, 0x91960E97u, 0xDED79850u,
		0XC7CCA911u, 0xECE1FAD2u, 0xF5FACB93u, 0x7262D75Cu, 0x6B79E61Du,
		0X4054B5DEu, 0x594F849Fu, 0x160E1258u, 0x0F152319u, 0x243870DAu,
		0X3D23419Bu, 0x65FD6BA7u, 0x7CE65AE6u, 0x57CB0925u, 0x4ED03864u,
		0X0191AEA3u, 0x188A9FE2u, 0x33A7CC21u, 0x2ABCFD60u, 0xAD24E1AFu,
		0XB43FD0EEu, 0x9F12832Du, 0x8609B26Cu, 0xC94824ABu, 0xD05315EAu,
		0XFB7E4629u, 0xE2657768u, 0x2F3F79F6u, 0x362448B7u, 0x1D091B74u,
		0X04122A35u, 0x4B53BCF2u, 0x52488DB3u, 0x7965DE70u, 0x607EEF31u,
		0XE7E6F3FEu, 0xFEFDC2BFu, 0xD5D0917Cu, 0xCCCBA03Du, 0x838A36FAu,
		0X9A9107BBu, 0xB1BC5478u, 0xA8A76539u, 0x3B83984Bu, 0x2298A90Au,
		0X09B5FAC9u, 0x10AECB88u, 0x5FEF5D4Fu, 0x46F46C0Eu, 0x6DD93FCDu,
		0X74C20E8Cu, 0xF35A1243u, 0xEA412302u, 0xC16C70C1u, 0xD8774180u,
		0X9736D747u, 0x8E2DE606u, 0xA500B5C5u, 0xBC1B8484u, 0x71418A1Au,
		0X685ABB5Bu, 0x4377E898u, 0x5A6CD9D9u, 0x152D4F1Eu, 0x0C367E5Fu,
		0X271B2D9Cu, 0x3E001CDDu, 0xB9980012u, 0xA0833153u, 0x8BAE6290u,
		0X92B553D1u, 0xDDF4C516u, 0xC4EFF457u, 0xEFC2A794u, 0xF6D996D5u,
		0XAE07BCE9u, 0xB71C8DA8u, 0x9C31DE6Bu, 0x852AEF2Au, 0xCA6B79EDu,
		0XD37048ACu, 0xF85D1B6Fu, 0xE1462A2Eu, 0x66DE36E1u, 0x7FC507A0u,
		0X54E85463u, 0x4DF36522u, 0x02B2F3E5u, 0x1BA9C2A4u, 0x30849167u,
		0X299FA026u, 0xE4C5AEB8u, 0xFDDE9FF9u, 0xD6F3CC3Au, 0xCFE8FD7Bu,
		0X80A96BBCu, 0x99B25AFDu, 0xB29F093Eu, 0xAB84387Fu, 0x2C1C24B0u,
		0X350715F1u, 0x1E2A4632u, 0x07317773u, 0x4870E1B4u, 0x516BD0F5u,
		0X7A468336u, 0x635DB277u, 0xCBFAD74Eu, 0xD2E1E60Fu, 0xF9CCB5CCu,
		0XE0D7848Du, 0xAF96124Au, 0xB68D230Bu, 0x9DA070C8u, 0x84BB4189u,
		0X03235D46u, 0x1A386C07u, 0x31153FC4u, 0x280E0E85u, 0x674F9842u,
		0X7E54A903u, 0x5579FAC0u, 0x4C62CB81u, 0x8138C51Fu, 0x9823F45Eu,
		0XB30EA79Du, 0xAA1596DCu, 0xE554001Bu, 0xFC4F315Au, 0xD7626299u,
		0XCE7953D8u, 0x49E14F17u, 0x50FA7E56u, 0x7BD72D95u, 0x62CC1CD4u,
		0X2D8D8A13u, 0x3496BB52u, 0x1FBBE891u, 0x06A0D9D0u, 0x5E7EF3ECu,
		0X4765C2ADu, 0x6C48916Eu, 0x7553A02Fu, 0x3A1236E8u, 0x230907A9u,
		0X0824546Au, 0x113F652Bu, 0x96A779E4u, 0x8FBC48A5u, 0xA4911B66u,
		0XBD8A2A27u, 0xF2CBBCE0u, 0xEBD08DA1u, 0xC0FDDE62u, 0xD9E6EF23u,
		0X14BCE1BDu, 0x0DA7D0FCu, 0x268A833Fu, 0x3F91B27Eu, 0x70D024B9u,
		0X69CB15F8u, 0x42E6463Bu, 0x5BFD777Au, 0xDC656BB5u, 0xC57E5AF4u,
		0XEE530937u, 0xF7483876u, 0xB809AEB1u, 0xA1129FF0u, 0x8A3FCC33u,
		0X9324FD72u
	},
	{
		0X00000000u, 0x01C26A37u, 0x0384D46Eu, 0x0246BE59u, 0x0709A8DCu,
		0X06CBC2EBu, 0x048D7CB2u, 0x054F1685u, 0x0E1351B8u, 0x0FD13B8Fu,
		0X0D9785D6u, 0x0C55EFE1u, 0x091AF964u, 0x08D89353u, 0x0A9E2D0Au,
		0X0B5C473Du, 0x1C26A370u, 0x1DE4C947u, 0x1FA2771Eu, 0x1E601D29u,
		0X1B2F0BACu, 0x1AED619Bu, 0x18ABDFC2u, 0x1969B5F5u, 0x1235F2C8u,
		0X13F798FFu, 0x11B126A6u, 0x10734C91u, 0x153C5A14u, 0x14FE3023u,
		0X16B88E7Au, 0x177AE44Du, 0x384D46E0u, 0x398F2CD7u, 0x3BC9928Eu,
		0X3A0BF8B9u, 0x3F44EE3Cu, 0x3E86840Bu, 0x3CC03A52u, 0x3D025065u,
		0X365E1758u, 0x379C7D6Fu, 0x35DAC336u, 0x3418A901u, 0x3157BF84u,
		0X3095D5B3u, 0x32D36BEAu, 0x331101DDu, 0x246BE590u, 0x25A98FA7u,
		0X27EF31FEu, 0x262D5BC9u, 0x23624D4Cu, 0x22A0277Bu, 0x20E69922u,
		0X2124F315u, 0x2A78B428u, 0x2BBADE1Fu, 0x29FC6046u, 0x283E0A71u,
		0X2D711CF4u, 0x2CB376C3u, 0x2EF5C89Au, 0x2F37A2ADu, 0x709A8DC0u,
		0X7158E7F7u, 0x731E59AEu, 0x72DC3399u, 0x7793251Cu, 0x76514F2Bu,
		0X7417F172u, 0x75D59B45u, 0x7E89DC78u, 0x7F4BB64Fu, 0x7D0D0816u,
		0X7CCF6221u, 0x798074A4u, 0x78421E93u, 0x7A04A0CAu, 0x7BC6CAFDu,
		0X6CBC2EB0u, 0x6D7E4487u, 0x6F38FADEu, 0x6EFA90E9u, 0x6BB5866Cu,
		0X6A77EC5Bu, 0x68315202u, 0x69F33835u, 0x62AF7F08u, 0x636D153Fu,
		0X612BAB66u, 0x60E9C151u, 0x65A6D7D4u, 0x6464BDE3u, 0x662203BAu,
		0X67E0698Du, 0x48D7CB20u, 0x4915A117u, 0x4B531F4Eu, 0x4A917579u,
		0X4FDE63FCu, 0x4E1C09CBu, 0x4C5AB792u, 0x4D98DDA5u, 0x46C49A98u,
		0X4706F0AFu, 0x45404EF6u, 0x448224C1u, 0x41CD3244u, 0x400F5873u,
		0X4249E62Au, 0x438B8C1Du, 0x54F16850u, 0x55330267u, 0x5775BC3Eu,
		0X56B7D609u, 0x53F8C08Cu, 0x523AAABBu, 0x507C14E2u, 0x51BE7ED5u,
		0X5AE239E8u, 0x5B2053DFu, 0x5966ED86u, 0x58A487B1u, 0x5DEB9134u,
		0X5C29FB03u, 0x5E6F455Au, 0x5FAD2F6Du, 0xE1351B80u, 0xE0F771B7u,
		0XE2B1CFEEu, 0xE373A5D9u, 0xE63CB35Cu, 0xE7FED96Bu, 0xE5B86732u,
		0XE47A0D05u, 0xEF264A38u, 0xEEE4200Fu, 0xECA29E56u, 0xED60F461u,
		0XE82FE2E4u, 0xE9ED88D3u, 0xEBAB368Au, 0xEA695CBDu, 0xFD13B8F0u,
		0XFCD1D2C7u, 0xFE976C9Eu, 0xFF5506A9u, 0xFA1A102Cu, 0xFBD87A1Bu,
		0XF99EC442u, 0xF85CAE75u, 0xF300E948u, 0xF2C2837Fu, 0xF0843D26u,
		0XF1465711u, 0xF4094194u, 0xF5CB2BA3u, 0xF78D95FAu, 0xF64FFFCDu,
		0XD9785D60u, 0xD8BA3757u, 0xDAFC890Eu, 0xDB3EE339u, 0xDE71F5BCu,
		0XDFB39F8Bu, 0xDDF521D2u, 0xDC374BE5u, 0xD76B0CD8u, 0xD6A966EFu,
		0XD4EFD8B6u, 0xD52DB281u, 0xD062A404u, 0xD1A0CE33u, 0xD3E6706Au,
		0XD2241A5Du, 0xC55EFE10u, 0xC49C9427u, 0xC6DA2A7Eu, 0xC7184049u,
		0XC25756CCu, 0xC3953CFBu, 0xC1D382A2u, 0xC011E895u, 0xCB4DAFA8u,
		0XCA8FC59Fu, 0xC8C97BC6u, 0xC90B11F1u, 0xCC440774u, 0xCD866D43u,
		0XCFC0D31Au, 0xCE02B92Du, 0x91AF9640u, 0x906DFC77u, 0x922B422Eu,
		0X93E92819u, 0x96A63E9Cu, 0x976454ABu, 0x9522EAF2u, 0x94E080C5u,
		0X9FBCC7F8u, 0x9E7EADCFu, 0x9C381396u, 0x9DFA79A1u, 0x98B56F24u,
		0X99770513u, 0x9B31BB4Au, 0x9AF3D17Du, 0x8D893530u, 0x8C4B5F07u,
		0X8E0DE15Eu, 0x8FCF8B69u, 0x8A809DECu, 0x8B42F7DBu, 0x89044982u,
		0X88C623B5u, 0x839A6488u, 0x82580EBFu, 0x801EB0E6u, 0x81DCDAD1u,
		0X8493CC54u, 0x8551A663u, 0x8717183Au, 0x86D5720Du, 0xA9E2D0A0u,
		0XA820BA97u, 0xAA6604CEu, 0xABA46EF9u, 0xAEEB787Cu, 0xAF29124Bu,
		0XAD6FAC12u, 0xACADC625u, 0xA7F18118u, 0xA633EB2Fu, 0xA4755576u,
		0XA5B73F41u, 0xA0F829C4u, 0xA13A43F3u, 0xA37CFDAAu, 0xA2BE979Du,
		0XB5C473D0u, 0xB40619E7u, 0xB640A7BEu, 0xB782CD89u, 0xB2CDDB0Cu,
		0XB30FB13Bu, 0xB1490F62u, 0xB08B6555u, 0xBBD72268u, 0xBA15485Fu,
		0XB853F606u, 0xB9919C31u, 0xBCDE8AB4u, 0xBD1CE083u, 0xBF5A5EDAu,
		0XBE9834EDu
	},
	{
		0X00000000u, 0xB8BC6765u, 0xAA09C88Bu, 0x12B5AFEEu, 0x8F629757u,
		0X37DEF032u, 0x256B5FDCu, 0x9DD738B9u, 0xC5B428EFu, 0x7D084F8Au,
		0X6FBDE064u, 0xD7018701u, 0x4AD6BFB8u, 0xF26AD8DDu, 0xE0DF7733u,
		0X58631056u, 0x5019579Fu, 0xE8A530FAu, 0xFA109F14u, 0x42ACF871u,
		0XDF7BC0C8u, 0x67C7A7ADu, 0x75720843u, 0xCDCE6F26u, 0x95AD7F70u,
		0X2D111815u, 0x3FA4B7FBu, 0x8718D09Eu, 0x1ACFE827u, 0xA2738F42u,
		0XB0C620ACu, 0x087A47C9u, 0xA032AF3Eu, 0x188EC85Bu, 0x0A3B67B5u,
		0XB28700D0u, 0x2F503869u, 0x97EC5F0Cu, 0x8559F0E2u, 0x3DE59787u,
		0X658687D1u, 0xDD3AE0B4u, 0xCF8F4F5Au, 0x7733283Fu, 0xEAE41086u,
		0X525877E3u, 0x40EDD80Du, 0xF851BF68u, 0xF02BF8A1u, 0x48979FC4u,
		0X5A22302Au, 0xE29E574Fu, 0x7F496FF6u, 0xC7F50893u, 0xD540A77Du,
		0X6DFCC018u, 0x359FD04Eu, 0x8D23B72Bu, 0x9F9618C5u, 0x272A7FA0u,
		0XBAFD4719u, 0x0241207Cu, 0x10F48F92u, 0xA848E8F7u, 0x9B14583Du,
		0X23A83F58u, 0x311D90B6u, 0x89A1F7D3u, 0x1476CF6Au, 0xACCAA80Fu,
		0XBE7F07E1u, 0x06C36084u, 0x5EA070D2u, 0xE61C17B7u, 0xF4A9B859u,
		0X4C15DF3Cu, 0xD1C2E785u, 0x697E80E0u, 0x7BCB2F0Eu, 0xC377486Bu,
		0XCB0D0FA2u, 0x73B168C7u, 0x6104C729u, 0xD9B8A04Cu, 0x446F98F5u,
		0XFCD3FF90u, 0xEE66507Eu, 0x56DA371Bu, 0x0EB9274Du, 0xB6054028u,
		0XA4B0EFC6u, 0x1C0C88A3u, 0x81DBB01Au, 0x3967D77Fu, 0x2BD27891u,
		0X936E1FF4u, 0x3B26F703u, 0x839A9066u, 0x912F3F88u, 0x299358EDu,
		0XB4446054u, 0x0CF80731u, 0x1E4DA8DFu, 0xA6F1CFBAu, 0xFE92DFECu,
		0X462EB889u, 0x549B1767u, 0xEC277002u, 0x71F048BBu, 0xC94C2FDEu,
		0XDBF98030u, 0x6345E755u, 0x6B3FA09Cu, 0xD383C7F9u, 0xC1366817u,
		0X798A0F72u, 0xE45D37CBu, 0x5CE150AEu, 0x4E54FF40u, 0xF6E89825u,
		0XAE8B8873u, 0x1637EF16u, 0x048240F8u, 0xBC3E279Du, 0x21E91F24u,
		0X99557841u, 0x8BE0D7AFu, 0x335CB0CAu, 0xED59B63Bu, 0x55E5D15Eu,
		0X47507EB0u, 0xFFEC19D5u, 0x623B216Cu, 0xDA874609u, 0xC832E9E7u,
		0X708E8E82u, 0x28ED9ED4u, 0x9051F9B1u, 0x82E4565Fu, 0x3A58313Au,
		0XA78F0983u, 0x1F336EE6u, 0x0D86C108u, 0xB53AA66Du, 0xBD40E1A4u,
		0X05FC86C1u, 0x1749292Fu, 0xAFF54E4Au, 0x322276F3u, 0x8A9E1196u,
		0X982BBE78u, 0x2097D91Du, 0x78F4C94Bu, 0xC048AE2Eu, 0xD2FD01C0u,
		0X6A4166A5u, 0xF7965E1Cu, 0x4F2A3979u, 0x5D9F9697u, 0xE523F1F2u,
		0X4D6B1905u, 0xF5D77E60u, 0xE762D18Eu, 0x5FDEB6EBu, 0xC2098E52u,
		0X7AB5E937u, 0x680046D9u, 0xD0BC21BCu, 0x88DF31EAu, 0x3063568Fu,
		0X22D6F961u, 0x9A6A9E04u, 0x07BDA6BDu, 0xBF01C1D8u, 0xADB46E36u,
		0X15080953u, 0x1D724E9Au, 0xA5CE29FFu, 0xB77B8611u, 0x0FC7E174u,
		0X9210D9CDu, 0x2AACBEA8u, 0x38191146u, 0x80A57623u, 0xD8C66675u,
		0X607A0110u, 0x72CFAEFEu, 0xCA73C99Bu, 0x57A4F122u, 0xEF189647u,
		0XFDAD39A9u, 0x45115ECCu, 0x764DEE06u, 0xCEF18963u, 0xDC44268Du,
		0X64F841E8u, 0xF92F7951u, 0x41931E34u, 0x5326B1DAu, 0xEB9AD6BFu,
		0XB3F9C6E9u, 0x0B45A18Cu, 0x19F00E62u, 0xA14C6907u, 0x3C9B51BEu,
		0X842736DBu, 0x96929935u, 0x2E2EFE50u, 0x2654B999u, 0x9EE8DEFCu,
		0X8C5D7112u, 0x34E11677u, 0xA9362ECEu, 0x118A49ABu, 0x033FE645u,
		0XBB838120u, 0xE3E09176u, 0x5B5CF613u, 0x49E959FDu, 0xF1553E98u,
		0X6C820621u, 0xD43E6144u, 0xC68BCEAAu, 0x7E37A9CFu, 0xD67F4138u,
		0X6EC3265Du, 0x7C7689B3u, 0xC4CAEED6u, 0x591DD66Fu, 0xE1A1B10Au,
		0XF3141EE4u, 0x4BA87981u, 0x13CB69D7u, 0xAB770EB2u, 0xB9C2A15Cu,
		0X017EC639u, 0x9CA9FE80u, 0x241599E5u, 0x36A0360Bu, 0x8E1C516Eu,
		0X866616A7u, 0x3EDA71C2u, 0x2C6FDE2Cu, 0x94D3B949u, 0x090481F0u,
		0XB1B8E695u, 0xA30D497Bu, 0x1BB12E1Eu, 0x43D23E48u, 0xFB6E592Du,
		0XE9DBF6C3u, 0x516791A6u, 0xCCB0A91Fu, 0x740CCE7Au, 0x66B96194u,
		0XDE0506F1u
	},
	{
		0X00000000u, 0x3D6029B0u, 0x7AC05360u, 0x47A07AD0u, 0xF580A6C0u,
		0XC8E08F70u, 0x8F40F5A0u, 0xB220DC10u, 0x30704BC1u, 0x0D106271u,
		0X4AB018A1u, 0x77D03111u, 0xC5F0ED01u, 0xF890C4B1u, 0xBF30BE61u,
		0X825097D1u, 0x60E09782u, 0x5D80BE32u, 0x1A20C4E2u, 0x2740ED52u,
		0X95603142u, 0xA80018F2u, 0xEFA06222u, 0xD2C04B92u, 0x5090DC43u,
		0X6DF0F5F3u, 0x2A508F23u, 0x1730A693u, 0xA5107A83u, 0x98705333u,
		0XDFD029E3u, 0xE2B00053u, 0xC1C12F04u, 0xFCA106B4u, 0xBB017C64u,
		0X866155D4u, 0x344189C4u, 0x0921A074u, 0x4E81DAA4u, 0x73E1F314u,
		0XF1B164C5u, 0xCCD14D75u, 0x8B7137A5u, 0xB6111E15u, 0x0431C205u,
		0X3951EBB5u, 0x7EF19165u, 0x4391B8D5u, 0xA121B886u, 0x9C419136u,
		0XDBE1EBE6u, 0xE681C256u, 0x54A11E46u, 0x69C137F6u, 0x2E614D26u,
		0X13016496u, 0x9151F347u, 0xAC31DAF7u, 0xEB91A027u, 0xD6F18997u,
		0X64D15587u, 0x59B17C37u, 0x1E1106E7u, 0x23712F57u, 0x58F35849u,
		0X659371F9u, 0x22330B29u, 0x1F532299u, 0xAD73FE89u, 0x9013D739u,
		0XD7B3ADE9u, 0xEAD38459u, 0x68831388u, 0x55E33A38u, 0x124340E8u,
		0X2F236958u, 0x9D03B548u, 0xA0639CF8u, 0xE7C3E628u, 0xDAA3CF98u,
		0X3813CFCBu, 0x0573E67Bu, 0x42D39CABu, 0x7FB3B51Bu, 0xCD93690Bu,
		0XF0F340BBu, 0xB7533A6Bu, 0x8A3313DBu, 0x0863840Au, 0x3503ADBAu,
		0X72A3D76Au, 0x4FC3FEDAu, 0xFDE322CAu, 0xC0830B7Au, 0x872371AAu,
		0XBA43581Au, 0x9932774Du, 0xA4525EFDu, 0xE3F2242Du, 0xDE920D9Du,
		0X6CB2D18Du, 0x51D2F83Du, 0x167282EDu, 0x2B12AB5Du, 0xA9423C8Cu,
		0X9422153Cu, 0xD3826FECu, 0xEEE2465Cu, 0x5CC29A4Cu, 0x61A2B3FCu,
		0X2602C92Cu, 0x1B62E09Cu, 0xF9D2E0CFu, 0xC4B2C97Fu, 0x8312B3AFu,
		0XBE729A1Fu, 0x0C52460Fu, 0x31326FBFu, 0x7692156Fu, 0x4BF23CDFu,
		0XC9A2AB0Eu, 0xF4C282BEu, 0xB362F86Eu, 0x8E02D1DEu, 0x3C220DCEu,
		0X0142247Eu, 0x46E25EAEu, 0x7B82771Eu, 0xB1E6B092u, 0x8C869922u,
		0XCB26E3F2u, 0xF646CA42u, 0x44661652u, 0x79063FE2u, 0x3EA64532u,
		0X03C66C82u, 0x8196FB53u, 0xBCF6D2E3u, 0xFB56A833u, 0xC6368183u,
		0X74165D93u, 0x49767423u, 0x0ED60EF3u, 0x33B62743u, 0xD1062710u,
		0XEC660EA0u, 0xABC67470u, 0x96A65DC0u, 0x248681D0u, 0x19E6A860u,
		0X5E46D2B0u, 0x6326FB00u, 0xE1766CD1u, 0xDC164561u, 0x9BB63FB1u,
		0XA6D61601u, 0x14F6CA11u, 0x2996E3A1u, 0x6E369971u, 0x5356B0C1u,
		0X70279F96u, 0x4D47B626u, 0x0AE7CCF6u, 0x3787E546u, 0x85A73956u,
		0XB8C710E6u, 0xFF676A36u, 0xC2074386u, 0x4057D457u, 0x7D37FDE7u,
		0X3A978737u, 0x07F7AE87u, 0xB5D77297u, 0x88B75B27u, 0xCF1721F7u,
		0XF2770847u, 0x10C70814u, 0x2DA721A4u, 0x6A075B74u, 0x576772C4u,
		0XE547AED4u, 0xD8278764u, 0x9F87FDB4u, 0xA2E7D404u, 0x20B743D5u,
		0X1DD76A65u, 0x5A7710B5u, 0x67173905u, 0xD537E515u, 0xE857CCA5u,
		0XAFF7B675u, 0x92979FC5u, 0xE915E8DBu, 0xD475C16Bu, 0x93D5BBBBu,
		0XAEB5920Bu, 0x1C954E1Bu, 0x21F567ABu, 0x66551D7Bu, 0x5B3534CBu,
		0XD965A31Au, 0xE4058AAAu, 0xA3A5F07Au, 0x9EC5D9CAu, 0x2CE505DAu,
		0X11852C6Au, 0x562556BAu, 0x6B457F0Au, 0x89F57F59u, 0xB49556E9u,
		0XF3352C39u, 0xCE550589u, 0x7C75D999u, 0x4115F029u, 0x06B58AF9u,
		0X3BD5A349u, 0xB9853498u, 0x84E51D28u, 0xC34567F8u, 0xFE254E48u,
		0X4C059258u, 0x7165BBE8u, 0x36C5C138u, 0x0BA5E888u, 0x28D4C7DFu,
		0X15B4EE6Fu, 0x521494BFu, 0x6F74BD0Fu, 0xDD54611Fu, 0xE03448AFu,
		0XA794327Fu, 0x9AF41BCFu, 0x18A48C1Eu, 0x25C4A5AEu, 0x6264DF7Eu,
		0X5F04F6CEu, 0xED242ADEu, 0xD044036Eu, 0x97E479BEu, 0xAA84500Eu,
		0X4834505Du, 0x755479EDu, 0x32F4033Du, 0x0F942A8Du, 0xBDB4F69Du,
		0X80D4DF2Du, 0xC774A5FDu, 0xFA148C4Du, 0x78441B9Cu, 0x4524322Cu,
		0X028448FCu, 0x3FE4614Cu, 0x8DC4BD5Cu, 0xB0A494ECu, 0xF704EE3Cu,
		0XCA64C78Cu
	},
	{
		0X00000000u, 0xCB5CD3A5u, 0x4DC8A10Bu, 0x869472AEu, 0x9B914216u,
		0X50CD91B3u, 0xD659E31Du, 0x1D0530B8u, 0xEC53826Du, 0x270F51C8u,
		0XA19B2366u, 0x6AC7F0C3u, 0x77C2C07Bu, 0xBC9E13DEu, 0x3A0A6170u,
		0XF156B2D5u, 0x03D6029Bu, 0xC88AD13Eu, 0x4E1EA390u, 0x85427035u,
		0X9847408Du, 0x531B9328u, 0xD58FE186u, 0x1ED33223u, 0xEF8580F6u,
		0X24D95353u, 0xA24D21FDu, 0x6911F258u, 0x7414C2E0u, 0xBF481145u,
		0X39DC63EBu, 0xF280B04Eu, 0x07AC0536u, 0xCCF0D693u, 0x4A64A43Du,
		0X81387798u, 0x9C3D4720u, 0x57619485u, 0xD1F5E62Bu, 0x1AA9358Eu,
		0XEBFF875Bu, 0x20A354FEu, 0xA6372650u, 0x6D6BF5F5u, 0x706EC54Du,
		0XBB3216E8u, 0x3DA66446u, 0xF6FAB7E3u, 0x047A07ADu, 0xCF26D408u,
		0X49B2A6A6u, 0x82EE7503u, 0x9FEB45BBu, 0x54B7961Eu, 0xD223E4B0u,
		0X197F3715u, 0xE82985C0u, 0x23755665u, 0xA5E124CBu, 0x6EBDF76Eu,
		0X73B8C7D6u, 0xB8E41473u, 0x3E7066DDu, 0xF52CB578u, 0x0F580A6Cu,
		0XC404D9C9u, 0x4290AB67u, 0x89CC78C2u, 0x94C9487Au, 0x5F959BDFu,
		0XD901E971u, 0x125D3AD4u, 0xE30B8801u, 0x28575BA4u, 0xAEC3290Au,
		0X659FFAAFu, 0x789ACA17u, 0xB3C619B2u, 0x35526B1Cu, 0xFE0EB8B9u,
		0X0C8E08F7u, 0xC7D2DB52u, 0x4146A9FCu, 0x8A1A7A59u, 0x971F4AE1u,
		0X5C439944u, 0xDAD7EBEAu, 0x118B384Fu, 0xE0DD8A9Au, 0x2B81593Fu,
		0XAD152B91u, 0x6649F834u, 0x7B4CC88Cu, 0xB0101B29u, 0x36846987u,
		0XFDD8BA22u, 0x08F40F5Au, 0xC3A8DCFFu, 0x453CAE51u, 0x8E607DF4u,
		0X93654D4Cu, 0x58399EE9u, 0xDEADEC47u, 0x15F13FE2u, 0xE4A78D37u,
		0X2FFB5E92u, 0xA96F2C3Cu, 0x6233FF99u, 0x7F36CF21u, 0xB46A1C84u,
		0X32FE6E2Au, 0xF9A2BD8Fu, 0x0B220DC1u, 0xC07EDE64u, 0x46EAACCAu,
		0X8DB67F6Fu, 0x90B34FD7u, 0x5BEF9C72u, 0xDD7BEEDCu, 0x16273D79u,
		0XE7718FACu, 0x2C2D5C09u, 0xAAB92EA7u, 0x61E5FD02u, 0x7CE0CDBAu,
		0XB7BC1E1Fu, 0x31286CB1u, 0xFA74BF14u, 0x1EB014D8u, 0xD5ECC77Du,
		0X5378B5D3u, 0x98246676u, 0x852156CEu, 0x4E7D856Bu, 0xC8E9F7C5u,
		0X03B52460u, 0xF2E396B5u, 0x39BF4510u, 0xBF2B37BEu, 0x7477E41Bu,
		0X6972D4A3u, 0xA22E0706u, 0x24BA75A8u, 0xEFE6A60Du, 0x1D661643u,
		0XD63AC5E6u, 0x50AEB748u, 0x9BF264EDu, 0x86F75455u, 0x4DAB87F0u,
		0XCB3FF55Eu, 0x006326FBu, 0xF135942Eu, 0x3A69478Bu, 0xBCFD3525u,
		0X77A1E680u, 0x6AA4D638u, 0xA1F8059Du, 0x276C7733u, 0xEC30A496u,
		0X191C11EEu, 0xD240C24Bu, 0x54D4B0E5u, 0x9F886340u, 0x828D53F8u,
		0X49D1805Du, 0xCF45F2F3u, 0x04192156u, 0xF54F9383u, 0x3E134026u,
		0XB8873288u, 0x73DBE12Du, 0x6EDED195u, 0xA5820230u, 0x2316709Eu,
		0XE84AA33Bu, 0x1ACA1375u, 0xD196C0D0u, 0x5702B27Eu, 0x9C5E61DBu,
		0X815B5163u, 0x4A0782C6u, 0xCC93F068u, 0x07CF23CDu, 0xF6999118u,
		0X3DC542BDu, 0xBB513013u, 0x700DE3B6u, 0x6D08D30Eu, 0xA65400ABu,
		0X20C07205u, 0xEB9CA1A0u, 0x11E81EB4u, 0xDAB4CD11u, 0x5C20BFBFu,
		0X977C6C1Au, 0x8A795CA2u, 0x41258F07u, 0xC7B1FDA9u, 0x0CED2E0Cu,
		0XFDBB9CD9u, 0x36E74F7Cu, 0xB0733DD2u, 0x7B2FEE77u, 0x662ADECFu,
		0XAD760D6Au, 0x2BE27FC4u, 0xE0BEAC61u, 0x123E1C2Fu, 0xD962CF8Au,
		0X5FF6BD24u, 0x94AA6E81u, 0x89AF5E39u, 0x42F38D9Cu, 0xC467FF32u,
		0X0F3B2C97u, 0xFE6D9E42u, 0x35314DE7u, 0xB3A53F49u, 0x78F9ECECu,
		0X65FCDC54u, 0xAEA00FF1u, 0x28347D5Fu, 0xE368AEFAu, 0x16441B82u,
		0XDD18C827u, 0x5B8CBA89u, 0x90D0692Cu, 0x8DD55994u, 0x46898A31u,
		0XC01DF89Fu, 0x0B412B3Au, 0xFA1799EFu, 0x314B4A4Au, 0xB7DF38E4u,
		0X7C83EB41u, 0x6186DBF9u, 0xAADA085Cu, 0x2C4E7AF2u, 0xE712A957u,
		0X15921919u, 0xDECECABCu, 0x585AB812u, 0x93066BB7u, 0x8E035B0Fu,
		0X455F88AAu, 0xC3CBFA04u, 0x089729A1u, 0xF9C19B74u, 0x329D48D1u,
		0XB4093A7Fu, 0x7F55E9DAu, 0x6250D962u, 0xA90C0AC7u, 0x2F987869u,
		0XE4C4ABCCu
	},
	{
		0X00000000u, 0xA6770BB4u, 0x979F1129u, 0x31E81A9Du, 0xF44F2413u,
		0X52382FA7u, 0x63D0353Au, 0xC5A73E8Eu, 0x33EF4E67u, 0x959845D3u,
		0XA4705F4Eu, 0x020754FAu, 0xC7A06A74u, 0x61D761C0u, 0x503F7B5Du,
		0XF64870E9u, 0x67DE9CCEu, 0xC1A9977Au, 0xF0418DE7u, 0x56368653u,
		0X9391B8DDu, 0x35E6B369u, 0x040EA9F4u, 0xA279A240u, 0x5431D2A9u,
		0XF246D91Du, 0xC3AEC380u, 0x65D9C834u, 0xA07EF6BAu, 0x0609FD0Eu,
		0X37E1E793u, 0x9196EC27u, 0xCFBD399Cu, 0x69CA3228u, 0x582228B5u,
		0XFE552301u, 0x3BF21D8Fu, 0x9D85163Bu, 0xAC6D0CA6u, 0x0A1A0712u,
		0XFC5277FBu, 0x5A257C4Fu, 0x6BCD66D2u, 0xCDBA6D66u, 0x081D53E8u,
		0XAE6A585Cu, 0x9F8242C1u, 0x39F54975u, 0xA863A552u, 0x0E14AEE6u,
		0X3FFCB47Bu, 0x998BBFCFu, 0x5C2C8141u, 0xFA5B8AF5u, 0xCBB39068u,
		0X6DC49BDCu, 0x9B8CEB35u, 0x3DFBE081u, 0x0C13FA1Cu, 0xAA64F1A8u,
		0X6FC3CF26u, 0xC9B4C492u, 0xF85CDE0Fu, 0x5E2BD5BBu, 0x440B7579u,
		0XE27C7ECDu, 0xD3946450u, 0x75E36FE4u, 0xB044516Au, 0x16335ADEu,
		0X27DB4043u, 0x81AC4BF7u, 0x77E43B1Eu, 0xD19330AAu, 0xE07B2A37u,
		0X460C2183u, 0x83AB1F0Du, 0x25DC14B9u, 0x14340E24u, 0xB2430590u,
		0X23D5E9B7u, 0x85A2E203u, 0xB44AF89Eu, 0x123DF32Au, 0xD79ACDA4u,
		0X71EDC610u, 0x4005DC8Du, 0xE672D739u, 0x103AA7D0u, 0xB64DAC64u,
		0X87A5B6F9u, 0x21D2BD4Du, 0xE47583C3u, 0x42028877u, 0x73EA92EAu,
		0XD59D995Eu, 0x8BB64CE5u, 0x2DC14751u, 0x1C295DCCu, 0xBA5E5678u,
		0X7FF968F6u, 0xD98E6342u, 0xE86679DFu, 0x4E11726Bu, 0xB8590282u,
		0X1E2E0936u, 0x2FC613ABu, 0x89B1181Fu, 0x4C162691u, 0xEA612D25u,
		0XDB8937B8u, 0x7DFE3C0Cu, 0xEC68D02Bu, 0x4A1FDB9Fu, 0x7BF7C102u,
		0XDD80CAB6u, 0x1827F438u, 0xBE50FF8Cu, 0x8FB8E511u, 0x29CFEEA5u,
		0XDF879E4Cu, 0x79F095F8u, 0x48188F65u, 0xEE6F84D1u, 0x2BC8BA5Fu,
		0X8DBFB1EBu, 0xBC57AB76u, 0x1A20A0C2u, 0x8816EAF2u, 0x2E61E146u,
		0X1F89FBDBu, 0xB9FEF06Fu, 0x7C59CEE1u, 0xDA2EC555u, 0xEBC6DFC8u,
		0X4DB1D47Cu, 0xBBF9A495u, 0x1D8EAF21u, 0x2C66B5BCu, 0x8A11BE08u,
		0X4FB68086u, 0xE9C18B32u, 0xD82991AFu, 0x7E5E9A1Bu, 0xEFC8763Cu,
		0X49BF7D88u, 0x78576715u, 0xDE206CA1u, 0x1B87522Fu, 0xBDF0599Bu,
		0X8C184306u, 0x2A6F48B2u, 0xDC27385Bu, 0x7A5033EFu, 0x4BB82972u,
		0XEDCF22C6u, 0x28681C48u, 0x8E1F17FCu, 0xBFF70D61u, 0x198006D5u,
		0X47ABD36Eu, 0xE1DCD8DAu, 0xD034C247u, 0x7643C9F3u, 0xB3E4F77Du,
		0X1593FCC9u, 0x247BE654u, 0x820CEDE0u, 0x74449D09u, 0xD23396BDu,
		0XE3DB8C20u, 0x45AC8794u, 0x800BB91Au, 0x267CB2AEu, 0x1794A833u,
		0XB1E3A387u, 0x20754FA0u, 0x86024414u, 0xB7EA5E89u, 0x119D553Du,
		0XD43A6BB3u, 0x724D6007u, 0x43A57A9Au, 0xE5D2712Eu, 0x139A01C7u,
		0XB5ED0A73u, 0x840510EEu, 0x22721B5Au, 0xE7D525D4u, 0x41A22E60u,
		0X704A34FDu, 0xD63D3F49u, 0xCC1D9F8Bu, 0x6A6A943Fu, 0x5B828EA2u,
		0XFDF58516u, 0x3852BB98u, 0x9E25B02Cu, 0xAFCDAAB1u, 0x09BAA105u,
		0XFFF2D1ECu, 0x5985DA58u, 0x686DC0C5u, 0xCE1ACB71u, 0x0BBDF5FFu,
		0XADCAFE4Bu, 0x9C22E4D6u, 0x3A55EF62u, 0xABC30345u, 0x0DB408F1u,
		0X3C5C126Cu, 0x9A2B19D8u, 0x5F8C2756u, 0xF9FB2CE2u, 0xC813367Fu,
		0X6E643DCBu, 0x982C4D22u, 0x3E5B4696u, 0x0FB35C0Bu, 0xA9C457BFu,
		0X6C636931u, 0xCA146285u, 0xFBFC7818u, 0x5D8B73ACu, 0x03A0A617u,
		0XA5D7ADA3u, 0x943FB73Eu, 0x3248BC8Au, 0xF7EF8204u, 0x519889B0u,
		0X6070932Du, 0xC6079899u, 0x304FE870u, 0x9638E3C4u, 0xA7D0F959u,
		0X01A7F2EDu, 0xC400CC63u, 0x6277C7D7u, 0x539FDD4Au, 0xF5E8D6FEu,
		0X647E3AD9u, 0xC209316Du, 0xF3E12BF0u, 0x55962044u, 0x90311ECAu,
		0X3646157Eu, 0x07AE0FE3u, 0xA1D90457u, 0x579174BEu, 0xF1E67F0Au,
		0XC00E6597u, 0x66796E23u, 0xA3DE50ADu, 0x05A95B19u, 0x34414184u,
		0X92364A30u
	},
	{
		0X00000000u, 0xCCAA009Eu, 0x4225077Du, 0x8E8F07E3u, 0x844A0EFAu,
		0X48E00E64u, 0xC66F0987u, 0x0AC50919u, 0xD3E51BB5u, 0x1F4F1B2Bu,
		0X91C01CC8u, 0x5D6A1C56u, 0x57AF154Fu, 0x9B0515D1u, 0x158A1232u,
		0XD92012ACu, 0x7CBB312Bu, 0xB01131B5u, 0x3E9E3656u, 0xF23436C8u,
		0XF8F13FD1u, 0x345B3F4Fu, 0xBAD438ACu, 0x767E3832u, 0xAF5E2A9Eu,
		0X63F42A00u, 0xED7B2DE3u, 0x21D12D7Du, 0x2B142464u, 0xE7BE24FAu,
		0X69312319u, 0xA59B2387u, 0xF9766256u, 0x35DC62C8u, 0xBB53652Bu,
		0X77F965B5u, 0x7D3C6CACu, 0xB1966C32u, 0x3F196BD1u, 0xF3B36B4Fu,
		0X2A9379E3u, 0xE639797Du, 0x68B67E9Eu, 0xA41C7E00u, 0xAED97719u,
		0X62737787u, 0xECFC7064u, 0x205670FAu, 0x85CD537Du, 0x496753E3u,
		0XC7E85400u, 0x0B42549Eu, 0x01875D87u, 0xCD2D5D19u, 0x43A25AFAu,
		0X8F085A64u, 0x562848C8u, 0x9A824856u, 0x140D4FB5u, 0xD8A74F2Bu,
		0XD2624632u, 0x1EC846ACu, 0x9047414Fu, 0x5CED41D1u, 0x299DC2EDu,
		0XE537C273u, 0x6BB8C590u, 0xA712C50Eu, 0xADD7CC17u, 0x617DCC89u,
		0XEFF2CB6Au, 0x2358CBF4u, 0xFA78D958u, 0x36D2D9C6u, 0xB85DDE25u,
		0X74F7DEBBu, 0x7E32D7A2u, 0xB298D73Cu, 0x3C17D0DFu, 0xF0BDD041u,
		0X5526F3C6u, 0x998CF358u, 0x1703F4BBu, 0xDBA9F425u, 0xD16CFD3Cu,
		0X1DC6FDA2u, 0x9349FA41u, 0x5FE3FADFu, 0x86C3E873u, 0x4A69E8EDu,
		0XC4E6EF0Eu, 0x084CEF90u, 0x0289E689u, 0xCE23E617u, 0x40ACE1F4u,
		0X8C06E16Au, 0xD0EBA0BBu, 0x1C41A025u, 0x92CEA7C6u, 0x5E64A758u,
		0X54A1AE41u, 0x980BAEDFu, 0x1684A93Cu, 0xDA2EA9A2u, 0x030EBB0Eu,
		0XCFA4BB90u, 0x412BBC73u, 0x8D81BCEDu, 0x8744B5F4u, 0x4BEEB56Au,
		0XC561B289u, 0x09CBB217u, 0xAC509190u, 0x60FA910Eu, 0xEE7596EDu,
		0X22DF9673u, 0x281A9F6Au, 0xE4B09FF4u, 0x6A3F9817u, 0xA6959889u,
		0X7FB58A25u, 0xB31F8ABBu, 0x3D908D58u, 0xF13A8DC6u, 0xFBFF84DFu,
		0X37558441u, 0xB9DA83A2u, 0x7570833Cu, 0x533B85DAu, 0x9F918544u,
		0X111E82A7u, 0xDDB48239u, 0xD7718B20u, 0x1BDB8BBEu, 0x95548C5Du,
		0X59FE8CC3u, 0x80DE9E6Fu, 0x4C749EF1u, 0xC2FB9912u, 0x0E51998Cu,
		0X04949095u, 0xC83E900Bu, 0x46B197E8u, 0x8A1B9776u, 0x2F80B4F1u,
		0XE32AB46Fu, 0x6DA5B38Cu, 0xA10FB312u, 0xABCABA0Bu, 0x6760BA95u,
		0XE9EFBD76u, 0x2545BDE8u, 0xFC65AF44u, 0x30CFAFDAu, 0xBE40A839u,
		0X72EAA8A7u, 0x782FA1BEu, 0xB485A120u, 0x3A0AA6C3u, 0xF6A0A65Du,
		0XAA4DE78Cu, 0x66E7E712u, 0xE868E0F1u, 0x24C2E06Fu, 0x2E07E976u,
		0XE2ADE9E8u, 0x6C22EE0Bu, 0xA088EE95u, 0x79A8FC39u, 0xB502FCA7u,
		0X3B8DFB44u, 0xF727FBDAu, 0xFDE2F2C3u, 0x3148F25Du, 0xBFC7F5BEu,
		0X736DF520u, 0xD6F6D6A7u, 0x1A5CD639u, 0x94D3D1DAu, 0x5879D144u,
		0X52BCD85Du, 0x9E16D8C3u, 0x1099DF20u, 0xDC33DFBEu, 0x0513CD12u,
		0XC9B9CD8Cu, 0x4736CA6Fu, 0x8B9CCAF1u, 0x8159C3E8u, 0x4DF3C376u,
		0XC37CC495u, 0x0FD6C40Bu, 0x7AA64737u, 0xB60C47A9u, 0x3883404Au,
		0XF42940D4u, 0xFEEC49CDu, 0x32464953u, 0xBCC94EB0u, 0x70634E2Eu,
		0XA9435C82u, 0x65E95C1Cu, 0xEB665BFFu, 0x27CC5B61u, 0x2D095278u,
		0XE1A352E6u, 0x6F2C5505u, 0xA386559Bu, 0x061D761Cu, 0xCAB77682u,
		0X44387161u, 0x889271FFu, 0x825778E6u, 0x4EFD7878u, 0xC0727F9Bu,
		0X0CD87F05u, 0xD5F86DA9u, 0x19526D37u, 0x97DD6AD4u, 0x5B776A4Au,
		0X51B26353u, 0x9D1863CDu, 0x1397642Eu, 0xDF3D64B0u, 0x83D02561u,
		0X4F7A25FFu, 0xC1F5221Cu, 0x0D5F2282u, 0x079A2B9Bu, 0xCB302B05u,
		0X45BF2CE6u, 0x89152C78u, 0x50353ED4u, 0x9C9F3E4Au, 0x121039A9u,
		0XDEBA3937u, 0xD47F302Eu, 0x18D530B0u, 0x965A3753u, 0x5AF037CDu,
		0XFF6B144Au, 0x33C114D4u, 0xBD4E1337u, 0x71E413A9u, 0x7B211AB0u,
		0XB78B1A2Eu, 0x39041DCDu, 0xF5AE1D53u, 0x2C8E0FFFu, 0xE0240F61u,
		0X6EAB0882u, 0xA201081Cu, 0xA8C40105u, 0x646E019Bu, 0xEAE10678u,
		0X264B06E6u
	}
};

static const uint32 (*crc32_table)[256] = crc32_table_;

#undef CRC32TABLES
#endif


static const uint32 crc32_combinetable_[32][32] =
{
	{
		0x77073096u, 0xEE0E612Cu, 0x076DC419u, 0x0EDB8832u, 0x1DB71064u,
		0x3B6E20C8u, 0x76DC4190u, 0xEDB88320u, 0x00000001u, 0x00000002u,
		0x00000004u, 0x00000008u, 0x00000010u, 0x00000020u, 0x00000040u,
		0x00000080u, 0x00000100u, 0x00000200u, 0x00000400u, 0x00000800u,
		0x00001000u, 0x00002000u, 0x00004000u, 0x00008000u, 0x00010000u,
		0x00020000u, 0x00040000u, 0x00080000u, 0x00100000u, 0x00200000u,
		0x00400000u, 0x00800000u
	},
	{
		0x191B3141u, 0x32366282u, 0x646CC504u, 0xC8D98A08u, 0x4AC21251u,
		0x958424A2u, 0xF0794F05u, 0x3B83984Bu, 0x77073096u, 0xEE0E612Cu,
		0x076DC419u, 0x0EDB8832u, 0x1DB71064u, 0x3B6E20C8u, 0x76DC4190u,
		0xEDB88320u, 0x00000001u, 0x00000002u, 0x00000004u, 0x00000008u,
		0x00000010u, 0x00000020u, 0x00000040u, 0x00000080u, 0x00000100u,
		0x00000200u, 0x00000400u, 0x00000800u, 0x00001000u, 0x00002000u,
		0x00004000u, 0x00008000u
	},
	{
		0xB8BC6765u, 0xAA09C88Bu, 0x8F629757u, 0xC5B428EFu, 0x5019579Fu,
		0xA032AF3Eu, 0x9B14583Du, 0xED59B63Bu, 0x01C26A37u, 0x0384D46Eu,
		0x0709A8DCu, 0x0E1351B8u, 0x1C26A370u, 0x384D46E0u, 0x709A8DC0u,
		0xE1351B80u, 0x191B3141u, 0x32366282u, 0x646CC504u, 0xC8D98A08u,
		0x4AC21251u, 0x958424A2u, 0xF0794F05u, 0x3B83984Bu, 0x77073096u,
		0xEE0E612Cu, 0x076DC419u, 0x0EDB8832u, 0x1DB71064u, 0x3B6E20C8u,
		0x76DC4190u, 0xEDB88320u
	},
	{
		0xCCAA009Eu, 0x4225077Du, 0x844A0EFAu, 0xD3E51BB5u, 0x7CBB312Bu,
		0xF9766256u, 0x299DC2EDu, 0x533B85DAu, 0xA6770BB4u, 0x979F1129u,
		0xF44F2413u, 0x33EF4E67u, 0x67DE9CCEu, 0xCFBD399Cu, 0x440B7579u,
		0x8816EAF2u, 0xCB5CD3A5u, 0x4DC8A10Bu, 0x9B914216u, 0xEC53826Du,
		0x03D6029Bu, 0x07AC0536u, 0x0F580A6Cu, 0x1EB014D8u, 0x3D6029B0u,
		0x7AC05360u, 0xF580A6C0u, 0x30704BC1u, 0x60E09782u, 0xC1C12F04u,
		0x58F35849u, 0xB1E6B092u
	},
	{
		0xAE689191u, 0x87A02563u, 0xD4314C87u, 0x73139F4Fu, 0xE6273E9Eu,
		0x173F7B7Du, 0x2E7EF6FAu, 0x5CFDEDF4u, 0xB9FBDBE8u, 0xA886B191u,
		0x8A7C6563u, 0xCF89CC87u, 0x44629F4Fu, 0x88C53E9Eu, 0xCAFB7B7Du,
		0x4E87F0BBu, 0x9D0FE176u, 0xE16EC4ADu, 0x19AC8F1Bu, 0x33591E36u,
		0x66B23C6Cu, 0xCD6478D8u, 0x41B9F7F1u, 0x8373EFE2u, 0xDD96D985u,
		0x605CB54Bu, 0xC0B96A96u, 0x5A03D36Du, 0xB407A6DAu, 0xB37E4BF5u,
		0xBD8D91ABu, 0xA06A2517u
	},
	{
		0xF1DA05AAu, 0x38C50D15u, 0x718A1A2Au, 0xE3143454u, 0x1D596EE9u,
		0x3AB2DDD2u, 0x7565BBA4u, 0xEACB7748u, 0x0EE7E8D1u, 0x1DCFD1A2u,
		0x3B9FA344u, 0x773F4688u, 0xEE7E8D10u, 0x078C1C61u, 0x0F1838C2u,
		0x1E307184u, 0x3C60E308u, 0x78C1C610u, 0xF1838C20u, 0x38761E01u,
		0x70EC3C02u, 0xE1D87804u, 0x18C1F649u, 0x3183EC92u, 0x6307D924u,
		0xC60FB248u, 0x576E62D1u, 0xAEDCC5A2u, 0x86C88D05u, 0xD6E01C4Bu,
		0x76B13ED7u, 0xED627DAEu
	},
	{
		0x8F352D95u, 0xC51B5D6Bu, 0x5147BC97u, 0xA28F792Eu, 0x9E6FF41Du,
		0xE7AEEE7Bu, 0x142CDAB7u, 0x2859B56Eu, 0x50B36ADCu, 0xA166D5B8u,
		0x99BCAD31u, 0xE8085C23u, 0x0B61BE07u, 0x16C37C0Eu, 0x2D86F81Cu,
		0x5B0DF038u, 0xB61BE070u, 0xB746C6A1u, 0xB5FC8B03u, 0xB0881047u,
		0xBA6126CFu, 0xAFB34BDFu, 0x841791FFu, 0xD35E25BFu, 0x7DCD4D3Fu,
		0xFB9A9A7Eu, 0x2C4432BDu, 0x5888657Au, 0xB110CAF4u, 0xB95093A9u,
		0xA9D02113u, 0x88D14467u
	},
	{
		0x33FFF533u, 0x67FFEA66u, 0xCFFFD4CCu, 0x448EAFD9u, 0x891D5FB2u,
		0xC94BB925u, 0x49E6740Bu, 0x93CCE816u, 0xFCE8D66Du, 0x22A0AA9Bu,
		0x45415536u, 0x8A82AA6Cu, 0xCE745299u, 0x4799A373u, 0x8F3346E6u,
		0xC5178B8Du, 0x515E115Bu, 0xA2BC22B6u, 0x9E09432Du, 0xE763801Bu,
		0x15B60677u, 0x2B6C0CEEu, 0x56D819DCu, 0xADB033B8u, 0x80116131u,
		0xDB53C423u, 0x6DD68E07u, 0xDBAD1C0Eu, 0x6C2B3E5Du, 0xD8567CBAu,
		0x6BDDFF35u, 0xD7BBFE6Au
	},
	{
		0xCE3371CBu, 0x4717E5D7u, 0x8E2FCBAEu, 0xC72E911Du, 0x552C247Bu,
		0xAA5848F6u, 0x8FC197ADu, 0xC4F2291Bu, 0x52955477u, 0xA52AA8EEu,
		0x9124579Du, 0xF939A97Bu, 0x290254B7u, 0x5204A96Eu, 0xA40952DCu,
		0x9363A3F9u, 0xFDB641B3u, 0x201D8527u, 0x403B0A4Eu, 0x8076149Cu,
		0xDB9D2F79u, 0x6C4B58B3u, 0xD896B166u, 0x6A5C648Du, 0xD4B8C91Au,
		0x72009475u, 0xE40128EAu, 0x13735795u, 0x26E6AF2Au, 0x4DCD5E54u,
		0x9B9ABCA8u, 0xEC447F11u
	},
	{
		0x1072DB28u, 0x20E5B650u, 0x41CB6CA0u, 0x8396D940u, 0xDC5CB4C1u,
		0x63C86FC3u, 0xC790DF86u, 0x5450B94Du, 0xA8A1729Au, 0x8A33E375u,
		0xCF16C0ABu, 0x455C8717u, 0x8AB90E2Eu, 0xCE031A1Du, 0x4777327Bu,
		0x8EEE64F6u, 0xC6ADCFADu, 0x562A991Bu, 0xAC553236u, 0x83DB622Du,
		0xDCC7C21Bu, 0x62FE8277u, 0xC5FD04EEu, 0x508B0F9Du, 0xA1161F3Au,
		0x995D3835u, 0xE9CB762Bu, 0x08E7EA17u, 0x11CFD42Eu, 0x239FA85Cu,
		0x473F50B8u, 0x8E7EA170u
	},
	{
		0xF891F16Fu, 0x2A52E49Fu, 0x54A5C93Eu, 0xA94B927Cu, 0x89E622B9u,
		0xC8BD4333u, 0x4A0B8027u, 0x9417004Eu, 0xF35F06DDu, 0x3DCF0BFBu,
		0x7B9E17F6u, 0xF73C2FECu, 0x35095999u, 0x6A12B332u, 0xD4256664u,
		0x733BCA89u, 0xE6779512u, 0x179E2C65u, 0x2F3C58CAu, 0x5E78B194u,
		0xBCF16328u, 0xA293C011u, 0x9E568663u, 0xE7DC0A87u, 0x14C9134Fu,
		0x2992269Eu, 0x53244D3Cu, 0xA6489A78u, 0x97E032B1u, 0xF4B16323u,
		0x3213C007u, 0x6427800Eu
	},
	{
		0x88B6BA63u, 0xCA1C7287u, 0x4F49E34Fu, 0x9E93C69Eu, 0xE6568B7Du,
		0x17DC10BBu, 0x2FB82176u, 0x5F7042ECu, 0xBEE085D8u, 0xA6B00DF1u,
		0x96111DA3u, 0xF7533D07u, 0x35D77C4Fu, 0x6BAEF89Eu, 0xD75DF13Cu,
		0x75CAE439u, 0xEB95C872u, 0x0C5A96A5u, 0x18B52D4Au, 0x316A5A94u,
		0x62D4B528u, 0xC5A96A50u, 0x5023D2E1u, 0xA047A5C2u, 0x9BFE4DC5u,
		0xEC8D9DCBu, 0x026A3DD7u, 0x04D47BAEu, 0x09A8F75Cu, 0x1351EEB8u,
		0x26A3DD70u, 0x4D47BAE0u
	},
	{
		0x5AD8A92Cu, 0xB5B15258u, 0xB013A2F1u, 0xBB5643A3u, 0xADDD8107u,
		0x80CA044Fu, 0xDAE50EDFu, 0x6EBB1BFFu, 0xDD7637FEu, 0x619D69BDu,
		0xC33AD37Au, 0x5D04A0B5u, 0xBA09416Au, 0xAF638495u, 0x85B60F6Bu,
		0xD01D1897u, 0x7B4B376Fu, 0xF6966EDEu, 0x365DDBFDu, 0x6CBBB7FAu,
		0xD9776FF4u, 0x699FD9A9u, 0xD33FB352u, 0x7D0E60E5u, 0xFA1CC1CAu,
		0x2F4885D5u, 0x5E910BAAu, 0xBD221754u, 0xA13528E9u, 0x991B5793u,
		0xE947A967u, 0x09FE548Fu
	},
	{
		0xB566F6E2u, 0xB1BCEB85u, 0xB808D14Bu, 0xAB60A4D7u, 0x8DB04FEFu,
		0xC011999Fu, 0x5B52357Fu, 0xB6A46AFEu, 0xB639D3BDu, 0xB702A13Bu,
		0xB5744437u, 0xB1998E2Fu, 0xB8421A1Fu, 0xABF5327Fu, 0x8C9B62BFu,
		0xC247C33Fu, 0x5FFE803Fu, 0xBFFD007Eu, 0xA48B06BDu, 0x92670B3Bu,
		0xFFBF1037u, 0x240F262Fu, 0x481E4C5Eu, 0x903C98BCu, 0xFB083739u,
		0x2D616833u, 0x5AC2D066u, 0xB585A0CCu, 0xB07A47D9u, 0xBB8589F3u,
		0xAC7A15A7u, 0x83852D0Fu
	},
	{
		0x9D9129BFu, 0xE053553Fu, 0x1BD7AC3Fu, 0x37AF587Eu, 0x6F5EB0FCu,
		0xDEBD61F8u, 0x660BC5B1u, 0xCC178B62u, 0x435E1085u, 0x86BC210Au,
		0xD6094455u, 0x77638EEBu, 0xEEC71DD6u, 0x06FF3DEDu, 0x0DFE7BDAu,
		0x1BFCF7B4u, 0x37F9EF68u, 0x6FF3DED0u, 0xDFE7BDA0u, 0x64BE7D01u,
		0xC97CFA02u, 0x4988F245u, 0x9311E48Au, 0xFD52CF55u, 0x21D498EBu,
		0x43A931D6u, 0x875263ACu, 0xD5D5C119u, 0x70DA8473u, 0xE1B508E6u,
		0x181B178Du, 0x30362F1Au
	},
	{
		0x2EE43A2Cu, 0x5DC87458u, 0xBB90E8B0u, 0xAC50D721u, 0x83D0A803u,
		0xDCD05647u, 0x62D1AACFu, 0xC5A3559Eu, 0x5037AD7Du, 0xA06F5AFAu,
		0x9BAFB3B5u, 0xEC2E612Bu, 0x032DC417u, 0x065B882Eu, 0x0CB7105Cu,
		0x196E20B8u, 0x32DC4170u, 0x65B882E0u, 0xCB7105C0u, 0x4D930DC1u,
		0x9B261B82u, 0xED3D3145u, 0x010B64CBu, 0x0216C996u, 0x042D932Cu,
		0x085B2658u, 0x10B64CB0u, 0x216C9960u, 0x42D932C0u, 0x85B26580u,
		0xD015CD41u, 0x7B5A9CC3u
	},
	{
		0x1B4511EEu, 0x368A23DCu, 0x6D1447B8u, 0xDA288F70u, 0x6F2018A1u,
		0xDE403142u, 0x67F164C5u, 0xCFE2C98Au, 0x44B49555u, 0x89692AAAu,
		0xC9A35315u, 0x4837A06Bu, 0x906F40D6u, 0xFBAF87EDu, 0x2C2E099Bu,
		0x585C1336u, 0xB0B8266Cu, 0xBA014A99u, 0xAF739373u, 0x859620A7u,
		0xD05D470Fu, 0x7BCB885Fu, 0xF79710BEu, 0x345F273Du, 0x68BE4E7Au,
		0xD17C9CF4u, 0x79883FA9u, 0xF3107F52u, 0x3D51F8E5u, 0x7AA3F1CAu,
		0xF547E394u, 0x31FEC169u
	},
	{
		0xBCE15202u, 0xA2B3A245u, 0x9E1642CBu, 0xE75D83D7u, 0x15CA01EFu,
		0x2B9403DEu, 0x572807BCu, 0xAE500F78u, 0x87D118B1u, 0xD4D33723u,
		0x72D76807u, 0xE5AED00Eu, 0x102CA65Du, 0x20594CBAu, 0x40B29974u,
		0x816532E8u, 0xD9BB6391u, 0x6807C163u, 0xD00F82C6u, 0x7B6E03CDu,
		0xF6DC079Au, 0x36C90975u, 0x6D9212EAu, 0xDB2425D4u, 0x6D394DE9u,
		0xDA729BD2u, 0x6F9431E5u, 0xDF2863CAu, 0x6521C1D5u, 0xCA4383AAu,
		0x4FF60115u, 0x9FEC022Au
	},
	{
		0xFF08E5EFu, 0x2560CD9Fu, 0x4AC19B3Eu, 0x9583367Cu, 0xF0776AB9u,
		0x3B9FD333u, 0x773FA666u, 0xEE7F4CCCu, 0x078F9FD9u, 0x0F1F3FB2u,
		0x1E3E7F64u, 0x3C7CFEC8u, 0x78F9FD90u, 0xF1F3FB20u, 0x3896F001u,
		0x712DE002u, 0xE25BC004u, 0x1FC68649u, 0x3F8D0C92u, 0x7F1A1924u,
		0xFE343248u, 0x271962D1u, 0x4E32C5A2u, 0x9C658B44u, 0xE3BA10C9u,
		0x1C0527D3u, 0x380A4FA6u, 0x70149F4Cu, 0xE0293E98u, 0x1B237B71u,
		0x3646F6E2u, 0x6C8DEDC4u
	},
	{
		0x6F76172Eu, 0xDEEC2E5Cu, 0x66A95AF9u, 0xCD52B5F2u, 0x41D46DA5u,
		0x83A8DB4Au, 0xDC20B0D5u, 0x633067EBu, 0xC660CFD6u, 0x57B099EDu,
		0xAF6133DAu, 0x85B361F5u, 0xD017C5ABu, 0x7B5E8D17u, 0xF6BD1A2Eu,
		0x360B321Du, 0x6C16643Au, 0xD82CC874u, 0x6B2896A9u, 0xD6512D52u,
		0x77D35CE5u, 0xEFA6B9CAu, 0x043C75D5u, 0x0878EBAAu, 0x10F1D754u,
		0x21E3AEA8u, 0x43C75D50u, 0x878EBAA0u, 0xD46C7301u, 0x73A9E043u,
		0xE753C086u, 0x15D6874Du
	},
	{
		0x56F5CAB9u, 0xADEB9572u, 0x80A62CA5u, 0xDA3D5F0Bu, 0x6F0BB857u,
		0xDE1770AEu, 0x675FE71Du, 0xCEBFCE3Au, 0x460E9A35u, 0x8C1D346Au,
		0xC34B6E95u, 0x5DE7DB6Bu, 0xBBCFB6D6u, 0xACEE6BEDu, 0x82ADD19Bu,
		0xDE2AA577u, 0x67244CAFu, 0xCE48995Eu, 0x47E034FDu, 0x8FC069FAu,
		0xC4F1D5B5u, 0x5292AD2Bu, 0xA5255A56u, 0x913BB2EDu, 0xF906639Bu,
		0x297DC177u, 0x52FB82EEu, 0xA5F705DCu, 0x909F0DF9u, 0xFA4F1DB3u,
		0x2FEF3D27u, 0x5FDE7A4Eu
	},
	{
		0x385993ACu, 0x70B32758u, 0xE1664EB0u, 0x19BD9B21u, 0x337B3642u,
		0x66F66C84u, 0xCDECD908u, 0x40A8B451u, 0x815168A2u, 0xD9D3D705u,
		0x68D6A84Bu, 0xD1AD5096u, 0x782BA76Du, 0xF0574EDAu, 0x3BDF9BF5u,
		0x77BF37EAu, 0xEF7E6FD4u, 0x058DD9E9u, 0x0B1BB3D2u, 0x163767A4u,
		0x2C6ECF48u, 0x58DD9E90u, 0xB1BB3D20u, 0xB8077C01u, 0xAB7FFE43u,
		0x8D8EFAC7u, 0xC06CF3CFu, 0x5BA8E1DFu, 0xB751C3BEu, 0xB5D2813Du,
		0xB0D4043Bu, 0xBAD90E37u
	},
	{
		0xB4247B20u, 0xB339F001u, 0xBD02E643u, 0xA174CAC7u, 0x999893CFu,
		0xE84021DFu, 0x0BF145FFu, 0x17E28BFEu, 0x2FC517FCu, 0x5F8A2FF8u,
		0xBF145FF0u, 0xA559B9A1u, 0x91C27503u, 0xF8F5EC47u, 0x2A9ADECFu,
		0x5535BD9Eu, 0xAA6B7B3Cu, 0x8FA7F039u, 0xC43EE633u, 0x530CCA27u,
		0xA619944Eu, 0x97422EDDu, 0xF5F55BFBu, 0x309BB1B7u, 0x6137636Eu,
		0xC26EC6DCu, 0x5FAC8BF9u, 0xBF5917F2u, 0xA5C329A5u, 0x90F7550Bu,
		0xFA9FAC57u, 0x2E4E5EEFu
	},
	{
		0x695186A7u, 0xD2A30D4Eu, 0x7E371CDDu, 0xFC6E39BAu, 0x23AD7535u,
		0x475AEA6Au, 0x8EB5D4D4u, 0xC61AAFE9u, 0x57445993u, 0xAE88B326u,
		0x8660600Du, 0xD7B1C65Bu, 0x74128AF7u, 0xE82515EEu, 0x0B3B2D9Du,
		0x16765B3Au, 0x2CECB674u, 0x59D96CE8u, 0xB3B2D9D0u, 0xBC14B5E1u,
		0xA3586D83u, 0x9DC1DD47u, 0xE0F2BCCFu, 0x1A947FDFu, 0x3528FFBEu,
		0x6A51FF7Cu, 0xD4A3FEF8u, 0x7236FBB1u, 0xE46DF762u, 0x13AAE885u,
		0x2755D10Au, 0x4EABA214u
	},
	{
		0x66BC001Eu, 0xCD78003Cu, 0x41810639u, 0x83020C72u, 0xDD751EA5u,
		0x619B3B0Bu, 0xC3367616u, 0x5D1DEA6Du, 0xBA3BD4DAu, 0xAF06AFF5u,
		0x857C59ABu, 0xD189B517u, 0x78626C6Fu, 0xF0C4D8DEu, 0x3AF8B7FDu,
		0x75F16FFAu, 0xEBE2DFF4u, 0x0CB4B9A9u, 0x19697352u, 0x32D2E6A4u,
		0x65A5CD48u, 0xCB4B9A90u, 0x4DE63361u, 0x9BCC66C2u, 0xECE9CBC5u,
		0x02A291CBu, 0x05452396u, 0x0A8A472Cu, 0x15148E58u, 0x2A291CB0u,
		0x54523960u, 0xA8A472C0u
	},
	{
		0xB58B27B3u, 0xB0674927u, 0xBBBF940Fu, 0xAC0E2E5Fu, 0x836D5AFFu,
		0xDDABB3BFu, 0x6026613Fu, 0xC04CC27Eu, 0x5BE882BDu, 0xB7D1057Au,
		0xB4D30CB5u, 0xB2D71F2Bu, 0xBEDF3817u, 0xA6CF766Fu, 0x96EFEA9Fu,
		0xF6AED37Fu, 0x362CA0BFu, 0x6C59417Eu, 0xD8B282FCu, 0x6A1403B9u,
		0xD4280772u, 0x732108A5u, 0xE642114Au, 0x17F524D5u, 0x2FEA49AAu,
		0x5FD49354u, 0xBFA926A8u, 0xA4234B11u, 0x93379063u, 0xFD1E2687u,
		0x214D4B4Fu, 0x429A969Eu
	},
	{
		0xFE273162u, 0x273F6485u, 0x4E7EC90Au, 0x9CFD9214u, 0xE28A2269u,
		0x1E654293u, 0x3CCA8526u, 0x79950A4Cu, 0xF32A1498u, 0x3D252F71u,
		0x7A4A5EE2u, 0xF494BDC4u, 0x32587DC9u, 0x64B0FB92u, 0xC961F724u,
		0x49B2E809u, 0x9365D012u, 0xFDBAA665u, 0x20044A8Bu, 0x40089516u,
		0x80112A2Cu, 0xDB535219u, 0x6DD7A273u, 0xDBAF44E6u, 0x6C2F8F8Du,
		0xD85F1F1Au, 0x6BCF3875u, 0xD79E70EAu, 0x744DE795u, 0xE89BCF2Au,
		0x0A469815u, 0x148D302Au
	},
	{
		0xD3C98813u, 0x7CE21667u, 0xF9C42CCEu, 0x28F95FDDu, 0x51F2BFBAu,
		0xA3E57F74u, 0x9CBBF8A9u, 0xE206F713u, 0x1F7CE867u, 0x3EF9D0CEu,
		0x7DF3A19Cu, 0xFBE74338u, 0x2CBF8031u, 0x597F0062u, 0xB2FE00C4u,
		0xBE8D07C9u, 0xA66B09D3u, 0x97A715E7u, 0xF43F2D8Fu, 0x330F5D5Fu,
		0x661EBABEu, 0xCC3D757Cu, 0x430BECB9u, 0x8617D972u, 0xD75EB4A5u,
		0x75CC6F0Bu, 0xEB98DE16u, 0x0C40BA6Du, 0x188174DAu, 0x3102E9B4u,
		0x6205D368u, 0xC40BA6D0u
	},
	{
		0xF7D6DEB4u, 0x34DCBB29u, 0x69B97652u, 0xD372ECA4u, 0x7D94DF09u,
		0xFB29BE12u, 0x2D227A65u, 0x5A44F4CAu, 0xB489E994u, 0xB262D569u,
		0xBFB4AC93u, 0xA4185F67u, 0x9341B88Fu, 0xFDF2775Fu, 0x2095E8FFu,
		0x412BD1FEu, 0x8257A3FCu, 0xDFDE41B9u, 0x64CD8533u, 0xC99B0A66u,
		0x4847128Du, 0x908E251Au, 0xFA6D4C75u, 0x2FAB9EABu, 0x5F573D56u,
		0xBEAE7AACu, 0xA62DF319u, 0x972AE073u, 0xF524C6A7u, 0x31388B0Fu,
		0x6271161Eu, 0xC4E22C3Cu
	},
	{
		0xEDB88320u, 0x00000001u, 0x00000002u, 0x00000004u, 0x00000008u,
		0x00000010u, 0x00000020u, 0x00000040u, 0x00000080u, 0x00000100u,
		0x00000200u, 0x00000400u, 0x00000800u, 0x00001000u, 0x00002000u,
		0x00004000u, 0x00008000u, 0x00010000u, 0x00020000u, 0x00040000u,
		0x00080000u, 0x00100000u, 0x00200000u, 0x00400000u, 0x00800000u,
		0x01000000u, 0x02000000u, 0x04000000u, 0x08000000u, 0x10000000u,
		0x20000000u, 0x40000000u
	},
	{
		0x76DC4190u, 0xEDB88320u, 0x00000001u, 0x00000002u, 0x00000004u,
		0x00000008u, 0x00000010u, 0x00000020u, 0x00000040u, 0x00000080u,
		0x00000100u, 0x00000200u, 0x00000400u, 0x00000800u, 0x00001000u,
		0x00002000u, 0x00004000u, 0x00008000u, 0x00010000u, 0x00020000u,
		0x00040000u, 0x00080000u, 0x00100000u, 0x00200000u, 0x00400000u,
		0x00800000u, 0x01000000u, 0x02000000u, 0x04000000u, 0x08000000u,
		0x10000000u, 0x20000000u
	},
	{
		0x1DB71064u, 0x3B6E20C8u, 0x76DC4190u, 0xEDB88320u, 0x00000001u,
		0x00000002u, 0x00000004u, 0x00000008u, 0x00000010u, 0x00000020u,
		0x00000040u, 0x00000080u, 0x00000100u, 0x00000200u, 0x00000400u,
		0x00000800u, 0x00001000u, 0x00002000u, 0x00004000u, 0x00008000u,
		0x00010000u, 0x00020000u, 0x00040000u, 0x00080000u, 0x00100000u,
		0x00200000u, 0x00400000u, 0x00800000u, 0x01000000u, 0x02000000u,
		0x04000000u, 0x08000000u
	}
};

static const uint32 (*crc32_combinetable)[32] = crc32_combinetable_;
