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


#define ZSTRM_MODEMASK 0x000f0000
#define ZSTRM_TYPEMASK 0x00f00000

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
		uint32 f;

		f = flags & 0xff00;
		zstrm->defltr = NULL;
		zstrm->infltr = inflator_create(f, allctr);
		if (zstrm->infltr == NULL) {
			zstrm_destroy(&zstrm->public);
			return NULL;
		}
	}
	if (smode == ZSTRM_DEFLATE) {
		uint32 f;

		f = flags & 0x00ff;
		zstrm->infltr = NULL;
		zstrm->defltr = deflator_create(f, level, allctr);
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
	if (n) {
		deflator_setsrc(defltr, source, n);
	}

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
zstrm_flush(const TZStrm* state, uint32 final)
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
zstrm_adler32update(uint32 chcksm, const uint8* data, uintxx size)
{
	CTB_ASSERT(data);
	return zstrm_adler32updateASM(chcksm, data, size);
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
zstrm_adler32update(uint32 chcksm, const uint8* data, uintxx size)
{
	uint32 a;
	uint32 b;
	uint32 ra;
	uint32 rb;
	uintxx i;
	CTB_ASSERT(data);

	a = 0xffffu & (chcksm);
	b = 0xffffu & (chcksm >> 16);

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
zstrm_crc32update(uint32 chcksm, const uint8* data, uintxx size)
{
	CTB_ASSERT(data);
	return zstrm_crc32updateASM(chcksm, data, size);
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
zstrm_crc32update(uint32 chcksm, const uint8* data, uintxx size)
{
	const uint32* ptr32;
	uint32 rg1;
	uint32 rg2;
	uint32 crc;
	CTB_ASSERT(data);

	crc = chcksm;
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
zstrm_crc32update(uint32 chcksm, const uint8* data, uintxx size)
{
	const uint32* ptr32;
	uint32 crc;
	CTB_ASSERT(data);

	crc = chcksm;
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


/* ****************************************************************************
 * CRC32 Lookup Tables
 *************************************************************************** */

#if defined(CRC32TABLES)

static const uint32 crc32_table_[8][256] =
{
	{
		0X00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419,
		0X706AF48F, 0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4,
		0XE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07,
		0X90BF1D91, 0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
		0X1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7, 0x136C9856,
		0X646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
		0XFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4,
		0XA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
		0X35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3,
		0X45DF5C75, 0xDCD60DCF, 0xABD13D59, 0x26D930AC, 0x51DE003A,
		0XC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599,
		0XB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
		0X2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190,
		0X01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F,
		0X9FBFE4A5, 0xE8B8D433, 0x7807C9A2, 0x0F00F934, 0x9609A88E,
		0XE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
		0X6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED,
		0X1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
		0X8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3,
		0XFBD44C65, 0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
		0X4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A,
		0X346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5,
		0XAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA, 0xBE0B1010,
		0XC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
		0X5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17,
		0X2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6,
		0X03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615,
		0X73DC1683, 0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
		0XE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1, 0xF00F9344,
		0X8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
		0X196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A,
		0X67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
		0XD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1,
		0XA6BC5767, 0x3FB506DD, 0x48B2364B, 0xD80D2BDA, 0xAF0A1B4C,
		0X36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF,
		0X4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
		0XCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE,
		0XB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31,
		0X2CD99E8B, 0x5BDEAE1D, 0x9B64C2B0, 0xEC63F226, 0x756AA39C,
		0X026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
		0X95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B,
		0XE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
		0X68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1,
		0X18B74777, 0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
		0X8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45, 0xA00AE278,
		0XD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7,
		0X4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC, 0x40DF0B66,
		0X37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
		0XBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605,
		0XCDD70693, 0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8,
		0X5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B,
		0X2D02EF8Du
	},
	{
		0X00000000, 0x191B3141, 0x32366282, 0x2B2D53C3, 0x646CC504,
		0X7D77F445, 0x565AA786, 0x4F4196C7, 0xC8D98A08, 0xD1C2BB49,
		0XFAEFE88A, 0xE3F4D9CB, 0xACB54F0C, 0xB5AE7E4D, 0x9E832D8E,
		0X87981CCF, 0x4AC21251, 0x53D92310, 0x78F470D3, 0x61EF4192,
		0X2EAED755, 0x37B5E614, 0x1C98B5D7, 0x05838496, 0x821B9859,
		0X9B00A918, 0xB02DFADB, 0xA936CB9A, 0xE6775D5D, 0xFF6C6C1C,
		0XD4413FDF, 0xCD5A0E9E, 0x958424A2, 0x8C9F15E3, 0xA7B24620,
		0XBEA97761, 0xF1E8E1A6, 0xE8F3D0E7, 0xC3DE8324, 0xDAC5B265,
		0X5D5DAEAA, 0x44469FEB, 0x6F6BCC28, 0x7670FD69, 0x39316BAE,
		0X202A5AEF, 0x0B07092C, 0x121C386D, 0xDF4636F3, 0xC65D07B2,
		0XED705471, 0xF46B6530, 0xBB2AF3F7, 0xA231C2B6, 0x891C9175,
		0X9007A034, 0x179FBCFB, 0x0E848DBA, 0x25A9DE79, 0x3CB2EF38,
		0X73F379FF, 0x6AE848BE, 0x41C51B7D, 0x58DE2A3C, 0xF0794F05,
		0XE9627E44, 0xC24F2D87, 0xDB541CC6, 0x94158A01, 0x8D0EBB40,
		0XA623E883, 0xBF38D9C2, 0x38A0C50D, 0x21BBF44C, 0x0A96A78F,
		0X138D96CE, 0x5CCC0009, 0x45D73148, 0x6EFA628B, 0x77E153CA,
		0XBABB5D54, 0xA3A06C15, 0x888D3FD6, 0x91960E97, 0xDED79850,
		0XC7CCA911, 0xECE1FAD2, 0xF5FACB93, 0x7262D75C, 0x6B79E61D,
		0X4054B5DE, 0x594F849F, 0x160E1258, 0x0F152319, 0x243870DA,
		0X3D23419B, 0x65FD6BA7, 0x7CE65AE6, 0x57CB0925, 0x4ED03864,
		0X0191AEA3, 0x188A9FE2, 0x33A7CC21, 0x2ABCFD60, 0xAD24E1AF,
		0XB43FD0EE, 0x9F12832D, 0x8609B26C, 0xC94824AB, 0xD05315EA,
		0XFB7E4629, 0xE2657768, 0x2F3F79F6, 0x362448B7, 0x1D091B74,
		0X04122A35, 0x4B53BCF2, 0x52488DB3, 0x7965DE70, 0x607EEF31,
		0XE7E6F3FE, 0xFEFDC2BF, 0xD5D0917C, 0xCCCBA03D, 0x838A36FA,
		0X9A9107BB, 0xB1BC5478, 0xA8A76539, 0x3B83984B, 0x2298A90A,
		0X09B5FAC9, 0x10AECB88, 0x5FEF5D4F, 0x46F46C0E, 0x6DD93FCD,
		0X74C20E8C, 0xF35A1243, 0xEA412302, 0xC16C70C1, 0xD8774180,
		0X9736D747, 0x8E2DE606, 0xA500B5C5, 0xBC1B8484, 0x71418A1A,
		0X685ABB5B, 0x4377E898, 0x5A6CD9D9, 0x152D4F1E, 0x0C367E5F,
		0X271B2D9C, 0x3E001CDD, 0xB9980012, 0xA0833153, 0x8BAE6290,
		0X92B553D1, 0xDDF4C516, 0xC4EFF457, 0xEFC2A794, 0xF6D996D5,
		0XAE07BCE9, 0xB71C8DA8, 0x9C31DE6B, 0x852AEF2A, 0xCA6B79ED,
		0XD37048AC, 0xF85D1B6F, 0xE1462A2E, 0x66DE36E1, 0x7FC507A0,
		0X54E85463, 0x4DF36522, 0x02B2F3E5, 0x1BA9C2A4, 0x30849167,
		0X299FA026, 0xE4C5AEB8, 0xFDDE9FF9, 0xD6F3CC3A, 0xCFE8FD7B,
		0X80A96BBC, 0x99B25AFD, 0xB29F093E, 0xAB84387F, 0x2C1C24B0,
		0X350715F1, 0x1E2A4632, 0x07317773, 0x4870E1B4, 0x516BD0F5,
		0X7A468336, 0x635DB277, 0xCBFAD74E, 0xD2E1E60F, 0xF9CCB5CC,
		0XE0D7848D, 0xAF96124A, 0xB68D230B, 0x9DA070C8, 0x84BB4189,
		0X03235D46, 0x1A386C07, 0x31153FC4, 0x280E0E85, 0x674F9842,
		0X7E54A903, 0x5579FAC0, 0x4C62CB81, 0x8138C51F, 0x9823F45E,
		0XB30EA79D, 0xAA1596DC, 0xE554001B, 0xFC4F315A, 0xD7626299,
		0XCE7953D8, 0x49E14F17, 0x50FA7E56, 0x7BD72D95, 0x62CC1CD4,
		0X2D8D8A13, 0x3496BB52, 0x1FBBE891, 0x06A0D9D0, 0x5E7EF3EC,
		0X4765C2AD, 0x6C48916E, 0x7553A02F, 0x3A1236E8, 0x230907A9,
		0X0824546A, 0x113F652B, 0x96A779E4, 0x8FBC48A5, 0xA4911B66,
		0XBD8A2A27, 0xF2CBBCE0, 0xEBD08DA1, 0xC0FDDE62, 0xD9E6EF23,
		0X14BCE1BD, 0x0DA7D0FC, 0x268A833F, 0x3F91B27E, 0x70D024B9,
		0X69CB15F8, 0x42E6463B, 0x5BFD777A, 0xDC656BB5, 0xC57E5AF4,
		0XEE530937, 0xF7483876, 0xB809AEB1, 0xA1129FF0, 0x8A3FCC33,
		0X9324FD72u
	},
	{
		0X00000000, 0x01C26A37, 0x0384D46E, 0x0246BE59, 0x0709A8DC,
		0X06CBC2EB, 0x048D7CB2, 0x054F1685, 0x0E1351B8, 0x0FD13B8F,
		0X0D9785D6, 0x0C55EFE1, 0x091AF964, 0x08D89353, 0x0A9E2D0A,
		0X0B5C473D, 0x1C26A370, 0x1DE4C947, 0x1FA2771E, 0x1E601D29,
		0X1B2F0BAC, 0x1AED619B, 0x18ABDFC2, 0x1969B5F5, 0x1235F2C8,
		0X13F798FF, 0x11B126A6, 0x10734C91, 0x153C5A14, 0x14FE3023,
		0X16B88E7A, 0x177AE44D, 0x384D46E0, 0x398F2CD7, 0x3BC9928E,
		0X3A0BF8B9, 0x3F44EE3C, 0x3E86840B, 0x3CC03A52, 0x3D025065,
		0X365E1758, 0x379C7D6F, 0x35DAC336, 0x3418A901, 0x3157BF84,
		0X3095D5B3, 0x32D36BEA, 0x331101DD, 0x246BE590, 0x25A98FA7,
		0X27EF31FE, 0x262D5BC9, 0x23624D4C, 0x22A0277B, 0x20E69922,
		0X2124F315, 0x2A78B428, 0x2BBADE1F, 0x29FC6046, 0x283E0A71,
		0X2D711CF4, 0x2CB376C3, 0x2EF5C89A, 0x2F37A2AD, 0x709A8DC0,
		0X7158E7F7, 0x731E59AE, 0x72DC3399, 0x7793251C, 0x76514F2B,
		0X7417F172, 0x75D59B45, 0x7E89DC78, 0x7F4BB64F, 0x7D0D0816,
		0X7CCF6221, 0x798074A4, 0x78421E93, 0x7A04A0CA, 0x7BC6CAFD,
		0X6CBC2EB0, 0x6D7E4487, 0x6F38FADE, 0x6EFA90E9, 0x6BB5866C,
		0X6A77EC5B, 0x68315202, 0x69F33835, 0x62AF7F08, 0x636D153F,
		0X612BAB66, 0x60E9C151, 0x65A6D7D4, 0x6464BDE3, 0x662203BA,
		0X67E0698D, 0x48D7CB20, 0x4915A117, 0x4B531F4E, 0x4A917579,
		0X4FDE63FC, 0x4E1C09CB, 0x4C5AB792, 0x4D98DDA5, 0x46C49A98,
		0X4706F0AF, 0x45404EF6, 0x448224C1, 0x41CD3244, 0x400F5873,
		0X4249E62A, 0x438B8C1D, 0x54F16850, 0x55330267, 0x5775BC3E,
		0X56B7D609, 0x53F8C08C, 0x523AAABB, 0x507C14E2, 0x51BE7ED5,
		0X5AE239E8, 0x5B2053DF, 0x5966ED86, 0x58A487B1, 0x5DEB9134,
		0X5C29FB03, 0x5E6F455A, 0x5FAD2F6D, 0xE1351B80, 0xE0F771B7,
		0XE2B1CFEE, 0xE373A5D9, 0xE63CB35C, 0xE7FED96B, 0xE5B86732,
		0XE47A0D05, 0xEF264A38, 0xEEE4200F, 0xECA29E56, 0xED60F461,
		0XE82FE2E4, 0xE9ED88D3, 0xEBAB368A, 0xEA695CBD, 0xFD13B8F0,
		0XFCD1D2C7, 0xFE976C9E, 0xFF5506A9, 0xFA1A102C, 0xFBD87A1B,
		0XF99EC442, 0xF85CAE75, 0xF300E948, 0xF2C2837F, 0xF0843D26,
		0XF1465711, 0xF4094194, 0xF5CB2BA3, 0xF78D95FA, 0xF64FFFCD,
		0XD9785D60, 0xD8BA3757, 0xDAFC890E, 0xDB3EE339, 0xDE71F5BC,
		0XDFB39F8B, 0xDDF521D2, 0xDC374BE5, 0xD76B0CD8, 0xD6A966EF,
		0XD4EFD8B6, 0xD52DB281, 0xD062A404, 0xD1A0CE33, 0xD3E6706A,
		0XD2241A5D, 0xC55EFE10, 0xC49C9427, 0xC6DA2A7E, 0xC7184049,
		0XC25756CC, 0xC3953CFB, 0xC1D382A2, 0xC011E895, 0xCB4DAFA8,
		0XCA8FC59F, 0xC8C97BC6, 0xC90B11F1, 0xCC440774, 0xCD866D43,
		0XCFC0D31A, 0xCE02B92D, 0x91AF9640, 0x906DFC77, 0x922B422E,
		0X93E92819, 0x96A63E9C, 0x976454AB, 0x9522EAF2, 0x94E080C5,
		0X9FBCC7F8, 0x9E7EADCF, 0x9C381396, 0x9DFA79A1, 0x98B56F24,
		0X99770513, 0x9B31BB4A, 0x9AF3D17D, 0x8D893530, 0x8C4B5F07,
		0X8E0DE15E, 0x8FCF8B69, 0x8A809DEC, 0x8B42F7DB, 0x89044982,
		0X88C623B5, 0x839A6488, 0x82580EBF, 0x801EB0E6, 0x81DCDAD1,
		0X8493CC54, 0x8551A663, 0x8717183A, 0x86D5720D, 0xA9E2D0A0,
		0XA820BA97, 0xAA6604CE, 0xABA46EF9, 0xAEEB787C, 0xAF29124B,
		0XAD6FAC12, 0xACADC625, 0xA7F18118, 0xA633EB2F, 0xA4755576,
		0XA5B73F41, 0xA0F829C4, 0xA13A43F3, 0xA37CFDAA, 0xA2BE979D,
		0XB5C473D0, 0xB40619E7, 0xB640A7BE, 0xB782CD89, 0xB2CDDB0C,
		0XB30FB13B, 0xB1490F62, 0xB08B6555, 0xBBD72268, 0xBA15485F,
		0XB853F606, 0xB9919C31, 0xBCDE8AB4, 0xBD1CE083, 0xBF5A5EDA,
		0XBE9834EDu
	},
	{
		0X00000000, 0xB8BC6765, 0xAA09C88B, 0x12B5AFEE, 0x8F629757,
		0X37DEF032, 0x256B5FDC, 0x9DD738B9, 0xC5B428EF, 0x7D084F8A,
		0X6FBDE064, 0xD7018701, 0x4AD6BFB8, 0xF26AD8DD, 0xE0DF7733,
		0X58631056, 0x5019579F, 0xE8A530FA, 0xFA109F14, 0x42ACF871,
		0XDF7BC0C8, 0x67C7A7AD, 0x75720843, 0xCDCE6F26, 0x95AD7F70,
		0X2D111815, 0x3FA4B7FB, 0x8718D09E, 0x1ACFE827, 0xA2738F42,
		0XB0C620AC, 0x087A47C9, 0xA032AF3E, 0x188EC85B, 0x0A3B67B5,
		0XB28700D0, 0x2F503869, 0x97EC5F0C, 0x8559F0E2, 0x3DE59787,
		0X658687D1, 0xDD3AE0B4, 0xCF8F4F5A, 0x7733283F, 0xEAE41086,
		0X525877E3, 0x40EDD80D, 0xF851BF68, 0xF02BF8A1, 0x48979FC4,
		0X5A22302A, 0xE29E574F, 0x7F496FF6, 0xC7F50893, 0xD540A77D,
		0X6DFCC018, 0x359FD04E, 0x8D23B72B, 0x9F9618C5, 0x272A7FA0,
		0XBAFD4719, 0x0241207C, 0x10F48F92, 0xA848E8F7, 0x9B14583D,
		0X23A83F58, 0x311D90B6, 0x89A1F7D3, 0x1476CF6A, 0xACCAA80F,
		0XBE7F07E1, 0x06C36084, 0x5EA070D2, 0xE61C17B7, 0xF4A9B859,
		0X4C15DF3C, 0xD1C2E785, 0x697E80E0, 0x7BCB2F0E, 0xC377486B,
		0XCB0D0FA2, 0x73B168C7, 0x6104C729, 0xD9B8A04C, 0x446F98F5,
		0XFCD3FF90, 0xEE66507E, 0x56DA371B, 0x0EB9274D, 0xB6054028,
		0XA4B0EFC6, 0x1C0C88A3, 0x81DBB01A, 0x3967D77F, 0x2BD27891,
		0X936E1FF4, 0x3B26F703, 0x839A9066, 0x912F3F88, 0x299358ED,
		0XB4446054, 0x0CF80731, 0x1E4DA8DF, 0xA6F1CFBA, 0xFE92DFEC,
		0X462EB889, 0x549B1767, 0xEC277002, 0x71F048BB, 0xC94C2FDE,
		0XDBF98030, 0x6345E755, 0x6B3FA09C, 0xD383C7F9, 0xC1366817,
		0X798A0F72, 0xE45D37CB, 0x5CE150AE, 0x4E54FF40, 0xF6E89825,
		0XAE8B8873, 0x1637EF16, 0x048240F8, 0xBC3E279D, 0x21E91F24,
		0X99557841, 0x8BE0D7AF, 0x335CB0CA, 0xED59B63B, 0x55E5D15E,
		0X47507EB0, 0xFFEC19D5, 0x623B216C, 0xDA874609, 0xC832E9E7,
		0X708E8E82, 0x28ED9ED4, 0x9051F9B1, 0x82E4565F, 0x3A58313A,
		0XA78F0983, 0x1F336EE6, 0x0D86C108, 0xB53AA66D, 0xBD40E1A4,
		0X05FC86C1, 0x1749292F, 0xAFF54E4A, 0x322276F3, 0x8A9E1196,
		0X982BBE78, 0x2097D91D, 0x78F4C94B, 0xC048AE2E, 0xD2FD01C0,
		0X6A4166A5, 0xF7965E1C, 0x4F2A3979, 0x5D9F9697, 0xE523F1F2,
		0X4D6B1905, 0xF5D77E60, 0xE762D18E, 0x5FDEB6EB, 0xC2098E52,
		0X7AB5E937, 0x680046D9, 0xD0BC21BC, 0x88DF31EA, 0x3063568F,
		0X22D6F961, 0x9A6A9E04, 0x07BDA6BD, 0xBF01C1D8, 0xADB46E36,
		0X15080953, 0x1D724E9A, 0xA5CE29FF, 0xB77B8611, 0x0FC7E174,
		0X9210D9CD, 0x2AACBEA8, 0x38191146, 0x80A57623, 0xD8C66675,
		0X607A0110, 0x72CFAEFE, 0xCA73C99B, 0x57A4F122, 0xEF189647,
		0XFDAD39A9, 0x45115ECC, 0x764DEE06, 0xCEF18963, 0xDC44268D,
		0X64F841E8, 0xF92F7951, 0x41931E34, 0x5326B1DA, 0xEB9AD6BF,
		0XB3F9C6E9, 0x0B45A18C, 0x19F00E62, 0xA14C6907, 0x3C9B51BE,
		0X842736DB, 0x96929935, 0x2E2EFE50, 0x2654B999, 0x9EE8DEFC,
		0X8C5D7112, 0x34E11677, 0xA9362ECE, 0x118A49AB, 0x033FE645,
		0XBB838120, 0xE3E09176, 0x5B5CF613, 0x49E959FD, 0xF1553E98,
		0X6C820621, 0xD43E6144, 0xC68BCEAA, 0x7E37A9CF, 0xD67F4138,
		0X6EC3265D, 0x7C7689B3, 0xC4CAEED6, 0x591DD66F, 0xE1A1B10A,
		0XF3141EE4, 0x4BA87981, 0x13CB69D7, 0xAB770EB2, 0xB9C2A15C,
		0X017EC639, 0x9CA9FE80, 0x241599E5, 0x36A0360B, 0x8E1C516E,
		0X866616A7, 0x3EDA71C2, 0x2C6FDE2C, 0x94D3B949, 0x090481F0,
		0XB1B8E695, 0xA30D497B, 0x1BB12E1E, 0x43D23E48, 0xFB6E592D,
		0XE9DBF6C3, 0x516791A6, 0xCCB0A91F, 0x740CCE7A, 0x66B96194,
		0XDE0506F1u
	},
	{
		0X00000000, 0x3D6029B0, 0x7AC05360, 0x47A07AD0, 0xF580A6C0,
		0XC8E08F70, 0x8F40F5A0, 0xB220DC10, 0x30704BC1, 0x0D106271,
		0X4AB018A1, 0x77D03111, 0xC5F0ED01, 0xF890C4B1, 0xBF30BE61,
		0X825097D1, 0x60E09782, 0x5D80BE32, 0x1A20C4E2, 0x2740ED52,
		0X95603142, 0xA80018F2, 0xEFA06222, 0xD2C04B92, 0x5090DC43,
		0X6DF0F5F3, 0x2A508F23, 0x1730A693, 0xA5107A83, 0x98705333,
		0XDFD029E3, 0xE2B00053, 0xC1C12F04, 0xFCA106B4, 0xBB017C64,
		0X866155D4, 0x344189C4, 0x0921A074, 0x4E81DAA4, 0x73E1F314,
		0XF1B164C5, 0xCCD14D75, 0x8B7137A5, 0xB6111E15, 0x0431C205,
		0X3951EBB5, 0x7EF19165, 0x4391B8D5, 0xA121B886, 0x9C419136,
		0XDBE1EBE6, 0xE681C256, 0x54A11E46, 0x69C137F6, 0x2E614D26,
		0X13016496, 0x9151F347, 0xAC31DAF7, 0xEB91A027, 0xD6F18997,
		0X64D15587, 0x59B17C37, 0x1E1106E7, 0x23712F57, 0x58F35849,
		0X659371F9, 0x22330B29, 0x1F532299, 0xAD73FE89, 0x9013D739,
		0XD7B3ADE9, 0xEAD38459, 0x68831388, 0x55E33A38, 0x124340E8,
		0X2F236958, 0x9D03B548, 0xA0639CF8, 0xE7C3E628, 0xDAA3CF98,
		0X3813CFCB, 0x0573E67B, 0x42D39CAB, 0x7FB3B51B, 0xCD93690B,
		0XF0F340BB, 0xB7533A6B, 0x8A3313DB, 0x0863840A, 0x3503ADBA,
		0X72A3D76A, 0x4FC3FEDA, 0xFDE322CA, 0xC0830B7A, 0x872371AA,
		0XBA43581A, 0x9932774D, 0xA4525EFD, 0xE3F2242D, 0xDE920D9D,
		0X6CB2D18D, 0x51D2F83D, 0x167282ED, 0x2B12AB5D, 0xA9423C8C,
		0X9422153C, 0xD3826FEC, 0xEEE2465C, 0x5CC29A4C, 0x61A2B3FC,
		0X2602C92C, 0x1B62E09C, 0xF9D2E0CF, 0xC4B2C97F, 0x8312B3AF,
		0XBE729A1F, 0x0C52460F, 0x31326FBF, 0x7692156F, 0x4BF23CDF,
		0XC9A2AB0E, 0xF4C282BE, 0xB362F86E, 0x8E02D1DE, 0x3C220DCE,
		0X0142247E, 0x46E25EAE, 0x7B82771E, 0xB1E6B092, 0x8C869922,
		0XCB26E3F2, 0xF646CA42, 0x44661652, 0x79063FE2, 0x3EA64532,
		0X03C66C82, 0x8196FB53, 0xBCF6D2E3, 0xFB56A833, 0xC6368183,
		0X74165D93, 0x49767423, 0x0ED60EF3, 0x33B62743, 0xD1062710,
		0XEC660EA0, 0xABC67470, 0x96A65DC0, 0x248681D0, 0x19E6A860,
		0X5E46D2B0, 0x6326FB00, 0xE1766CD1, 0xDC164561, 0x9BB63FB1,
		0XA6D61601, 0x14F6CA11, 0x2996E3A1, 0x6E369971, 0x5356B0C1,
		0X70279F96, 0x4D47B626, 0x0AE7CCF6, 0x3787E546, 0x85A73956,
		0XB8C710E6, 0xFF676A36, 0xC2074386, 0x4057D457, 0x7D37FDE7,
		0X3A978737, 0x07F7AE87, 0xB5D77297, 0x88B75B27, 0xCF1721F7,
		0XF2770847, 0x10C70814, 0x2DA721A4, 0x6A075B74, 0x576772C4,
		0XE547AED4, 0xD8278764, 0x9F87FDB4, 0xA2E7D404, 0x20B743D5,
		0X1DD76A65, 0x5A7710B5, 0x67173905, 0xD537E515, 0xE857CCA5,
		0XAFF7B675, 0x92979FC5, 0xE915E8DB, 0xD475C16B, 0x93D5BBBB,
		0XAEB5920B, 0x1C954E1B, 0x21F567AB, 0x66551D7B, 0x5B3534CB,
		0XD965A31A, 0xE4058AAA, 0xA3A5F07A, 0x9EC5D9CA, 0x2CE505DA,
		0X11852C6A, 0x562556BA, 0x6B457F0A, 0x89F57F59, 0xB49556E9,
		0XF3352C39, 0xCE550589, 0x7C75D999, 0x4115F029, 0x06B58AF9,
		0X3BD5A349, 0xB9853498, 0x84E51D28, 0xC34567F8, 0xFE254E48,
		0X4C059258, 0x7165BBE8, 0x36C5C138, 0x0BA5E888, 0x28D4C7DF,
		0X15B4EE6F, 0x521494BF, 0x6F74BD0F, 0xDD54611F, 0xE03448AF,
		0XA794327F, 0x9AF41BCF, 0x18A48C1E, 0x25C4A5AE, 0x6264DF7E,
		0X5F04F6CE, 0xED242ADE, 0xD044036E, 0x97E479BE, 0xAA84500E,
		0X4834505D, 0x755479ED, 0x32F4033D, 0x0F942A8D, 0xBDB4F69D,
		0X80D4DF2D, 0xC774A5FD, 0xFA148C4D, 0x78441B9C, 0x4524322C,
		0X028448FC, 0x3FE4614C, 0x8DC4BD5C, 0xB0A494EC, 0xF704EE3C,
		0XCA64C78Cu
	},
	{
		0X00000000, 0xCB5CD3A5, 0x4DC8A10B, 0x869472AE, 0x9B914216,
		0X50CD91B3, 0xD659E31D, 0x1D0530B8, 0xEC53826D, 0x270F51C8,
		0XA19B2366, 0x6AC7F0C3, 0x77C2C07B, 0xBC9E13DE, 0x3A0A6170,
		0XF156B2D5, 0x03D6029B, 0xC88AD13E, 0x4E1EA390, 0x85427035,
		0X9847408D, 0x531B9328, 0xD58FE186, 0x1ED33223, 0xEF8580F6,
		0X24D95353, 0xA24D21FD, 0x6911F258, 0x7414C2E0, 0xBF481145,
		0X39DC63EB, 0xF280B04E, 0x07AC0536, 0xCCF0D693, 0x4A64A43D,
		0X81387798, 0x9C3D4720, 0x57619485, 0xD1F5E62B, 0x1AA9358E,
		0XEBFF875B, 0x20A354FE, 0xA6372650, 0x6D6BF5F5, 0x706EC54D,
		0XBB3216E8, 0x3DA66446, 0xF6FAB7E3, 0x047A07AD, 0xCF26D408,
		0X49B2A6A6, 0x82EE7503, 0x9FEB45BB, 0x54B7961E, 0xD223E4B0,
		0X197F3715, 0xE82985C0, 0x23755665, 0xA5E124CB, 0x6EBDF76E,
		0X73B8C7D6, 0xB8E41473, 0x3E7066DD, 0xF52CB578, 0x0F580A6C,
		0XC404D9C9, 0x4290AB67, 0x89CC78C2, 0x94C9487A, 0x5F959BDF,
		0XD901E971, 0x125D3AD4, 0xE30B8801, 0x28575BA4, 0xAEC3290A,
		0X659FFAAF, 0x789ACA17, 0xB3C619B2, 0x35526B1C, 0xFE0EB8B9,
		0X0C8E08F7, 0xC7D2DB52, 0x4146A9FC, 0x8A1A7A59, 0x971F4AE1,
		0X5C439944, 0xDAD7EBEA, 0x118B384F, 0xE0DD8A9A, 0x2B81593F,
		0XAD152B91, 0x6649F834, 0x7B4CC88C, 0xB0101B29, 0x36846987,
		0XFDD8BA22, 0x08F40F5A, 0xC3A8DCFF, 0x453CAE51, 0x8E607DF4,
		0X93654D4C, 0x58399EE9, 0xDEADEC47, 0x15F13FE2, 0xE4A78D37,
		0X2FFB5E92, 0xA96F2C3C, 0x6233FF99, 0x7F36CF21, 0xB46A1C84,
		0X32FE6E2A, 0xF9A2BD8F, 0x0B220DC1, 0xC07EDE64, 0x46EAACCA,
		0X8DB67F6F, 0x90B34FD7, 0x5BEF9C72, 0xDD7BEEDC, 0x16273D79,
		0XE7718FAC, 0x2C2D5C09, 0xAAB92EA7, 0x61E5FD02, 0x7CE0CDBA,
		0XB7BC1E1F, 0x31286CB1, 0xFA74BF14, 0x1EB014D8, 0xD5ECC77D,
		0X5378B5D3, 0x98246676, 0x852156CE, 0x4E7D856B, 0xC8E9F7C5,
		0X03B52460, 0xF2E396B5, 0x39BF4510, 0xBF2B37BE, 0x7477E41B,
		0X6972D4A3, 0xA22E0706, 0x24BA75A8, 0xEFE6A60D, 0x1D661643,
		0XD63AC5E6, 0x50AEB748, 0x9BF264ED, 0x86F75455, 0x4DAB87F0,
		0XCB3FF55E, 0x006326FB, 0xF135942E, 0x3A69478B, 0xBCFD3525,
		0X77A1E680, 0x6AA4D638, 0xA1F8059D, 0x276C7733, 0xEC30A496,
		0X191C11EE, 0xD240C24B, 0x54D4B0E5, 0x9F886340, 0x828D53F8,
		0X49D1805D, 0xCF45F2F3, 0x04192156, 0xF54F9383, 0x3E134026,
		0XB8873288, 0x73DBE12D, 0x6EDED195, 0xA5820230, 0x2316709E,
		0XE84AA33B, 0x1ACA1375, 0xD196C0D0, 0x5702B27E, 0x9C5E61DB,
		0X815B5163, 0x4A0782C6, 0xCC93F068, 0x07CF23CD, 0xF6999118,
		0X3DC542BD, 0xBB513013, 0x700DE3B6, 0x6D08D30E, 0xA65400AB,
		0X20C07205, 0xEB9CA1A0, 0x11E81EB4, 0xDAB4CD11, 0x5C20BFBF,
		0X977C6C1A, 0x8A795CA2, 0x41258F07, 0xC7B1FDA9, 0x0CED2E0C,
		0XFDBB9CD9, 0x36E74F7C, 0xB0733DD2, 0x7B2FEE77, 0x662ADECF,
		0XAD760D6A, 0x2BE27FC4, 0xE0BEAC61, 0x123E1C2F, 0xD962CF8A,
		0X5FF6BD24, 0x94AA6E81, 0x89AF5E39, 0x42F38D9C, 0xC467FF32,
		0X0F3B2C97, 0xFE6D9E42, 0x35314DE7, 0xB3A53F49, 0x78F9ECEC,
		0X65FCDC54, 0xAEA00FF1, 0x28347D5F, 0xE368AEFA, 0x16441B82,
		0XDD18C827, 0x5B8CBA89, 0x90D0692C, 0x8DD55994, 0x46898A31,
		0XC01DF89F, 0x0B412B3A, 0xFA1799EF, 0x314B4A4A, 0xB7DF38E4,
		0X7C83EB41, 0x6186DBF9, 0xAADA085C, 0x2C4E7AF2, 0xE712A957,
		0X15921919, 0xDECECABC, 0x585AB812, 0x93066BB7, 0x8E035B0F,
		0X455F88AA, 0xC3CBFA04, 0x089729A1, 0xF9C19B74, 0x329D48D1,
		0XB4093A7F, 0x7F55E9DA, 0x6250D962, 0xA90C0AC7, 0x2F987869,
		0XE4C4ABCCu
	},
	{
		0X00000000, 0xA6770BB4, 0x979F1129, 0x31E81A9D, 0xF44F2413,
		0X52382FA7, 0x63D0353A, 0xC5A73E8E, 0x33EF4E67, 0x959845D3,
		0XA4705F4E, 0x020754FA, 0xC7A06A74, 0x61D761C0, 0x503F7B5D,
		0XF64870E9, 0x67DE9CCE, 0xC1A9977A, 0xF0418DE7, 0x56368653,
		0X9391B8DD, 0x35E6B369, 0x040EA9F4, 0xA279A240, 0x5431D2A9,
		0XF246D91D, 0xC3AEC380, 0x65D9C834, 0xA07EF6BA, 0x0609FD0E,
		0X37E1E793, 0x9196EC27, 0xCFBD399C, 0x69CA3228, 0x582228B5,
		0XFE552301, 0x3BF21D8F, 0x9D85163B, 0xAC6D0CA6, 0x0A1A0712,
		0XFC5277FB, 0x5A257C4F, 0x6BCD66D2, 0xCDBA6D66, 0x081D53E8,
		0XAE6A585C, 0x9F8242C1, 0x39F54975, 0xA863A552, 0x0E14AEE6,
		0X3FFCB47B, 0x998BBFCF, 0x5C2C8141, 0xFA5B8AF5, 0xCBB39068,
		0X6DC49BDC, 0x9B8CEB35, 0x3DFBE081, 0x0C13FA1C, 0xAA64F1A8,
		0X6FC3CF26, 0xC9B4C492, 0xF85CDE0F, 0x5E2BD5BB, 0x440B7579,
		0XE27C7ECD, 0xD3946450, 0x75E36FE4, 0xB044516A, 0x16335ADE,
		0X27DB4043, 0x81AC4BF7, 0x77E43B1E, 0xD19330AA, 0xE07B2A37,
		0X460C2183, 0x83AB1F0D, 0x25DC14B9, 0x14340E24, 0xB2430590,
		0X23D5E9B7, 0x85A2E203, 0xB44AF89E, 0x123DF32A, 0xD79ACDA4,
		0X71EDC610, 0x4005DC8D, 0xE672D739, 0x103AA7D0, 0xB64DAC64,
		0X87A5B6F9, 0x21D2BD4D, 0xE47583C3, 0x42028877, 0x73EA92EA,
		0XD59D995E, 0x8BB64CE5, 0x2DC14751, 0x1C295DCC, 0xBA5E5678,
		0X7FF968F6, 0xD98E6342, 0xE86679DF, 0x4E11726B, 0xB8590282,
		0X1E2E0936, 0x2FC613AB, 0x89B1181F, 0x4C162691, 0xEA612D25,
		0XDB8937B8, 0x7DFE3C0C, 0xEC68D02B, 0x4A1FDB9F, 0x7BF7C102,
		0XDD80CAB6, 0x1827F438, 0xBE50FF8C, 0x8FB8E511, 0x29CFEEA5,
		0XDF879E4C, 0x79F095F8, 0x48188F65, 0xEE6F84D1, 0x2BC8BA5F,
		0X8DBFB1EB, 0xBC57AB76, 0x1A20A0C2, 0x8816EAF2, 0x2E61E146,
		0X1F89FBDB, 0xB9FEF06F, 0x7C59CEE1, 0xDA2EC555, 0xEBC6DFC8,
		0X4DB1D47C, 0xBBF9A495, 0x1D8EAF21, 0x2C66B5BC, 0x8A11BE08,
		0X4FB68086, 0xE9C18B32, 0xD82991AF, 0x7E5E9A1B, 0xEFC8763C,
		0X49BF7D88, 0x78576715, 0xDE206CA1, 0x1B87522F, 0xBDF0599B,
		0X8C184306, 0x2A6F48B2, 0xDC27385B, 0x7A5033EF, 0x4BB82972,
		0XEDCF22C6, 0x28681C48, 0x8E1F17FC, 0xBFF70D61, 0x198006D5,
		0X47ABD36E, 0xE1DCD8DA, 0xD034C247, 0x7643C9F3, 0xB3E4F77D,
		0X1593FCC9, 0x247BE654, 0x820CEDE0, 0x74449D09, 0xD23396BD,
		0XE3DB8C20, 0x45AC8794, 0x800BB91A, 0x267CB2AE, 0x1794A833,
		0XB1E3A387, 0x20754FA0, 0x86024414, 0xB7EA5E89, 0x119D553D,
		0XD43A6BB3, 0x724D6007, 0x43A57A9A, 0xE5D2712E, 0x139A01C7,
		0XB5ED0A73, 0x840510EE, 0x22721B5A, 0xE7D525D4, 0x41A22E60,
		0X704A34FD, 0xD63D3F49, 0xCC1D9F8B, 0x6A6A943F, 0x5B828EA2,
		0XFDF58516, 0x3852BB98, 0x9E25B02C, 0xAFCDAAB1, 0x09BAA105,
		0XFFF2D1EC, 0x5985DA58, 0x686DC0C5, 0xCE1ACB71, 0x0BBDF5FF,
		0XADCAFE4B, 0x9C22E4D6, 0x3A55EF62, 0xABC30345, 0x0DB408F1,
		0X3C5C126C, 0x9A2B19D8, 0x5F8C2756, 0xF9FB2CE2, 0xC813367F,
		0X6E643DCB, 0x982C4D22, 0x3E5B4696, 0x0FB35C0B, 0xA9C457BF,
		0X6C636931, 0xCA146285, 0xFBFC7818, 0x5D8B73AC, 0x03A0A617,
		0XA5D7ADA3, 0x943FB73E, 0x3248BC8A, 0xF7EF8204, 0x519889B0,
		0X6070932D, 0xC6079899, 0x304FE870, 0x9638E3C4, 0xA7D0F959,
		0X01A7F2ED, 0xC400CC63, 0x6277C7D7, 0x539FDD4A, 0xF5E8D6FE,
		0X647E3AD9, 0xC209316D, 0xF3E12BF0, 0x55962044, 0x90311ECA,
		0X3646157E, 0x07AE0FE3, 0xA1D90457, 0x579174BE, 0xF1E67F0A,
		0XC00E6597, 0x66796E23, 0xA3DE50AD, 0x05A95B19, 0x34414184,
		0X92364A30u
	},
	{
		0X00000000, 0xCCAA009E, 0x4225077D, 0x8E8F07E3, 0x844A0EFA,
		0X48E00E64, 0xC66F0987, 0x0AC50919, 0xD3E51BB5, 0x1F4F1B2B,
		0X91C01CC8, 0x5D6A1C56, 0x57AF154F, 0x9B0515D1, 0x158A1232,
		0XD92012AC, 0x7CBB312B, 0xB01131B5, 0x3E9E3656, 0xF23436C8,
		0XF8F13FD1, 0x345B3F4F, 0xBAD438AC, 0x767E3832, 0xAF5E2A9E,
		0X63F42A00, 0xED7B2DE3, 0x21D12D7D, 0x2B142464, 0xE7BE24FA,
		0X69312319, 0xA59B2387, 0xF9766256, 0x35DC62C8, 0xBB53652B,
		0X77F965B5, 0x7D3C6CAC, 0xB1966C32, 0x3F196BD1, 0xF3B36B4F,
		0X2A9379E3, 0xE639797D, 0x68B67E9E, 0xA41C7E00, 0xAED97719,
		0X62737787, 0xECFC7064, 0x205670FA, 0x85CD537D, 0x496753E3,
		0XC7E85400, 0x0B42549E, 0x01875D87, 0xCD2D5D19, 0x43A25AFA,
		0X8F085A64, 0x562848C8, 0x9A824856, 0x140D4FB5, 0xD8A74F2B,
		0XD2624632, 0x1EC846AC, 0x9047414F, 0x5CED41D1, 0x299DC2ED,
		0XE537C273, 0x6BB8C590, 0xA712C50E, 0xADD7CC17, 0x617DCC89,
		0XEFF2CB6A, 0x2358CBF4, 0xFA78D958, 0x36D2D9C6, 0xB85DDE25,
		0X74F7DEBB, 0x7E32D7A2, 0xB298D73C, 0x3C17D0DF, 0xF0BDD041,
		0X5526F3C6, 0x998CF358, 0x1703F4BB, 0xDBA9F425, 0xD16CFD3C,
		0X1DC6FDA2, 0x9349FA41, 0x5FE3FADF, 0x86C3E873, 0x4A69E8ED,
		0XC4E6EF0E, 0x084CEF90, 0x0289E689, 0xCE23E617, 0x40ACE1F4,
		0X8C06E16A, 0xD0EBA0BB, 0x1C41A025, 0x92CEA7C6, 0x5E64A758,
		0X54A1AE41, 0x980BAEDF, 0x1684A93C, 0xDA2EA9A2, 0x030EBB0E,
		0XCFA4BB90, 0x412BBC73, 0x8D81BCED, 0x8744B5F4, 0x4BEEB56A,
		0XC561B289, 0x09CBB217, 0xAC509190, 0x60FA910E, 0xEE7596ED,
		0X22DF9673, 0x281A9F6A, 0xE4B09FF4, 0x6A3F9817, 0xA6959889,
		0X7FB58A25, 0xB31F8ABB, 0x3D908D58, 0xF13A8DC6, 0xFBFF84DF,
		0X37558441, 0xB9DA83A2, 0x7570833C, 0x533B85DA, 0x9F918544,
		0X111E82A7, 0xDDB48239, 0xD7718B20, 0x1BDB8BBE, 0x95548C5D,
		0X59FE8CC3, 0x80DE9E6F, 0x4C749EF1, 0xC2FB9912, 0x0E51998C,
		0X04949095, 0xC83E900B, 0x46B197E8, 0x8A1B9776, 0x2F80B4F1,
		0XE32AB46F, 0x6DA5B38C, 0xA10FB312, 0xABCABA0B, 0x6760BA95,
		0XE9EFBD76, 0x2545BDE8, 0xFC65AF44, 0x30CFAFDA, 0xBE40A839,
		0X72EAA8A7, 0x782FA1BE, 0xB485A120, 0x3A0AA6C3, 0xF6A0A65D,
		0XAA4DE78C, 0x66E7E712, 0xE868E0F1, 0x24C2E06F, 0x2E07E976,
		0XE2ADE9E8, 0x6C22EE0B, 0xA088EE95, 0x79A8FC39, 0xB502FCA7,
		0X3B8DFB44, 0xF727FBDA, 0xFDE2F2C3, 0x3148F25D, 0xBFC7F5BE,
		0X736DF520, 0xD6F6D6A7, 0x1A5CD639, 0x94D3D1DA, 0x5879D144,
		0X52BCD85D, 0x9E16D8C3, 0x1099DF20, 0xDC33DFBE, 0x0513CD12,
		0XC9B9CD8C, 0x4736CA6F, 0x8B9CCAF1, 0x8159C3E8, 0x4DF3C376,
		0XC37CC495, 0x0FD6C40B, 0x7AA64737, 0xB60C47A9, 0x3883404A,
		0XF42940D4, 0xFEEC49CD, 0x32464953, 0xBCC94EB0, 0x70634E2E,
		0XA9435C82, 0x65E95C1C, 0xEB665BFF, 0x27CC5B61, 0x2D095278,
		0XE1A352E6, 0x6F2C5505, 0xA386559B, 0x061D761C, 0xCAB77682,
		0X44387161, 0x889271FF, 0x825778E6, 0x4EFD7878, 0xC0727F9B,
		0X0CD87F05, 0xD5F86DA9, 0x19526D37, 0x97DD6AD4, 0x5B776A4A,
		0X51B26353, 0x9D1863CD, 0x1397642E, 0xDF3D64B0, 0x83D02561,
		0X4F7A25FF, 0xC1F5221C, 0x0D5F2282, 0x079A2B9B, 0xCB302B05,
		0X45BF2CE6, 0x89152C78, 0x50353ED4, 0x9C9F3E4A, 0x121039A9,
		0XDEBA3937, 0xD47F302E, 0x18D530B0, 0x965A3753, 0x5AF037CD,
		0XFF6B144A, 0x33C114D4, 0xBD4E1337, 0x71E413A9, 0x7B211AB0,
		0XB78B1A2E, 0x39041DCD, 0xF5AE1D53, 0x2C8E0FFF, 0xE0240F61,
		0X6EAB0882, 0xA201081C, 0xA8C40105, 0x646E019B, 0xEAE10678,
		0X264B06E6u
	}
};

static const uint32 (*crc32_table)[256] = crc32_table_;

#undef CRC32TABLES
#endif


static const uint32 crc32_combinetable_[32][32] =
{
	{
		0x77073096, 0xEE0E612C, 0x076DC419, 0x0EDB8832, 0x1DB71064,
		0x3B6E20C8, 0x76DC4190, 0xEDB88320, 0x00000001, 0x00000002,
		0x00000004, 0x00000008, 0x00000010, 0x00000020, 0x00000040,
		0x00000080, 0x00000100, 0x00000200, 0x00000400, 0x00000800,
		0x00001000, 0x00002000, 0x00004000, 0x00008000, 0x00010000,
		0x00020000, 0x00040000, 0x00080000, 0x00100000, 0x00200000,
		0x00400000, 0x00800000u
	},
	{
		0x191B3141, 0x32366282, 0x646CC504, 0xC8D98A08, 0x4AC21251,
		0x958424A2, 0xF0794F05, 0x3B83984B, 0x77073096, 0xEE0E612C,
		0x076DC419, 0x0EDB8832, 0x1DB71064, 0x3B6E20C8, 0x76DC4190,
		0xEDB88320, 0x00000001, 0x00000002, 0x00000004, 0x00000008,
		0x00000010, 0x00000020, 0x00000040, 0x00000080, 0x00000100,
		0x00000200, 0x00000400, 0x00000800, 0x00001000, 0x00002000,
		0x00004000, 0x00008000u
	},
	{
		0xB8BC6765, 0xAA09C88B, 0x8F629757, 0xC5B428EF, 0x5019579F,
		0xA032AF3E, 0x9B14583D, 0xED59B63B, 0x01C26A37, 0x0384D46E,
		0x0709A8DC, 0x0E1351B8, 0x1C26A370, 0x384D46E0, 0x709A8DC0,
		0xE1351B80, 0x191B3141, 0x32366282, 0x646CC504, 0xC8D98A08,
		0x4AC21251, 0x958424A2, 0xF0794F05, 0x3B83984B, 0x77073096,
		0xEE0E612C, 0x076DC419, 0x0EDB8832, 0x1DB71064, 0x3B6E20C8,
		0x76DC4190, 0xEDB88320u
	},
	{
		0xCCAA009E, 0x4225077D, 0x844A0EFA, 0xD3E51BB5, 0x7CBB312B,
		0xF9766256, 0x299DC2ED, 0x533B85DA, 0xA6770BB4, 0x979F1129,
		0xF44F2413, 0x33EF4E67, 0x67DE9CCE, 0xCFBD399C, 0x440B7579,
		0x8816EAF2, 0xCB5CD3A5, 0x4DC8A10B, 0x9B914216, 0xEC53826D,
		0x03D6029B, 0x07AC0536, 0x0F580A6C, 0x1EB014D8, 0x3D6029B0,
		0x7AC05360, 0xF580A6C0, 0x30704BC1, 0x60E09782, 0xC1C12F04,
		0x58F35849, 0xB1E6B092u
	},
	{
		0xAE689191, 0x87A02563, 0xD4314C87, 0x73139F4F, 0xE6273E9E,
		0x173F7B7D, 0x2E7EF6FA, 0x5CFDEDF4, 0xB9FBDBE8, 0xA886B191,
		0x8A7C6563, 0xCF89CC87, 0x44629F4F, 0x88C53E9E, 0xCAFB7B7D,
		0x4E87F0BB, 0x9D0FE176, 0xE16EC4AD, 0x19AC8F1B, 0x33591E36,
		0x66B23C6C, 0xCD6478D8, 0x41B9F7F1, 0x8373EFE2, 0xDD96D985,
		0x605CB54B, 0xC0B96A96, 0x5A03D36D, 0xB407A6DA, 0xB37E4BF5,
		0xBD8D91AB, 0xA06A2517u
	},
	{
		0xF1DA05AA, 0x38C50D15, 0x718A1A2A, 0xE3143454, 0x1D596EE9,
		0x3AB2DDD2, 0x7565BBA4, 0xEACB7748, 0x0EE7E8D1, 0x1DCFD1A2,
		0x3B9FA344, 0x773F4688, 0xEE7E8D10, 0x078C1C61, 0x0F1838C2,
		0x1E307184, 0x3C60E308, 0x78C1C610, 0xF1838C20, 0x38761E01,
		0x70EC3C02, 0xE1D87804, 0x18C1F649, 0x3183EC92, 0x6307D924,
		0xC60FB248, 0x576E62D1, 0xAEDCC5A2, 0x86C88D05, 0xD6E01C4B,
		0x76B13ED7, 0xED627DAEu
	},
	{
		0x8F352D95, 0xC51B5D6B, 0x5147BC97, 0xA28F792E, 0x9E6FF41D,
		0xE7AEEE7B, 0x142CDAB7, 0x2859B56E, 0x50B36ADC, 0xA166D5B8,
		0x99BCAD31, 0xE8085C23, 0x0B61BE07, 0x16C37C0E, 0x2D86F81C,
		0x5B0DF038, 0xB61BE070, 0xB746C6A1, 0xB5FC8B03, 0xB0881047,
		0xBA6126CF, 0xAFB34BDF, 0x841791FF, 0xD35E25BF, 0x7DCD4D3F,
		0xFB9A9A7E, 0x2C4432BD, 0x5888657A, 0xB110CAF4, 0xB95093A9,
		0xA9D02113, 0x88D14467u
	},
	{
		0x33FFF533, 0x67FFEA66, 0xCFFFD4CC, 0x448EAFD9, 0x891D5FB2,
		0xC94BB925, 0x49E6740B, 0x93CCE816, 0xFCE8D66D, 0x22A0AA9B,
		0x45415536, 0x8A82AA6C, 0xCE745299, 0x4799A373, 0x8F3346E6,
		0xC5178B8D, 0x515E115B, 0xA2BC22B6, 0x9E09432D, 0xE763801B,
		0x15B60677, 0x2B6C0CEE, 0x56D819DC, 0xADB033B8, 0x80116131,
		0xDB53C423, 0x6DD68E07, 0xDBAD1C0E, 0x6C2B3E5D, 0xD8567CBA,
		0x6BDDFF35, 0xD7BBFE6Au
	},
	{
		0xCE3371CB, 0x4717E5D7, 0x8E2FCBAE, 0xC72E911D, 0x552C247B,
		0xAA5848F6, 0x8FC197AD, 0xC4F2291B, 0x52955477, 0xA52AA8EE,
		0x9124579D, 0xF939A97B, 0x290254B7, 0x5204A96E, 0xA40952DC,
		0x9363A3F9, 0xFDB641B3, 0x201D8527, 0x403B0A4E, 0x8076149C,
		0xDB9D2F79, 0x6C4B58B3, 0xD896B166, 0x6A5C648D, 0xD4B8C91A,
		0x72009475, 0xE40128EA, 0x13735795, 0x26E6AF2A, 0x4DCD5E54,
		0x9B9ABCA8, 0xEC447F11u
	},
	{
		0x1072DB28, 0x20E5B650, 0x41CB6CA0, 0x8396D940, 0xDC5CB4C1,
		0x63C86FC3, 0xC790DF86, 0x5450B94D, 0xA8A1729A, 0x8A33E375,
		0xCF16C0AB, 0x455C8717, 0x8AB90E2E, 0xCE031A1D, 0x4777327B,
		0x8EEE64F6, 0xC6ADCFAD, 0x562A991B, 0xAC553236, 0x83DB622D,
		0xDCC7C21B, 0x62FE8277, 0xC5FD04EE, 0x508B0F9D, 0xA1161F3A,
		0x995D3835, 0xE9CB762B, 0x08E7EA17, 0x11CFD42E, 0x239FA85C,
		0x473F50B8, 0x8E7EA170u
	},
	{
		0xF891F16F, 0x2A52E49F, 0x54A5C93E, 0xA94B927C, 0x89E622B9,
		0xC8BD4333, 0x4A0B8027, 0x9417004E, 0xF35F06DD, 0x3DCF0BFB,
		0x7B9E17F6, 0xF73C2FEC, 0x35095999, 0x6A12B332, 0xD4256664,
		0x733BCA89, 0xE6779512, 0x179E2C65, 0x2F3C58CA, 0x5E78B194,
		0xBCF16328, 0xA293C011, 0x9E568663, 0xE7DC0A87, 0x14C9134F,
		0x2992269E, 0x53244D3C, 0xA6489A78, 0x97E032B1, 0xF4B16323,
		0x3213C007, 0x6427800Eu
	},
	{
		0x88B6BA63, 0xCA1C7287, 0x4F49E34F, 0x9E93C69E, 0xE6568B7D,
		0x17DC10BB, 0x2FB82176, 0x5F7042EC, 0xBEE085D8, 0xA6B00DF1,
		0x96111DA3, 0xF7533D07, 0x35D77C4F, 0x6BAEF89E, 0xD75DF13C,
		0x75CAE439, 0xEB95C872, 0x0C5A96A5, 0x18B52D4A, 0x316A5A94,
		0x62D4B528, 0xC5A96A50, 0x5023D2E1, 0xA047A5C2, 0x9BFE4DC5,
		0xEC8D9DCB, 0x026A3DD7, 0x04D47BAE, 0x09A8F75C, 0x1351EEB8,
		0x26A3DD70, 0x4D47BAE0u
	},
	{
		0x5AD8A92C, 0xB5B15258, 0xB013A2F1, 0xBB5643A3, 0xADDD8107,
		0x80CA044F, 0xDAE50EDF, 0x6EBB1BFF, 0xDD7637FE, 0x619D69BD,
		0xC33AD37A, 0x5D04A0B5, 0xBA09416A, 0xAF638495, 0x85B60F6B,
		0xD01D1897, 0x7B4B376F, 0xF6966EDE, 0x365DDBFD, 0x6CBBB7FA,
		0xD9776FF4, 0x699FD9A9, 0xD33FB352, 0x7D0E60E5, 0xFA1CC1CA,
		0x2F4885D5, 0x5E910BAA, 0xBD221754, 0xA13528E9, 0x991B5793,
		0xE947A967, 0x09FE548Fu
	},
	{
		0xB566F6E2, 0xB1BCEB85, 0xB808D14B, 0xAB60A4D7, 0x8DB04FEF,
		0xC011999F, 0x5B52357F, 0xB6A46AFE, 0xB639D3BD, 0xB702A13B,
		0xB5744437, 0xB1998E2F, 0xB8421A1F, 0xABF5327F, 0x8C9B62BF,
		0xC247C33F, 0x5FFE803F, 0xBFFD007E, 0xA48B06BD, 0x92670B3B,
		0xFFBF1037, 0x240F262F, 0x481E4C5E, 0x903C98BC, 0xFB083739,
		0x2D616833, 0x5AC2D066, 0xB585A0CC, 0xB07A47D9, 0xBB8589F3,
		0xAC7A15A7, 0x83852D0Fu
	},
	{
		0x9D9129BF, 0xE053553F, 0x1BD7AC3F, 0x37AF587E, 0x6F5EB0FC,
		0xDEBD61F8, 0x660BC5B1, 0xCC178B62, 0x435E1085, 0x86BC210A,
		0xD6094455, 0x77638EEB, 0xEEC71DD6, 0x06FF3DED, 0x0DFE7BDA,
		0x1BFCF7B4, 0x37F9EF68, 0x6FF3DED0, 0xDFE7BDA0, 0x64BE7D01,
		0xC97CFA02, 0x4988F245, 0x9311E48A, 0xFD52CF55, 0x21D498EB,
		0x43A931D6, 0x875263AC, 0xD5D5C119, 0x70DA8473, 0xE1B508E6,
		0x181B178D, 0x30362F1Au
	},
	{
		0x2EE43A2C, 0x5DC87458, 0xBB90E8B0, 0xAC50D721, 0x83D0A803,
		0xDCD05647, 0x62D1AACF, 0xC5A3559E, 0x5037AD7D, 0xA06F5AFA,
		0x9BAFB3B5, 0xEC2E612B, 0x032DC417, 0x065B882E, 0x0CB7105C,
		0x196E20B8, 0x32DC4170, 0x65B882E0, 0xCB7105C0, 0x4D930DC1,
		0x9B261B82, 0xED3D3145, 0x010B64CB, 0x0216C996, 0x042D932C,
		0x085B2658, 0x10B64CB0, 0x216C9960, 0x42D932C0, 0x85B26580,
		0xD015CD41, 0x7B5A9CC3u
	},
	{
		0x1B4511EE, 0x368A23DC, 0x6D1447B8, 0xDA288F70, 0x6F2018A1,
		0xDE403142, 0x67F164C5, 0xCFE2C98A, 0x44B49555, 0x89692AAA,
		0xC9A35315, 0x4837A06B, 0x906F40D6, 0xFBAF87ED, 0x2C2E099B,
		0x585C1336, 0xB0B8266C, 0xBA014A99, 0xAF739373, 0x859620A7,
		0xD05D470F, 0x7BCB885F, 0xF79710BE, 0x345F273D, 0x68BE4E7A,
		0xD17C9CF4, 0x79883FA9, 0xF3107F52, 0x3D51F8E5, 0x7AA3F1CA,
		0xF547E394, 0x31FEC169u
	},
	{
		0xBCE15202, 0xA2B3A245, 0x9E1642CB, 0xE75D83D7, 0x15CA01EF,
		0x2B9403DE, 0x572807BC, 0xAE500F78, 0x87D118B1, 0xD4D33723,
		0x72D76807, 0xE5AED00E, 0x102CA65D, 0x20594CBA, 0x40B29974,
		0x816532E8, 0xD9BB6391, 0x6807C163, 0xD00F82C6, 0x7B6E03CD,
		0xF6DC079A, 0x36C90975, 0x6D9212EA, 0xDB2425D4, 0x6D394DE9,
		0xDA729BD2, 0x6F9431E5, 0xDF2863CA, 0x6521C1D5, 0xCA4383AA,
		0x4FF60115, 0x9FEC022Au
	},
	{
		0xFF08E5EF, 0x2560CD9F, 0x4AC19B3E, 0x9583367C, 0xF0776AB9,
		0x3B9FD333, 0x773FA666, 0xEE7F4CCC, 0x078F9FD9, 0x0F1F3FB2,
		0x1E3E7F64, 0x3C7CFEC8, 0x78F9FD90, 0xF1F3FB20, 0x3896F001,
		0x712DE002, 0xE25BC004, 0x1FC68649, 0x3F8D0C92, 0x7F1A1924,
		0xFE343248, 0x271962D1, 0x4E32C5A2, 0x9C658B44, 0xE3BA10C9,
		0x1C0527D3, 0x380A4FA6, 0x70149F4C, 0xE0293E98, 0x1B237B71,
		0x3646F6E2, 0x6C8DEDC4u
	},
	{
		0x6F76172E, 0xDEEC2E5C, 0x66A95AF9, 0xCD52B5F2, 0x41D46DA5,
		0x83A8DB4A, 0xDC20B0D5, 0x633067EB, 0xC660CFD6, 0x57B099ED,
		0xAF6133DA, 0x85B361F5, 0xD017C5AB, 0x7B5E8D17, 0xF6BD1A2E,
		0x360B321D, 0x6C16643A, 0xD82CC874, 0x6B2896A9, 0xD6512D52,
		0x77D35CE5, 0xEFA6B9CA, 0x043C75D5, 0x0878EBAA, 0x10F1D754,
		0x21E3AEA8, 0x43C75D50, 0x878EBAA0, 0xD46C7301, 0x73A9E043,
		0xE753C086, 0x15D6874Du
	},
	{
		0x56F5CAB9, 0xADEB9572, 0x80A62CA5, 0xDA3D5F0B, 0x6F0BB857,
		0xDE1770AE, 0x675FE71D, 0xCEBFCE3A, 0x460E9A35, 0x8C1D346A,
		0xC34B6E95, 0x5DE7DB6B, 0xBBCFB6D6, 0xACEE6BED, 0x82ADD19B,
		0xDE2AA577, 0x67244CAF, 0xCE48995E, 0x47E034FD, 0x8FC069FA,
		0xC4F1D5B5, 0x5292AD2B, 0xA5255A56, 0x913BB2ED, 0xF906639B,
		0x297DC177, 0x52FB82EE, 0xA5F705DC, 0x909F0DF9, 0xFA4F1DB3,
		0x2FEF3D27, 0x5FDE7A4Eu
	},
	{
		0x385993AC, 0x70B32758, 0xE1664EB0, 0x19BD9B21, 0x337B3642,
		0x66F66C84, 0xCDECD908, 0x40A8B451, 0x815168A2, 0xD9D3D705,
		0x68D6A84B, 0xD1AD5096, 0x782BA76D, 0xF0574EDA, 0x3BDF9BF5,
		0x77BF37EA, 0xEF7E6FD4, 0x058DD9E9, 0x0B1BB3D2, 0x163767A4,
		0x2C6ECF48, 0x58DD9E90, 0xB1BB3D20, 0xB8077C01, 0xAB7FFE43,
		0x8D8EFAC7, 0xC06CF3CF, 0x5BA8E1DF, 0xB751C3BE, 0xB5D2813D,
		0xB0D4043B, 0xBAD90E37u
	},
	{
		0xB4247B20, 0xB339F001, 0xBD02E643, 0xA174CAC7, 0x999893CF,
		0xE84021DF, 0x0BF145FF, 0x17E28BFE, 0x2FC517FC, 0x5F8A2FF8,
		0xBF145FF0, 0xA559B9A1, 0x91C27503, 0xF8F5EC47, 0x2A9ADECF,
		0x5535BD9E, 0xAA6B7B3C, 0x8FA7F039, 0xC43EE633, 0x530CCA27,
		0xA619944E, 0x97422EDD, 0xF5F55BFB, 0x309BB1B7, 0x6137636E,
		0xC26EC6DC, 0x5FAC8BF9, 0xBF5917F2, 0xA5C329A5, 0x90F7550B,
		0xFA9FAC57, 0x2E4E5EEFu
	},
	{
		0x695186A7, 0xD2A30D4E, 0x7E371CDD, 0xFC6E39BA, 0x23AD7535,
		0x475AEA6A, 0x8EB5D4D4, 0xC61AAFE9, 0x57445993, 0xAE88B326,
		0x8660600D, 0xD7B1C65B, 0x74128AF7, 0xE82515EE, 0x0B3B2D9D,
		0x16765B3A, 0x2CECB674, 0x59D96CE8, 0xB3B2D9D0, 0xBC14B5E1,
		0xA3586D83, 0x9DC1DD47, 0xE0F2BCCF, 0x1A947FDF, 0x3528FFBE,
		0x6A51FF7C, 0xD4A3FEF8, 0x7236FBB1, 0xE46DF762, 0x13AAE885,
		0x2755D10A, 0x4EABA214u
	},
	{
		0x66BC001E, 0xCD78003C, 0x41810639, 0x83020C72, 0xDD751EA5,
		0x619B3B0B, 0xC3367616, 0x5D1DEA6D, 0xBA3BD4DA, 0xAF06AFF5,
		0x857C59AB, 0xD189B517, 0x78626C6F, 0xF0C4D8DE, 0x3AF8B7FD,
		0x75F16FFA, 0xEBE2DFF4, 0x0CB4B9A9, 0x19697352, 0x32D2E6A4,
		0x65A5CD48, 0xCB4B9A90, 0x4DE63361, 0x9BCC66C2, 0xECE9CBC5,
		0x02A291CB, 0x05452396, 0x0A8A472C, 0x15148E58, 0x2A291CB0,
		0x54523960, 0xA8A472C0u
	},
	{
		0xB58B27B3, 0xB0674927, 0xBBBF940F, 0xAC0E2E5F, 0x836D5AFF,
		0xDDABB3BF, 0x6026613F, 0xC04CC27E, 0x5BE882BD, 0xB7D1057A,
		0xB4D30CB5, 0xB2D71F2B, 0xBEDF3817, 0xA6CF766F, 0x96EFEA9F,
		0xF6AED37F, 0x362CA0BF, 0x6C59417E, 0xD8B282FC, 0x6A1403B9,
		0xD4280772, 0x732108A5, 0xE642114A, 0x17F524D5, 0x2FEA49AA,
		0x5FD49354, 0xBFA926A8, 0xA4234B11, 0x93379063, 0xFD1E2687,
		0x214D4B4F, 0x429A969Eu
	},
	{
		0xFE273162, 0x273F6485, 0x4E7EC90A, 0x9CFD9214, 0xE28A2269,
		0x1E654293, 0x3CCA8526, 0x79950A4C, 0xF32A1498, 0x3D252F71,
		0x7A4A5EE2, 0xF494BDC4, 0x32587DC9, 0x64B0FB92, 0xC961F724,
		0x49B2E809, 0x9365D012, 0xFDBAA665, 0x20044A8B, 0x40089516,
		0x80112A2C, 0xDB535219, 0x6DD7A273, 0xDBAF44E6, 0x6C2F8F8D,
		0xD85F1F1A, 0x6BCF3875, 0xD79E70EA, 0x744DE795, 0xE89BCF2A,
		0x0A469815, 0x148D302Au
	},
	{
		0xD3C98813, 0x7CE21667, 0xF9C42CCE, 0x28F95FDD, 0x51F2BFBA,
		0xA3E57F74, 0x9CBBF8A9, 0xE206F713, 0x1F7CE867, 0x3EF9D0CE,
		0x7DF3A19C, 0xFBE74338, 0x2CBF8031, 0x597F0062, 0xB2FE00C4,
		0xBE8D07C9, 0xA66B09D3, 0x97A715E7, 0xF43F2D8F, 0x330F5D5F,
		0x661EBABE, 0xCC3D757C, 0x430BECB9, 0x8617D972, 0xD75EB4A5,
		0x75CC6F0B, 0xEB98DE16, 0x0C40BA6D, 0x188174DA, 0x3102E9B4,
		0x6205D368, 0xC40BA6D0u
	},
	{
		0xF7D6DEB4, 0x34DCBB29, 0x69B97652, 0xD372ECA4, 0x7D94DF09,
		0xFB29BE12, 0x2D227A65, 0x5A44F4CA, 0xB489E994, 0xB262D569,
		0xBFB4AC93, 0xA4185F67, 0x9341B88F, 0xFDF2775F, 0x2095E8FF,
		0x412BD1FE, 0x8257A3FC, 0xDFDE41B9, 0x64CD8533, 0xC99B0A66,
		0x4847128D, 0x908E251A, 0xFA6D4C75, 0x2FAB9EAB, 0x5F573D56,
		0xBEAE7AAC, 0xA62DF319, 0x972AE073, 0xF524C6A7, 0x31388B0F,
		0x6271161E, 0xC4E22C3Cu
	},
	{
		0xEDB88320, 0x00000001, 0x00000002, 0x00000004, 0x00000008,
		0x00000010, 0x00000020, 0x00000040, 0x00000080, 0x00000100,
		0x00000200, 0x00000400, 0x00000800, 0x00001000, 0x00002000,
		0x00004000, 0x00008000, 0x00010000, 0x00020000, 0x00040000,
		0x00080000, 0x00100000, 0x00200000, 0x00400000, 0x00800000,
		0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x10000000,
		0x20000000, 0x40000000u
	},
	{
		0x76DC4190, 0xEDB88320, 0x00000001, 0x00000002, 0x00000004,
		0x00000008, 0x00000010, 0x00000020, 0x00000040, 0x00000080,
		0x00000100, 0x00000200, 0x00000400, 0x00000800, 0x00001000,
		0x00002000, 0x00004000, 0x00008000, 0x00010000, 0x00020000,
		0x00040000, 0x00080000, 0x00100000, 0x00200000, 0x00400000,
		0x00800000, 0x01000000, 0x02000000, 0x04000000, 0x08000000,
		0x10000000, 0x20000000u
	},
	{
		0x1DB71064, 0x3B6E20C8, 0x76DC4190, 0xEDB88320, 0x00000001,
		0x00000002, 0x00000004, 0x00000008, 0x00000010, 0x00000020,
		0x00000040, 0x00000080, 0x00000100, 0x00000200, 0x00000400,
		0x00000800, 0x00001000, 0x00002000, 0x00004000, 0x00008000,
		0x00010000, 0x00020000, 0x00040000, 0x00080000, 0x00100000,
		0x00200000, 0x00400000, 0x00800000, 0x01000000, 0x02000000,
		0x04000000, 0x08000000u
	}
};

static const uint32 (*crc32_combinetable)[32] = crc32_combinetable_;
