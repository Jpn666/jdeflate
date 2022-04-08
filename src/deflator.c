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

#include "../deflator.h"


#if defined(AUTOINCLUDE_1)

/* deflate format definitions */
#define DEFLT_MAXBITS       15
#define DEFLT_PCODESMAXBITS 7
#define DEFLT_WINDOWSZ      32768


#else

#define DEFLT_LMAXSYMBOL 288
#define DEFLT_DMAXSYMBOL 32
#define DEFLT_CMAXSYMBOL 19

#define WNDWBITS 15
#define WNDWSIZE 32768

/* cache size */
#define SMASK ((WNDWSIZE << 1) - 1)
#define HMASK ((WNDWSIZE << 1) - 1)

/* number of literals (including end of block), match symbols and precodes */
#define MAXLTCODES 257
#define MAXLZCODES 32
#define MAXPCCODES 19


/* private stuff */
struct TDEFLTPrvt {
	/* public fields */
	struct TDeflator hidden;

	/* state */
	uintxx substate;
	uintxx level;
	uintxx used;

	uintxx blockinit;
	uintxx blocktype;
	uintxx hasinput;
	uintxx aux1;
	uintxx aux2;
	uintxx aux3;

	/* bit buffer */
#if defined(CTB_ENV64)
	uint64 bbuffer;
#else
	uint32 bbuffer;
#endif
	uintxx bcount;

	/* window buffer allocated space */
	uint8* window;
	uint8* windowend;

	/* window buffer size */
	uintxx wnsize;

	/* window span end (the used part of the window buffer) */
	uint8* wend;

	/* cursor, position in the window buffer */
	uintxx cursor;

	/* offset */
	uint16 base;

	/* cache */
	uint16* hlist;
	uint16* chain;

	/* match search parameters */
	uintxx nicematch;
	uintxx goodmatch;
	uintxx maxchain;
	uintxx mininsert;

	/* lz token and literal buffer */
	uint16* lzlist;
	uint16* lzlistend;

	/* lz buffer size */
	uintxx lzsize;

	/* last lz token */
	uint16* zend;

	/* token index */
	uint16* zptr;

	/* used for any non 0 level */
	struct TDEFLTExtra {
		/* chache table */
		uint16 chain[SMASK + 1];
		uint16 hlist[HMASK + 1];

		/* to count the frequencies (literals-lengths, distaces, precodes) */
		uintxx lfrqs[DEFLT_LMAXSYMBOL];
		uintxx dfrqs[DEFLT_DMAXSYMBOL];
		uintxx cfrqs[DEFLT_CMAXSYMBOL];
		uintxx lmax;
		uintxx dmax;
		uintxx cmax;

		/* to get the symbols sorted by frequency */
		uintxx smap[DEFLT_LMAXSYMBOL];

		/* working array used to build the code lengths */
		uintxx clns[DEFLT_LMAXSYMBOL];

		/* literal and precodes codes */
		struct THCode1 {
			uint8  bitlen;
			uint16 code;
		}
		litcodes[MAXLTCODES],
		precodes[MAXPCCODES];

		/* length and distance codes */
		struct THCode2 {
			uint8  bitlen;
			uint8  bextra;

			uint16 code;
			uint16 base;
		}
		lnscodes[MAXLZCODES],
		dstcodes[MAXLZCODES];

		/* encoding tables */
		struct THCode1* littable;
		struct THCode2* lnstable;
		struct THCode2* dsttable;
	}
	*extra;
};

#endif


#if defined(AUTOINCLUDE_1)

/* window buffer size | lz buffer size */
#define BUILDMEMINFO(A, B) (((A) << 0x08) | ((B) << 0x00))

CTB_INLINE uintxx
getmeminfo(uintxx level)
{
	switch (level) {
		case 0:
			return BUILDMEMINFO(WNDWBITS + 1, 0x00);
		case 1:
			return BUILDMEMINFO(WNDWBITS + 1, 0x0e);
		case 2:
		case 3:
		case 4:
			return BUILDMEMINFO(WNDWBITS + 1, 0x0f);
		case 5:
		case 6:
		case 7:
			return BUILDMEMINFO(WNDWBITS + 2, 0x0f);
		case 8:
		case 9:
			return BUILDMEMINFO(WNDWBITS + 2, 0x10);
	}
	return 0;
}

#undef BUILDMEMINFO

#define GETWNBFFSZ(M) ((uintxx) 1L << (((M) >> 0x08) & 0xff))
#define GETLZBFFSZ(M) ((uintxx) 1L << (((M) >> 0x00) & 0xff))

#define PRVT ((struct TDEFLTPrvt*) state)


CTB_INLINE void
setparameters(struct TDeflator* state, uintxx level)
{
	uintxx good;
	uintxx nice;
	uintxx lazy;
	uintxx chain;

	switch (level) {
		case 0: nice =   0; good =  0; lazy =   0; chain =    0; break;
		case 1: nice =   3; good =  3; lazy =   3; chain =    1; break;
		case 2: nice =   3; good =  3; lazy =   6; chain =    4; break;
		case 3: nice =   8; good =  6; lazy =   8; chain =    8; break;
		case 4: nice =  16; good =  8; lazy =  16; chain =   16; break;
		case 5: nice =  16; good =  8; lazy =  16; chain =   16; break;
		case 6: nice =  32; good =  8; lazy =  32; chain =   32; break;
		case 7: nice =  64; good = 16; lazy =  64; chain =  128; break;
		case 8: nice = 128; good = 32; lazy = 128; chain = 1024; break;
		case 9: nice = 258; good = 64; lazy = 258; chain = 4096; break;
		default:
			return;
	}
	PRVT->goodmatch = good;
	PRVT->nicematch = nice;
	PRVT->mininsert = lazy;
	PRVT->maxchain  = chain;
}


CTB_INLINE uintxx
allocateprvt(TDeflator* state)
{
	uintxx i;
	struct TDEFLTExtra* extra;

	extra = CTB_MALLOC(sizeof(struct TDEFLTExtra));
	if (extra == NULL) {
		return 0;
	}

	/* set the base and extra bit values */
	for (i = 0; i < MAXLZCODES; i++) {
		extra->lnscodes[i] = slnscodes[i];
		extra->dstcodes[i] = sdstcodes[i];
	}

	PRVT->hlist = extra->hlist;
	PRVT->chain = extra->chain;

	PRVT->extra = extra;
	return 1;
}

#define MINMATCH   3
#define MAXMATCH 258


#if defined(CTB_ENV64)
	#define WNDNGUARDSZ (MAXMATCH + (sizeof(uint64) * (4 + 1)))
#else
	#define WNDNGUARDSZ (MAXMATCH + (sizeof(uint32) * (4 + 1)))
#endif

static uintxx
allocatemem(TDeflator* state, uintxx meminfo)
{
	uintxx wsize;
	uintxx bsize;
	void* buffer;
	ASSERT(state);

	/* we needs some extra bytes at the end of the window buffer to
	 * perform fast comparisons */
	wsize = GETWNBFFSZ(meminfo) + WNDNGUARDSZ;
	bsize = GETLZBFFSZ(meminfo);
	if (bsize == 1)
		bsize--;

	buffer = PRVT->window;
	if (PRVT->wnsize < wsize) {
		buffer = CTB_REALLOC(PRVT->window, wsize);
		if (buffer == NULL) {
			return 0;
		}
		PRVT->wnsize = wsize;
	}
	PRVT->windowend  = PRVT->window = buffer;
	PRVT->windowend += wsize;

	buffer = PRVT->lzlist;
	if (PRVT->lzsize < bsize) {
		buffer = CTB_REALLOC(PRVT->lzlist, bsize * sizeof(PRVT->lzlist[0]));
		if (buffer == NULL) {
			return 0;
		}
		PRVT->lzsize = bsize;
	}
	PRVT->lzlistend  = PRVT->lzlist = buffer;
	PRVT->lzlistend += bsize;

	if (PRVT->level) {
		if (PRVT->extra == NULL) {
			if (allocateprvt(state) == 0) {
				return 0;
			}
		}
	}
	return 1;
}


#undef WNDNGUARDSZ

#undef GETWINBFFSZ
#undef GETTKNBFFSZ

#define SETERROR(ERROR) (state->error = (ERROR))
#define SETSTATE(STATE) (state->state = (STATE))


TDeflator*
deflator_create(uintxx level)
{
	struct TDeflator* state;

	if (level >= 10) {
		/* invalid level */
		return NULL;
	}

	state = CTB_CALLOC(1, sizeof(struct TDEFLTPrvt));
	if (state == NULL) {
		return NULL;
	}

	deflator_reset(state, level);
	if (state->error) {
		deflator_destroy(state);
		return NULL;
	}
	return state;
}

static void
resetcache(struct TDeflator* state)
{
	uintxx i;
	uintxx j;

	j = SMASK + 1;
	for (i = 0; j > i; i++) {
		PRVT->chain[i] = 0;
	}

	j = HMASK + 1;
	for (i = 0; j > i; i++) {
		PRVT->hlist[i] = 0;
	}
}

CTB_INLINE void
resetfreqs(TDeflator* state)
{
	uintxx i;

	for (i = 0; DEFLT_LMAXSYMBOL > i; i++) {
		PRVT->extra->lfrqs[i] = 0;
	}

	for (i = 0; DEFLT_DMAXSYMBOL > i; i++) {
		PRVT->extra->dfrqs[i] = 0;
	}
}

void
deflator_reset(TDeflator* state, uintxx level)
{
	uintxx meminfo;
	ASSERT(state);

	meminfo = getmeminfo(level);
	if (meminfo == 0) {
		/* invalid level */
		SETERROR(DEFLT_ELEVEL);
		goto L_ERROR;
	}

	PRVT->level = level;
	if (allocatemem(state, meminfo) == 0) {
		SETERROR(DEFLT_EOOM);
		goto L_ERROR;
	}
	setparameters(state, level);

	state->state = 0;
	state->flush = 0;
	state->error = 0;

	state->source = NULL;
	state->target = NULL;
	state->sbgn = NULL;
	state->send = NULL;
	state->tbgn = NULL;
	state->tend = NULL;

	/* private fields */
	PRVT->used = 0;
	PRVT->substate = 0;

	PRVT->blockinit = 0;
	PRVT->blocktype = 0;
	PRVT->hasinput  = 0;
	PRVT->aux1 = 0;
	PRVT->aux2 = 0;
	PRVT->aux3 = 0;

	PRVT->bbuffer = 0;
	PRVT->bcount  = 0;

	if (PRVT->level) {
		PRVT->base   = 0;
		PRVT->cursor = 0;

		PRVT->zend = PRVT->lzlist;
		PRVT->zptr = PRVT->lzlist;
		resetcache(state);
	}
	PRVT->wend = PRVT->window;
	return;

L_ERROR:
	SETSTATE(DEFLT_BADSTATE);
}

void
deflator_destroy(TDeflator* state)
{
	if (state == NULL) {
		return;
	}

	if (PRVT->window)
		CTB_FREE(PRVT->window);
	if (PRVT->lzlist)
		CTB_FREE(PRVT->lzlist);

	if (PRVT->extra) {
		CTB_FREE(PRVT->extra);
	}
	CTB_FREE(state);
}


#if defined(CTB_ENV64)
	#define BBTYPE uint64
#else
	#define BBTYPE uint32
#endif


/*
 * BIT output operations */

CTB_FORCEINLINE bool
tryemitbits(struct TDeflator* state, uintxx count)
{
	for(; count + PRVT->bcount > (sizeof(BBTYPE) << 3); PRVT->bcount -= 8) {
		if (state->target >= state->tend) {
			return 0;
		}
		*state->target++ = (uint8) PRVT->bbuffer;

		PRVT->bbuffer >>= 8;
	}
	return 1;
}

CTB_FORCEINLINE uintxx
tryflushbits(struct TDeflator* state)
{
	intxx bytes;

	if (PRVT->bcount == 0) {
		return 1;
	}

	bytes = (PRVT->bcount + 7) >> 3;
	for (; bytes-- > 0; PRVT->bbuffer = PRVT->bbuffer >> 8) {
		if (state->target >= state->tend) {
			return 0;
		}
		state->target[0] = (uint8) PRVT->bbuffer;

		state->target += 1;
		PRVT->bcount  -= 8;
	}

	PRVT->bcount  = 0;
	PRVT->bbuffer = 0;
	return 1;
}

CTB_FORCEINLINE void
putbits(struct TDeflator* state, uintxx bits, uintxx count)
{
	PRVT->bbuffer = PRVT->bbuffer | (bits << PRVT->bcount);
	PRVT->bcount += count;
}

static uintxx
endstream(struct TDeflator* state)
{
	const uint8 endblock[] = {
		0x00, 0x00, 0xff, 0xff
	};

	if (PRVT->blockinit == 0) {
		if (tryemitbits(state, 3)) {
			if (state->flush == DEFLT_END) {
				putbits(state, 1, 1);
			}
			else {
				putbits(state, 0, 1);
			}
			putbits(state, 0, 2);

			PRVT->blockinit++;
		}
		else {
			return DEFLT_TGTEXHSTD;
		}
	}

	if (PRVT->blockinit == 1) {
		if (tryflushbits(state) == 0) {
			PRVT->substate = 1;
			return DEFLT_TGTEXHSTD;
		}
		PRVT->blockinit++;
	}

	if (PRVT->blockinit == 2) {
		for (; PRVT->aux1 < 4;) {
			if (state->tend - state->target) {
				*state->target++ = endblock[PRVT->aux1++];
				continue;
			}
			return DEFLT_TGTEXHSTD;
		}
	}

	PRVT->blockinit = 0;
	PRVT->aux1 = 0;
	return 0;
}


static uintxx compress0(struct TDeflator* state);
static uintxx compress1(struct TDeflator* state);
static uintxx compress2(struct TDeflator* state);

static uintxx flushblck(struct TDeflator* state);

eDEFLTResult
deflator_deflate(TDeflator* state, eDEFLTFlush flush)
{
	uintxx r;
	ASSERT(state);

	if (flush && (state->flush == 0 || state->flush == DEFLT_FLUSH)) {
		state->flush = flush;
	}

	PRVT->used = 1;
	for (;;) {
		switch (state->state) {
			case 0: {
				switch (PRVT->level) {
					case 0:
						/* flush is handled by this function */
						r = compress0(state);
						if (r == 0) {
							SETSTATE(2);
							continue;
						}
						return r;
					case 1:
					case 2:
					case 3:
					case 4:
						r = compress1(state);
						break;
					default:  /* 5 6 7 8 9 */
						r = compress2(state);
						break;
				}

				if (r) {
					/* source exhausted */
					return r;
				}
			}

			/* fallthrough */
			case 1: {
				if ((r = flushblck(state)) != 0) {
					return r;
				}

				if (state->flush) {
					if (PRVT->hasinput == 0) {
						SETSTATE(2);
						continue;
					}
				}
				break;
			}

			/* end the stream */
			case 2: {
				r = endstream(state);
				if (r) {
					return DEFLT_TGTEXHSTD;
				}
				if (state->flush == DEFLT_FLUSH) {
					/* don't invalidate the state, we can keep compressing
					* using the same window */
					state->state = 0;
					state->flush = 0;
				}
				else {
					SETSTATE(DEFLT_BADSTATE);
				}
				return r;
			}

			default:
				goto L_ERROR;
		}
	}

L_ERROR:
	if (state->error == 0) {
		SETERROR(DEFLT_EBADSTATE);
	}
	return DEFLT_ERROR;
}

/* block types */
#define BLOCKSTRD 0
#define BLOCKSTTC 1
#define BLOCKDNMC 2


#define MAXSTRDSZ 0x8000

static uintxx
compress0(struct TDeflator* state)
{
	uintxx maxrun;
	uintxx outputleft;
	uintxx sourceleft;
	uintxx targetleft;
	uint8* buffer;

	switch (PRVT->substate) {
		case 0:
			break;
		case 1: goto L_STATE1;
		case 2: goto L_STATE2;
		case 3: goto L_STATE3;
	}

L_LOOP:
	outputleft = (uintxx) (PRVT->windowend - PRVT->wend);
	sourceleft = (uintxx) (state->send - state->source);

	maxrun = MAXSTRDSZ - PRVT->aux1;
	if (maxrun > outputleft)
		maxrun = outputleft;
	if (maxrun > sourceleft)
		maxrun = sourceleft;

	memcpy(PRVT->wend, state->source, maxrun);
	PRVT->wend    += maxrun;
	state->source += maxrun;

	PRVT->aux1 += maxrun;
	if (state->flush) {
		if (PRVT->aux1 == 0 && sourceleft == 0) {
			goto L_DONE;
		}
	}
	else {
		if (PRVT->aux1 < MAXSTRDSZ) {
			PRVT->substate = 0;
			return DEFLT_SRCEXHSTD;
		}
	}

	PRVT->blockinit = 0;

L_STATE1:
	if (PRVT->blockinit == 0) {
		if (tryemitbits(state, 3)) {
			putbits(state, 0, 1);
			putbits(state, 0, 2);

			PRVT->blockinit = 1;
		}
		else {
			PRVT->substate = 1;
			return DEFLT_TGTEXHSTD;
		}
	}

	if (tryflushbits(state) == 0) {
		PRVT->substate = 1;
		return DEFLT_TGTEXHSTD;
	}
	PRVT->blockinit = 0;

L_STATE2:
	targetleft = (uintxx) (state->tend - state->target);

	if (PRVT->blockinit == 0) {
		uint16 a;
		uint16 b;

		a = (uint16)  PRVT->aux1;
		b = (uint16) ~PRVT->aux1;
		if (targetleft >= 4) {
			*state->target++ = (uint8) (a >> 0x00);
			*state->target++ = (uint8) (a >> 0x08);
			*state->target++ = (uint8) (b >> 0x00);
			*state->target++ = (uint8) (b >> 0x08);
			targetleft += PRVT->aux2 = 4;
		}
		else {
			PRVT->aux2 = 0;
			PRVT->aux3 = 0;
			PRVT->aux3 |= ((uintxx) (a >> 0x00)) << 0x00;
			PRVT->aux3 |= ((uintxx) (a >> 0x08)) << 0x08;
			PRVT->aux3 |= ((uintxx) (b >> 0x00)) << 0x10;
			PRVT->aux3 |= ((uintxx) (b >> 0x08)) << 0x18;

		}
		PRVT->blockinit = 1;
	}

	for (; PRVT->aux2 < 4;) {
		if (state->tend - state->target) {
			*state->target++ = (uint8) (PRVT->aux3 >> (PRVT->aux2++ << 3));
			continue;
		}
		PRVT->substate = 2;
		return DEFLT_TGTEXHSTD;
	}
	PRVT->aux3 = 0;

L_STATE3:
	targetleft = (uintxx) (state->tend - state->target);

	maxrun = PRVT->aux1;
	if (maxrun > targetleft)
		maxrun = targetleft;

	buffer = PRVT->wend - PRVT->aux1;
	memcpy(state->target, buffer, maxrun);
	state->target += maxrun;

	PRVT->aux1 -= maxrun;
	if (PRVT->aux1) {
		PRVT->substate = 3;
		return DEFLT_TGTEXHSTD;
	}

	PRVT->wend = PRVT->window;
	goto L_LOOP;

L_DONE:
	PRVT->aux1 = 0;
	PRVT->aux2 = 0;
	PRVT->aux3 = 0;
	PRVT->substate  = 0;
	PRVT->blockinit = 0;
	return DEFLT_OK;
}

CTB_INLINE void
siftdown(uintxx* smap, uintxx* frqs, uintxx i, uintxx size)
{
	uintxx swap;
	uintxx r;
	uintxx left;
	uintxx rght;
	intxx diff;

	for (;;) {
		left = (i << 1L) + 1;
		rght = left + 1;
		r = i;

		if (left <= size - 1) {
			diff = frqs[smap[left]] - frqs[smap[i]];
			if (diff > 0 || (diff == 0 && smap[left] > smap[i])) {
				r = left;
			}
		}

		if (rght <= size - 1) {
			diff = frqs[smap[rght]] - frqs[smap[r]];
			if (diff > 0 || (diff == 0 && smap[rght] > smap[r])) {
				r = rght;
			}
		}
		if (i == r) {
			break;
		}
		swap = smap[i];
		smap[i] = smap[r];
		smap[r] = swap;
		i = r;
	}
}

static void
heapsort(uintxx* smap, uintxx* frqs, uintxx size)
{
	uintxx swap;
	intxx j;

	j = (size >> 1) +  1;
	for (; j >= 0; j--) {
		siftdown(smap, frqs, j, size);
	}

	for (j = size - 1; j > 0; j--) {
		swap = smap[0];
		smap[0] = smap[j];
		smap[j] = swap;

		siftdown(smap, frqs, 0, j);
	}
}

static void
limitlengths(uintxx* lengths, intxx size, intxx mlen)
{
	/* taken from: http://cbloomrants.blogspot.com */
	intxx i;
	intxx k;
	const uintxx ktable[] = {
		0x8000, 0x4000,
		0x2000, 0x1000,
		0x0800, 0x0400,
		0x0200, 0x0100,
		0x0080, 0x0040,
		0x0020, 0x0010,
		0x0008, 0x0004,
		0x0002, 0x0001
	};

	k = 0;
	for (i = 0; i < size; i++) {
		if (lengths[i] > (uintxx) mlen) {
			lengths[i] = mlen;
		}
		k += ktable[lengths[i]];
	}

	for (i = 0; i < size; i++) {
		while (lengths[i] < (uintxx) mlen && (uintxx) k > ktable[0]) {
			k -= ktable[++lengths[i]];
		}
	}

	i--;
	for (; i >= 0; i--) {
		while (k + ktable[lengths[i]] <= ktable[0]) {
			k += ktable[lengths[i]--];
		}
	}
}

/* In-Place Calculation of Minimum-Redundancy Codes
 * Alistair Moffaf - Jyrki Katajainen */
static void
katajainen(uintxx* frqs, intxx n)
{
	intxx tree;
	intxx leaf;
	intxx next;
	intxx cnts;
	intxx used;
	intxx prev;
	intxx dpth;
	intxx j;

	tree = 0;
	leaf = 0;

	for (next = 0; next < n - 1; next++) {
		if (leaf >= n || ((tree < next) && (frqs[tree] < frqs[leaf]))) {
			frqs[next] = frqs[tree];
			frqs[tree++] = next;
		}
		else {
			frqs[next] = frqs[leaf++];
		}

		if (leaf >= n || ((tree < next) && (frqs[tree] < frqs[leaf]))) {
			frqs[next] = frqs[next] + frqs[tree];
			frqs[tree++] = next;
		}
		else {
			frqs[next] = frqs[next] + frqs[leaf++];
		}
	}

	prev = tree = n - 2;
	dpth = 1;
	cnts = 2;

	for (n--; n > 0; dpth++) {
		for (used = 0; tree && frqs[tree - 1] >= (uintxx) prev;) {
			tree--;
			used++;
		}

		for (j = cnts - used; j; j--)
			frqs[n--] = dpth;

		cnts = used << 1;
		prev = tree;
	}
}

CTB_FORCEINLINE uint16
reversecode(uint16 code, uintxx length)
{
	uintxx a;
	uintxx b;
	uintxx r;

	static const uint8 rtable[] = {
		0x00, 0x08, 0x04, 0x0c,
		0x02, 0x0a, 0x06, 0x0e,
		0x01, 0x09, 0x05, 0x0d,
		0x03, 0x0b, 0x07, 0x0f
	};

	if (length > 8) {
		a = (uint8) (code >> 0);
		b = (uint8) (code >> 8);
		a = rtable[a >> 4] | (rtable[a & 0x0f] << 4);
		b = rtable[b >> 4] | (rtable[b & 0x0f] << 4);

		r = b | (a << 8);
		return (uint16) (r >> (0x10 - length));
	}

	a = (uint8) code;
	r = rtable[a >> 4] | (rtable[a & 0x0f] << 4);

	return (uint16) (r >> (0x08 - length));
}

static uintxx
computelengths(struct TDEFLTExtra* extra, uintxx* frqs, uintxx size)
{
	intxx i;
	intxx j;

	j = 0;
	for (i = 0; (uintxx) i < size; i++) {
		if (frqs[i])
			extra->smap[j++] = i;
	}

	if (j == 0) {
		return 0;
	}

	if (j == 1) {
		extra->clns[0] = 1;
	}
	else {
		heapsort(extra->smap, frqs, j);

		for (i = 0; j > i; i++) {
			extra->clns[i] = frqs[extra->smap[i]];
		}
		katajainen(extra->clns, j);
	}

	return j;
}


#define LTABLEMODE 0
#define DTABLEMODE 1
#define CTABLEMODE 2

#define NMAXBITS DEFLT_MAXBITS
#define PMAXBITS DEFLT_PCODESMAXBITS

static uintxx
setuptable(struct TDEFLTExtra* extra, uintxx mode, uintxx* frqs)
{
	uintxx last;
	uintxx size;
	uintxx i;
	uintxx j;
	struct THCode1* r1;
	struct THCode2* r2;
	uint16 counts[DEFLT_MAXBITS + 1];
	uint16 ncodes[DEFLT_MAXBITS + 1];

	const uintxx mlimits[][3] = {
		{DEFLT_LMAXSYMBOL, NMAXBITS},
		{DEFLT_DMAXSYMBOL, NMAXBITS},
		{DEFLT_CMAXSYMBOL, PMAXBITS},
	};

	size = computelengths(extra, frqs, mlimits[mode][0]);
	if (size == 0) {
		return 0;
	}
	limitlengths(extra->clns, size, mlimits[mode][1]);

	/* count the lengths of each length */
	for (i = 0; i < DEFLT_MAXBITS; i++) {
		counts[i] = 0;
	}
	for (i = 0; size > i; i++) {
		counts[extra->clns[i]]++;

		/* rearrange the frequencies */
		frqs[extra->smap[i]] = extra->clns[i];
	}

	/* calculate the codes */
	ncodes[0] = 0;
	for (i = 1; i <= DEFLT_MAXBITS; i++) {
		ncodes[i] = (counts[i - 1] + ncodes[i - 1]) << 1;
	}

	r1 = extra->litcodes;
	r2 = extra->lnscodes;
	j = 0;
	switch (mode) {
		case DTABLEMODE:
			r2 = extra->dstcodes;
			j = 1;
			break;
		case CTABLEMODE:
			r1 = extra->precodes;
			break;
	}

	last = 0;
	size = mlimits[mode][0];
	for (i = 0; i < size; i++) {
		uintxx bitlen;
		uint16 code;
		struct THCode1* c1;
		struct THCode2* c2;

		/* switch to from literals to lengths codes */
		if (i == MAXLTCODES) {
			j  = 1;
			r2 = r2 - MAXLTCODES;
		}

		bitlen = frqs[i];
		if (bitlen == 0) {
			if (j) {
				c2 = r2 + i;
				c2->bitlen = 0;
			}
			else {
				c1 = r1 + i;
				c1->bitlen = 0;
			}
			continue;
		}

		code = reversecode(ncodes[bitlen], bitlen);
		if (j) {
			c2 = r2 + i;
			c2->bitlen = (uint8) bitlen;
			c2->code   = (uint16) code;
		}
		else {
			c1 = r1 + i;
			c1->bitlen = (uint8) bitlen;
			c1->code   = (uint16) code;
		}
		ncodes[bitlen] += 1;
		last = i;
	}

	return last + 1;
}

static void
countprecodes(uintxx* clns, uintxx size, uintxx* cfrqs)
{
	uintxx i;
	uintxx j;
	uintxx n;
	uintxx s;
	uintxx p;
	uintxx maxrun;
	uintxx breakrun;
	uintxx count;

	clns[size + 1] = 0xffff;
	p = 0xffff;
	for (maxrun = i = j = count = 0; i <= size; i++) {
		n = clns[i];

		if (n == p) {
			count++;
			if (count < maxrun)
				continue;
			else
				breakrun = 1;
		}
		else {
			breakrun = 0;
		}

		if (count > 2) {
			if (p) {
				s = 16;
			}
			else {
				s = 17;
				if (count > 10)
					s++;
			}
			cfrqs[s]++;
			clns[j++] = s;
			clns[j++] = count;

			if (breakrun) {
				count = 0;
				continue;
			}
		}
		else {
			if (count) {
				cfrqs[p] += count;

				while (count) {
					clns[j++] = p;
					count--;
				}
			}
		}
		cfrqs[n]++;

		maxrun = 136;
		if (n)
			maxrun = 6;

		clns[j++] = p = n;
		count = 0;
	}

	clns[j - 1] = 0xffff;
}


static const uint8 pcodesorder[] = {
	16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

static void
buildtables(struct TDEFLTExtra* extra)
{
	uintxx lmax;
	uintxx dmax;
	uintxx i;

	lmax = setuptable(extra, LTABLEMODE, extra->lfrqs);
	dmax = setuptable(extra, DTABLEMODE, extra->dfrqs);

	/* no distances */
	if (dmax == 0 || dmax == 1) {
		if (dmax == 0) {
			extra->dfrqs[0] = 1;
			extra->dfrqs[1] = 1;
		}
		else {
			extra->dfrqs[extra->dfrqs[0] != 0] = 1;
		}
		dmax = 2;
	}

	for (i = 0; i < DEFLT_CMAXSYMBOL; i++) {
		extra->cfrqs[i] = 0;
	}

	/* extra->lfrqs and extra->dfrqs are modified in place like in the
	 * function setuptable above, after this it will containt the symbols for
	 * precodes and will end with  0xffff */
	countprecodes(extra->lfrqs, lmax, extra->cfrqs);
	countprecodes(extra->dfrqs, dmax, extra->cfrqs);

	setuptable(extra, CTABLEMODE, extra->cfrqs);

	/* count the precodes */
	for (i = DEFLT_CMAXSYMBOL - 1; i >= 3; i--) {
		if (extra->cfrqs[pcodesorder[i]]) {
			break;
		}
	}
	extra->cmax = i + 1;

	extra->lmax = lmax;
	extra->dmax = dmax;
}

#define BLOCKENDSYMBOL 256


/* write N bytes from B (bit buffer) to T (target buffer) */
#define W1(T, B) *(T)++ = (uint8) (B); (B) = (B) >> 8;
#define W2(T, B) W1(T, B); W1(T, B);
#define W3(T, B) W1(T, B); W2(T, B);
#define W4(T, B) W1(T, B); W3(T, B);
#define W5(T, B) W1(T, B); W4(T, B);
#define W6(T, B) W1(T, B); W5(T, B);

#if defined(CTB_ENV64)
	#define ENSURE2ON32(T, B, C)
	#define ENSURE2ON64(T, B, C) if(LIKELY((C) > 48)) { W6(T, B); (C) -= 48; }
	#define ENSURE3ON64(T, B, C) if(LIKELY((C) > 40)) { W5(T, B); (C) -= 40; }
	#define ENSURE4ON64(T, B, C) if(LIKELY((C) > 32)) { W4(T, B); (C) -= 32; }
#else
	#define ENSURE2ON64(T, B, C)
	#define ENSURE3ON64(T, B, C)
	#define ENSURE4ON64(T, B, C)
	#define ENSURE2ON32(T, B, C) if(LIKELY((C) > 16)) { W2(T, B); (C) -= 16; }
#endif

#define EMIT(BB, BC, BITS, N) BB |= (BBTYPE) (BITS) << (BC); BC += (N);


static uintxx
emitlzfast(struct TDeflator* state)
{
	BBTYPE bb;
	uintxx bc;
	uintxx r;
	uintxx extra;
	uint8* target;
	struct THCode1* littable;
	struct THCode2* lnstable;
	struct THCode2* dsttable;
	struct THCode1 code1;
	struct THCode2 lcode;
	struct THCode2 dcode;
	uint16* lzlist;

	littable = PRVT->extra->littable;
	lnstable = PRVT->extra->lnstable;
	dsttable = PRVT->extra->dsttable;

	/* load the state */
	bb = PRVT->bbuffer;
	bc = PRVT->bcount;
	lzlist = PRVT->zptr;
	target = state->target;

	r = 1;
	while ((uintxx) (state->tend - target) >= (8 + (sizeof(BBTYPE) << 1))) {
		if (LIKELY(lzlist[0] < 0x8000)) {
			code1 = littable[lzlist[0]];

			/* 15 */
			ENSURE2ON64(target, bb, bc);
			ENSURE2ON32(target, bb, bc);
			EMIT(bb, bc, code1.code, code1.bitlen);

			if (UNLIKELY(lzlist[0] == BLOCKENDSYMBOL)) {
				r = 0;
				goto L_DONE;
			}
			lzlist++;
			continue;
		}

		lcode = lnstable[(uint8) (lzlist[2] >> 0x08)];
		dcode = dsttable[(uint8) (lzlist[2] >> 0x00)];

		/* length 15 + 5 */
		ENSURE3ON64(target, bb, bc);
		ENSURE2ON32(target, bb, bc);
		EMIT(bb, bc, lcode.code, lcode.bitlen);

		if (LIKELY(lcode.bextra)) {
			extra = (lzlist[0] - (uintxx) (0x8000)) - lcode.base;

			ENSURE2ON32(target, bb, bc);
			EMIT(bb, bc, extra, lcode.bextra);
		}

		/* distance 15 + 13 */
		ENSURE4ON64(target, bb, bc);
		ENSURE2ON32(target, bb, bc);
		EMIT(bb, bc, dcode.code, dcode.bitlen);

		if (dcode.bextra) {
			extra = lzlist[1] - dcode.base;

			ENSURE2ON32(target, bb, bc);
			EMIT(bb, bc, extra, dcode.bextra);
		}
		lzlist += 3;
	}

L_DONE:
	/* restore the state */
	PRVT->bbuffer = bb;
	PRVT->bcount  = bc;
	PRVT->zptr    = lzlist;
	state->target = target;
	return r;
}

#undef W1
#undef W2
#undef W3
#undef W4
#undef W5
#undef W6
#undef EMIT

static uintxx
emitlz(struct TDeflator* state)
{
	uintxx extra;
	uintxx r;
	uintxx fastcheck;
	struct THCode1* littable;
	struct THCode2* lnstable;
	struct THCode2* dsttable;
	struct THCode1 code1;
	struct THCode2 code2;

	littable = PRVT->extra->littable;
	lnstable = PRVT->extra->lnstable;
	dsttable = PRVT->extra->dsttable;

	fastcheck = 1;
	switch (PRVT->aux1) {
		case 0:
			break;
		case 1: goto L_STATE1;
		case 2: goto L_STATE2;
		case 3: goto L_STATE2;
	}

L_LOOP:
	if (UNLIKELY(fastcheck)) {
		uintxx remaining = (uintxx) (state->tend - state->target);
		if (remaining >= ((sizeof(BBTYPE) << 1) << 2)) {
			r = emitlzfast(state);
			if (r == 0) {
				goto L_DONE;
			}
		}

		fastcheck = 0;
		goto L_LOOP;
	}

	if (PRVT->zptr[0] < 0x8000) {
		code1 = littable[PRVT->zptr[0]];
		if (tryemitbits(state, code1.bitlen)) {
			putbits(state, code1.code, code1.bitlen);
		}
		else {
			PRVT->aux1 = 0;
			return 1;
		}

		if (UNLIKELY(PRVT->zptr[0] == BLOCKENDSYMBOL)) {
			goto L_DONE;
		}
		PRVT->zptr++;
		goto L_LOOP;
	}

L_STATE1:
	/* length */
	code2 = lnstable[(uint8) (PRVT->zptr[2] >> 0x08)];
	if (PRVT->aux2 == 0) {
		if (tryemitbits(state, code2.bitlen)) {
			putbits(state, code2.code, code2.bitlen);
		}
		else {
			PRVT->aux1 = 1;
			PRVT->aux2 = 0;
			return 1;
		}
	}

	if (code2.bextra) {
		extra = ((uintxx) PRVT->zptr[0] - 0x8000) - code2.base;
		if (tryemitbits(state, code2.bextra)) {
			putbits(state, extra, code2.bextra);
		}
		else {
			PRVT->aux1 = 1;
			PRVT->aux2 = 1;
			return 1;
		}
		PRVT->aux2 = 0;
	}

L_STATE2:
	/* distance */
	code2 = dsttable[(uint8) (PRVT->zptr[2] >> 0x00)];
	if (PRVT->aux2 == 0) {
		if (tryemitbits(state, code2.bitlen)) {
			putbits(state, code2.code, code2.bitlen);
		}
		else {
			PRVT->aux1 = 2;
			PRVT->aux2 = 0;
			return 1;
		}
	}

	if (code2.bextra) {
		extra = PRVT->zptr[1] - code2.base;
		if (tryemitbits(state, code2.bextra)) {
			putbits(state, extra, code2.bextra);
		}
		else {
			PRVT->aux1 = 2;
			PRVT->aux2 = 1;
			return 1;
		}
		PRVT->aux2 = 0;
	}

	PRVT->zptr += 3;
	goto L_LOOP;

L_DONE:
	PRVT->aux1 = 0;
	PRVT->aux2 = 0;
	PRVT->zptr = PRVT->lzlist;
	PRVT->zend = PRVT->lzlist;
	return 0;
}

static uintxx
emittrees(struct TDeflator* state)
{
	uintxx* slist, symbol;

	switch (PRVT->aux1) {
		case 0:
			break;
		case 1: goto L_STATE1;
		case 2: goto L_STATE2;
		case 3: goto L_STATE2;
	}

	if (tryemitbits(state, 14)) {
		putbits(state, PRVT->extra->lmax - 257, 5);
		putbits(state, PRVT->extra->dmax -   1, 5);
		putbits(state, PRVT->extra->cmax -   4, 4);
	}
	else {
		return 1;
	}

L_STATE1:
	/* precodes */
	slist = PRVT->extra->cfrqs;
	for(; PRVT->aux2 < PRVT->extra->cmax; PRVT->aux2++) {
		if (tryemitbits(state, 3)) {
			putbits(state, slist[pcodesorder[PRVT->aux2]], 3);
		}
		else {
			PRVT->aux1 = 1;
			return 1;
		}
	}

	PRVT->aux2 = 0;
	PRVT->aux1 = 2;

L_STATE2:
	/* trees */
	if (PRVT->aux1 == 2) {
		slist = PRVT->extra->lfrqs;
	}
	else {
		slist = PRVT->extra->dfrqs;
	}

	while ((symbol = slist[PRVT->aux2]) ^ 0xffff) {
		uintxx extra;
		uintxx times;
		struct THCode1 code;

		code = PRVT->extra->precodes[symbol];
		if (symbol <= 15) {
			if (tryemitbits(state, PMAXBITS)) {
				putbits(state, code.code, code.bitlen);
			}
			else {
				return 1;
			}

			PRVT->aux2++;
			continue;
		}

		if (tryemitbits(state, PMAXBITS + 7) == 0) {
			return 1;
		}

		PRVT->aux2++;
		switch (symbol) {
			case 16: extra = 2; times = slist[PRVT->aux2] -  3; break;
			case 17: extra = 3; times = slist[PRVT->aux2] -  3; break;
			default: extra = 7; times = slist[PRVT->aux2] - 11; break;
		}
		putbits(state, code.code, code.bitlen);
		putbits(state, times, extra);
		PRVT->aux2++;
	}

	PRVT->aux1++;
	if (PRVT->aux1 == 3) {
		PRVT->aux2 = 0;
		goto L_STATE2;
	}

	PRVT->aux1 = 0;
	PRVT->aux2 = 0;
	return 0;
}

static uintxx
flushblck(struct TDeflator* state)
{
	uintxx total;
	uintxx r;

	switch (PRVT->substate) {
		case 0:
			break;
		case 1: goto L_STATE1;
		case 2: goto L_STATE2;
		case 3: goto L_STATE3;
	}

	total = (uintxx) (PRVT->zend - PRVT->zptr);
	if (total == 0) {
		SETSTATE(0);
		PRVT->substate  = 0;
		PRVT->blockinit = 0;
		return 0;
	}

	/* append the end-of-block symbol */
	PRVT->extra->lfrqs[BLOCKENDSYMBOL]++;
	PRVT->zend[0] = BLOCKENDSYMBOL;
	PRVT->zend++;

	if (total < 0x400) {
		/* force an static block for small blocks */
		PRVT->blocktype = BLOCKSTTC;
	}
	else {
		if (PRVT->level == 1) {
			PRVT->blocktype = BLOCKSTTC;
		}
		else {
			PRVT->blocktype = BLOCKDNMC;
		}
	}

	if (PRVT->blocktype == BLOCKDNMC) {
		PRVT->extra->littable = PRVT->extra->litcodes;
		PRVT->extra->lnstable = PRVT->extra->lnscodes;
		PRVT->extra->dsttable = PRVT->extra->dstcodes;
		buildtables(PRVT->extra);
	}
	else {
		PRVT->extra->littable = (void*) slitcodes;
		PRVT->extra->lnstable = (void*) slnscodes;
		PRVT->extra->dsttable = (void*) sdstcodes;
	}

L_STATE1:
	/* block header */
	if (tryemitbits(state, 3)) {
		putbits(state, 0, 1);
		putbits(state, PRVT->blocktype, 2);
	}
	else {
		PRVT->substate = 1;
		return DEFLT_TGTEXHSTD;
	}

L_STATE2:
	if (PRVT->blocktype == BLOCKDNMC) {
		r = emittrees(state);
		if (r) {
			PRVT->substate = 2;
			return DEFLT_TGTEXHSTD;
		}
	}

L_STATE3:
	r = emitlz(state);
	if (r) {
		PRVT->substate = 3;
		return DEFLT_TGTEXHSTD;
	}

	SETSTATE(0);
	PRVT->substate  = 0;
	PRVT->blockinit = 0;
	return 0;
}


CTB_INLINE void
insert(struct TDeflator* state, uint16 offset, uint16 hhash)
{
	uintxx hindex;

	hindex = hhash & HMASK;

	PRVT->chain[offset & SMASK] = PRVT->hlist[hindex];
	PRVT->hlist[hindex] = offset;
}


#if !defined(CTB_STRICTALIGNMENT) && defined(CTB_FASTUNALIGNED)
	#define GETSHEAD3(B, N) ((*((uint32*) ((B) + (N)))) & 0xffffffL)
	#define GETSHEAD4(B, N) ((*((uint32*) ((B) + (N)))))
#else
	#define GETSHEAD3(B, N) \
	    ((B)[(N) + 0] << 0x00) | \
	    ((B)[(N) + 1] << 0x08) | \
	    ((B)[(N) + 2] << 0x10)

	#define GETSHEAD4(B, N) GETSHEAD3(B, N) | ((B)[(N) + 3] << 0x18)
#endif

#define GETHHASH(H) (((H) * 0x9e3779b1) >> 16)


void
deflator_setdctnr(TDeflator* state, uint8* dict, uintxx size)
{
	uintxx i;
	ASSERT(state && dict);

	if (PRVT->level == 0 || PRVT->used) {
		SETERROR(DEFLT_EBADUSE);
		SETSTATE(DEFLT_BADSTATE);
		return;
	}

	if (size > WNDWSIZE)
		size = WNDWSIZE;

	if (PRVT->level <= 4) {
		for (i = 0; i < size - 2; i++) {
			insert(state, (uint16) i, GETHHASH(GETSHEAD3(dict, i)));
		}
	}
	else {
		for (i = 0; i < size - 2; i++) {
			insert(state, (uint16) i, GETHHASH(GETSHEAD4(dict, i)));
		}
	}
	memcpy(PRVT->window, dict, size);
	PRVT->wend  += size;
	PRVT->cursor = size;

	PRVT->used = 1;
}

static uintxx
fillwindow(struct TDeflator* state)
{
	uintxx total;
	uintxx windowleft;
	uintxx sourceleft;

	windowleft = (uintxx) (PRVT->windowend - PRVT->wend);
	sourceleft = (uintxx) (state->send     - state->source);

	total = sourceleft;
	if (LIKELY(sourceleft > windowleft)) {
		if (LIKELY(windowleft < 0x400)) {
			uint8* bgn;

			/* we use this to get the offset in the window
			 * cursor - base := relative position
			 * offset        := relative position - hash index */
			PRVT->base -= (uint16) (PRVT->cursor - WNDWSIZE);

			/* move the window lower part of the buffer */
			bgn = PRVT->window + (PRVT->cursor - WNDWSIZE);
			memcpy(PRVT->window, bgn, PRVT->wend - bgn);

			PRVT->cursor = WNDWSIZE;
			PRVT->wend   = PRVT->window + (PRVT->wend - bgn);

			windowleft = (uintxx) (PRVT->windowend - PRVT->wend);
		}
	}

	if (total > windowleft) {
		total = windowleft;
	}
	if (total) {
		memcpy(PRVT->wend, state->source, total);
		state->source += total;
		PRVT->wend    += total;
	}
	return total;
}


#if !defined(CTB_STRICTALIGNMENT) && defined(CTB_FASTUNALIGNED)

#if defined(__GNUC__)
	#if defined(CTB_ENV64)
		#define CTZERO(X) __builtin_ctzll(X)
	#else
		#define CTZERO(X) __builtin_ctzl(X)
	#endif
#else
	#if defined(__has_builtin)
		#if defined(CTB_ENV64)
			#if __has_builtin(__builtin_ctzll)
				#define CTZERO(X) __builtin_ctzll(X)
			#endif
		#else
			#if __has_builtin(__builtin_ctzl)
				#define CTZERO(X) __builtin_ctzl(X)
			#endif
		#endif
	#endif
#endif

#if CTB_IS_BIGENDIAN
	#undef CTZERO
#endif


CTB_FORCEINLINE uintxx
getmatchlength(uint8* p1, uint8* p2, uint8* end)
{
	uint8* pp;

#if defined(CTB_ENV64)
	uint64* c1;
	uint64* c2;
#if defined(CTZERO)
	uint64 xor;
#endif
#else
	uint32* c1;
	uint32* c2;
#if defined(CTZERO)
	uint32 xor;
#endif
#endif

	pp = p1;
	c1 = (void*) p1;
	c2 = (void*) p2;
#if defined(CTZERO)
	xor = 0;

	do {
		if ((uint8*) c1 >= end) {
			return (uintxx) (end - pp);
		}
	} while (
		((xor = *c1++ ^ *c2++) == 0) &&
		((xor = *c1++ ^ *c2++) == 0) &&
		((xor = *c1++ ^ *c2++) == 0) &&
		((xor = *c1++ ^ *c2++) == 0));

	p1 = ((uint8*) (c1 - 1)) + (CTZERO(xor) >> 3);
	if (p1 >= end) {
		return (uintxx) (end - pp);
	}

	return (uintxx) (p1 - pp);
#else

	do {
		if ((uint8*) c1 >= end) {
			return (uintxx) (end - pp);
		}
	} while (
		(*c1++ == *c2++) &&
		(*c1++ == *c2++) &&
		(*c1++ == *c2++) &&
		(*c1++ == *c2++));

	p1 = (void*) (c1 - 1);
	p2 = (void*) (c2 - 1);

	if (p1[0] ^ p2[0]) { p1 += 0; goto L1; } 
	if (p1[1] ^ p2[1]) { p1 += 1; goto L1; }
	if (p1[2] ^ p2[2]) { p1 += 2; goto L1; }
	if (p1[3] ^ p2[3]) { p1 += 3; goto L1; }
#if defined(CTB_ENV64)
	if (p1[4] ^ p2[4]) { p1 += 4; goto L1; }
	if (p1[5] ^ p2[5]) { p1 += 5; goto L1; }
	if (p1[6] ^ p2[6]) { p1 += 6; goto L1; }
	if (p1[7] ^ p2[7]) { p1 += 7; goto L1; }
#endif

L1:
	if (p1 >= end) {
		return (uintxx) (end - pp);
	}
	return (uintxx) (p1 - pp);
#endif
}

#if defined(CTZERO)
	#undef CTZERO
#endif

#else

CTB_FORCEINLINE uintxx
getmatchlength(uint8* p1, uint8* p2, uint8* end)
{
	uint8* pp;

	pp = p1;
	do {
		if (p1 >= end) {
			return (uintxx) (end - pp);
		}
	} while (
		(*p1++ == *p2++) &&
		(*p1++ == *p2++) &&
		(*p1++ == *p2++) &&
		(*p1++ == *p2++));

	p1--;
	if (p1 >= end) {
		return (uintxx) (end - pp);
	}
	return (uintxx) (p1 - pp);
}

#endif


struct TMatch {
	uintxx length;
	uintxx offset;
};

CTB_INLINE struct TMatch
findmatch(struct TDeflator* state, uint16 hhash, uintxx minlength)
{
	intxx  i;
	uintxx next;
	uint8* strbgn;
	uint8* strend;
	uintxx length;
	uintxx offset;
	uintxx rpos;
	uintxx noffset;

	length = minlength;
	offset = 0;

	strbgn = PRVT->window + PRVT->cursor;
	strend = strbgn + MAXMATCH;
	if (strend > PRVT->wend) {
		strend = PRVT->wend;
	}

	/* base offset */
	rpos = PRVT->cursor - PRVT->base;

	next = PRVT->hlist[hhash & HMASK];
	for (i = PRVT->maxchain; i > 0; i--) {
		uintxx nlength;
		uint8* pmatch;

		/* we use modular arithmetic here, so there is no need to shift
		 * the cache table entries each time we slide the window */
		noffset = (uint16) (rpos - next);
		if (UNLIKELY(noffset > WNDWSIZE || noffset == 0)) {
			break;
		}
		pmatch = strbgn - noffset;
		if (LIKELY(strbgn[length] == pmatch[length])) {

			nlength = getmatchlength(strbgn, pmatch, strend);
			if (nlength > length) {
				length = nlength;
				offset = noffset;

				if (length >= PRVT->nicematch) {
					if (length >= PRVT->goodmatch) {
						break;
					}
					i = i >> 2;
				}
			}
		}
		next = PRVT->chain[next & SMASK];
	}

	return (struct TMatch) {length, offset};
}

static const uint8 dsymbols[] = {
	0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x04, 0x05,
	0x05, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07,
	0x07, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
	0x08, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
	0x09, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a,
	0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a,
	0x0a, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
	0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
	0x0b, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d,
	0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d,
	0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d,
	0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d,
	0x0d, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e,
	0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e,
	0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e,
	0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e,
	0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e,
	0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e,
	0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e,
	0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e,
	0x0e, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
	0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
	0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
	0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
	0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
	0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
	0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
	0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
	0x00, 0x0f, 0x10, 0x11, 0x12, 0x12, 0x13, 0x13,
	0x14, 0x14, 0x14, 0x14, 0x15, 0x15, 0x15, 0x15,
	0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16,
	0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17,
	0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
	0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
	0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19,
	0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19,
	0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a,
	0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a,
	0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a,
	0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a,
	0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b,
	0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b,
	0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b,
	0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b,
	0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c,
	0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c,
	0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c,
	0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c,
	0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c,
	0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c,
	0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c,
	0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c,
	0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d,
	0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d,
	0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d,
	0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d,
	0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d,
	0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d,
	0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d,
	0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d
};

CTB_FORCEINLINE uintxx
getdsymbol(uintxx n)
{
	if (n < 256) {
		return dsymbols[n];
	}
	return dsymbols[256 + ((n - 1) >> 7)];
}

static const uint8 lsymbols[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x08, 0x09, 0x09, 0x0a, 0x0a, 0x0b, 0x0b,
	0x0c, 0x0c, 0x0c, 0x0c, 0x0d, 0x0d, 0x0d, 0x0d,
	0x0e, 0x0e, 0x0e, 0x0e, 0x0f, 0x0f, 0x0f, 0x0f,
	0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
	0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
	0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12,
	0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13,
	0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
	0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
	0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15,
	0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15,
	0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16,
	0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16,
	0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17,
	0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17,
	0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
	0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
	0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
	0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
	0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19,
	0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19,
	0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19,
	0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19,
	0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a,
	0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a,
	0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a,
	0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a,
	0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b,
	0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b,
	0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b,
	0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1c
};

CTB_FORCEINLINE uintxx
getlsymbol(uintxx n)
{
	return lsymbols[n - 3];
}


CTB_FORCEINLINE void
appendz(struct TDeflator* state, struct TMatch match, uintxx ls, uintxx ds)
{
	/* token layout:
	 *        length       distance            length symbol and distance
	 * 1------a aaaaaaaa | bbbbbbbb bbbbbbbb | cccccccc dddddddd */
	PRVT->zend[0] = (uint16) (match.length | 0x8000);
	PRVT->zend[1] = (uint16) (match.offset);

	PRVT->zend[2] = (uint16) ((ls << 0x08) | (ds << 0x00));
	PRVT->zend += 3;
}

#define APPENDL(B, L) (*(B)++ = (uint16) (L))


#define MINLOOKAHEAD (MINMATCH + MAXMATCH)

/* greedy parser */
static uintxx
compress1(struct TDeflator* state)
{
	uintxx limit;
	uintxx srcleft;
	uintxx r;
	uintxx c;
	uintxx skip;
	uintxx* lnsfrqs;
	uintxx* dstfrqs;
	uintxx* litfrqs;
	struct TMatch match;

	litfrqs = PRVT->extra->lfrqs;
	dstfrqs = PRVT->extra->dfrqs;
	lnsfrqs = PRVT->extra->lfrqs + MAXLTCODES;
	if (PRVT->blockinit == 0) {
		resetfreqs(state);
		PRVT->blockinit = 1;
	}

L_LOOP:
	limit = (uintxx) (PRVT->wend - PRVT->window);
	if (limit - PRVT->cursor > MINLOOKAHEAD) {
		if (state->flush == 0) {
			limit -= MINLOOKAHEAD - 1;
		}
	}
	else {
		srcleft = (uintxx) (state->send - state->source);
		if (srcleft) {
			limit = PRVT->cursor;
		}
		else {
			if (state->flush == 0) {
				return DEFLT_SRCEXHSTD;
			}
		}
	}

	while (LIKELY(limit > PRVT->cursor)) {
		uintxx lsymbol;
		uintxx dsymbol;
		uint16 hash;
		uint32 head;

		head = GETSHEAD3(PRVT->window, PRVT->cursor);
		hash = GETHHASH(head);

		match = findmatch(state, hash, MINMATCH - 1);

		insert(state, (uint16) (PRVT->cursor - PRVT->base), hash);
		if (LIKELY(match.length >= MINMATCH)) {
			lsymbol = getlsymbol(match.length);
			dsymbol = getdsymbol(match.offset);

			lnsfrqs[lsymbol]++;
			dstfrqs[dsymbol]++;
			appendz(state, match, lsymbol, dsymbol);
			if (LIKELY(match.length <= PRVT->mininsert)) {
				PRVT->cursor++;
				for (skip = 1; match.length > skip; skip++) {
					head = GETSHEAD3(PRVT->window, PRVT->cursor);
					hash = GETHHASH(head);

					insert(state, (uint16) (PRVT->cursor - PRVT->base), hash);
					PRVT->cursor++;
				}
			}
			else {
				PRVT->cursor += match.length;
			}
		}
		else {
			c = PRVT->window[PRVT->cursor];
			APPENDL(PRVT->zend, c);
			litfrqs[c]++;

			PRVT->cursor++;
		}

		if (UNLIKELY(PRVT->zend + 4 > PRVT->lzlistend)) {
			/* flush */
			SETSTATE(1);
			PRVT->hasinput = 1;
			return 0;
		}
	}

	r = fillwindow(state);
	if (LIKELY(r)) {
		goto L_LOOP;
	}

	if (UNLIKELY(state->flush)) {
		SETSTATE(1);
		PRVT->hasinput = 0;
		/* no more input */
		return 0;
	}

	return DEFLT_SRCEXHSTD;
}

/* lazy parser */
static uintxx
compress2(struct TDeflator* state)
{
	uintxx limit;
	uintxx srcleft;
	uintxx minlength;
	uintxx hasmatch;
	uintxx skip;
	uintxx r;
	uintxx c;
	uintxx* lnsfrqs;
	uintxx* dstfrqs;
	uintxx* litfrqs;
	struct TMatch prevm;
	struct TMatch match;

	if (PRVT->blockinit == 0) {
		resetfreqs(state);
		PRVT->blockinit = 1;
	}
	litfrqs = PRVT->extra->lfrqs;
	dstfrqs = PRVT->extra->dfrqs;
	lnsfrqs = PRVT->extra->lfrqs + MAXLTCODES;

	/* added because GCC warnings */
	skip = 1;
	prevm.length = 0;
	prevm.offset = 0;

	hasmatch  = 0;
	minlength = MINMATCH;

L_LOOP:
	limit = (uintxx) (PRVT->wend - PRVT->window);
	if (limit - PRVT->cursor > MINLOOKAHEAD + 1) {
		if (state->flush == 0) {
			limit -= MINLOOKAHEAD;
		}
	}
	else {
		srcleft = (uintxx) (state->send - state->source);
		if (srcleft) {
			limit = PRVT->cursor;
		}
		else {
			if (state->flush == 0) {
				return DEFLT_SRCEXHSTD;
			}
		}
	}

	while (LIKELY(limit > PRVT->cursor)) {
		uint16 hash;
		uint32 head;

		for (;;) {
			head = GETSHEAD4(PRVT->window, PRVT->cursor);
			hash = GETHHASH(head);

			match = findmatch(state, hash, minlength);

			insert(state, (uint16) (PRVT->cursor - PRVT->base), hash);
			PRVT->cursor++;
			if (LIKELY(hasmatch)) {
				if (LIKELY(minlength >= match.length)) {
					match = prevm;
					skip++;
				}
				else {
					c = PRVT->window[PRVT->cursor - 2];
					litfrqs[c]++;
					APPENDL(PRVT->zend, c);
				}
			}
			else {
				if (LIKELY(match.length > MINMATCH)) {
					hasmatch = 1;
					skip = 1;
					if (LIKELY(match.length < PRVT->nicematch)) {
						/* try at the next position */
						prevm     = match;
						minlength = prevm.length;
						continue;
					}
				}
			}

			break;
		}

		if (LIKELY(hasmatch)) {
			uintxx lsymbol;
			uintxx dsymbol;

			lsymbol = getlsymbol(match.length);
			dsymbol = getdsymbol(match.offset);

			lnsfrqs[lsymbol]++;
			dstfrqs[dsymbol]++;
			appendz(state, match, lsymbol, dsymbol);
			for (; match.length > skip; skip++) {
				head = GETSHEAD4(PRVT->window, PRVT->cursor);
				hash = GETHHASH(head);

				insert(state, (uint16) (PRVT->cursor - PRVT->base), hash);
				PRVT->cursor++;
			}

			hasmatch = 0;
			minlength = MINMATCH;
		}
		else {
			c = PRVT->window[PRVT->cursor - 1];
			litfrqs[c]++;

			APPENDL(PRVT->zend, c);
		}

		if (UNLIKELY(PRVT->zend + 5 > PRVT->lzlistend)) {
			/* flush */
			SETSTATE(1);
			PRVT->hasinput = 1;

			return 0;
		}
	}

	r = fillwindow(state);
	if (LIKELY(r)) {
		goto L_LOOP;
	}

	if (UNLIKELY(state->flush)) {
		SETSTATE(1);
		PRVT->hasinput = 0;
		/* no more input */
		return 0;
	}

	return DEFLT_SRCEXHSTD;
}

#undef APPENDL

#undef SETSTATE
#undef SETERROR
#undef PRVT

#else

/* ****************************************************************************
 * Static Tables
 *************************************************************************** */

static const struct THCode1 slitcodes[MAXLTCODES] = {
	{0x08, 0x000c}, {0x08, 0x008c}, {0x08, 0x004c},
	{0x08, 0x00cc}, {0x08, 0x002c}, {0x08, 0x00ac},
	{0x08, 0x006c}, {0x08, 0x00ec}, {0x08, 0x001c},
	{0x08, 0x009c}, {0x08, 0x005c}, {0x08, 0x00dc},
	{0x08, 0x003c}, {0x08, 0x00bc}, {0x08, 0x007c},
	{0x08, 0x00fc}, {0x08, 0x0002}, {0x08, 0x0082},
	{0x08, 0x0042}, {0x08, 0x00c2}, {0x08, 0x0022},
	{0x08, 0x00a2}, {0x08, 0x0062}, {0x08, 0x00e2},
	{0x08, 0x0012}, {0x08, 0x0092}, {0x08, 0x0052},
	{0x08, 0x00d2}, {0x08, 0x0032}, {0x08, 0x00b2},
	{0x08, 0x0072}, {0x08, 0x00f2}, {0x08, 0x000a},
	{0x08, 0x008a}, {0x08, 0x004a}, {0x08, 0x00ca},
	{0x08, 0x002a}, {0x08, 0x00aa}, {0x08, 0x006a},
	{0x08, 0x00ea}, {0x08, 0x001a}, {0x08, 0x009a},
	{0x08, 0x005a}, {0x08, 0x00da}, {0x08, 0x003a},
	{0x08, 0x00ba}, {0x08, 0x007a}, {0x08, 0x00fa},
	{0x08, 0x0006}, {0x08, 0x0086}, {0x08, 0x0046},
	{0x08, 0x00c6}, {0x08, 0x0026}, {0x08, 0x00a6},
	{0x08, 0x0066}, {0x08, 0x00e6}, {0x08, 0x0016},
	{0x08, 0x0096}, {0x08, 0x0056}, {0x08, 0x00d6},
	{0x08, 0x0036}, {0x08, 0x00b6}, {0x08, 0x0076},
	{0x08, 0x00f6}, {0x08, 0x000e}, {0x08, 0x008e},
	{0x08, 0x004e}, {0x08, 0x00ce}, {0x08, 0x002e},
	{0x08, 0x00ae}, {0x08, 0x006e}, {0x08, 0x00ee},
	{0x08, 0x001e}, {0x08, 0x009e}, {0x08, 0x005e},
	{0x08, 0x00de}, {0x08, 0x003e}, {0x08, 0x00be},
	{0x08, 0x007e}, {0x08, 0x00fe}, {0x08, 0x0001},
	{0x08, 0x0081}, {0x08, 0x0041}, {0x08, 0x00c1},
	{0x08, 0x0021}, {0x08, 0x00a1}, {0x08, 0x0061},
	{0x08, 0x00e1}, {0x08, 0x0011}, {0x08, 0x0091},
	{0x08, 0x0051}, {0x08, 0x00d1}, {0x08, 0x0031},
	{0x08, 0x00b1}, {0x08, 0x0071}, {0x08, 0x00f1},
	{0x08, 0x0009}, {0x08, 0x0089}, {0x08, 0x0049},
	{0x08, 0x00c9}, {0x08, 0x0029}, {0x08, 0x00a9},
	{0x08, 0x0069}, {0x08, 0x00e9}, {0x08, 0x0019},
	{0x08, 0x0099}, {0x08, 0x0059}, {0x08, 0x00d9},
	{0x08, 0x0039}, {0x08, 0x00b9}, {0x08, 0x0079},
	{0x08, 0x00f9}, {0x08, 0x0005}, {0x08, 0x0085},
	{0x08, 0x0045}, {0x08, 0x00c5}, {0x08, 0x0025},
	{0x08, 0x00a5}, {0x08, 0x0065}, {0x08, 0x00e5},
	{0x08, 0x0015}, {0x08, 0x0095}, {0x08, 0x0055},
	{0x08, 0x00d5}, {0x08, 0x0035}, {0x08, 0x00b5},
	{0x08, 0x0075}, {0x08, 0x00f5}, {0x08, 0x000d},
	{0x08, 0x008d}, {0x08, 0x004d}, {0x08, 0x00cd},
	{0x08, 0x002d}, {0x08, 0x00ad}, {0x08, 0x006d},
	{0x08, 0x00ed}, {0x08, 0x001d}, {0x08, 0x009d},
	{0x08, 0x005d}, {0x08, 0x00dd}, {0x08, 0x003d},
	{0x08, 0x00bd}, {0x08, 0x007d}, {0x08, 0x00fd},
	{0x09, 0x0013}, {0x09, 0x0113}, {0x09, 0x0093},
	{0x09, 0x0193}, {0x09, 0x0053}, {0x09, 0x0153},
	{0x09, 0x00d3}, {0x09, 0x01d3}, {0x09, 0x0033},
	{0x09, 0x0133}, {0x09, 0x00b3}, {0x09, 0x01b3},
	{0x09, 0x0073}, {0x09, 0x0173}, {0x09, 0x00f3},
	{0x09, 0x01f3}, {0x09, 0x000b}, {0x09, 0x010b},
	{0x09, 0x008b}, {0x09, 0x018b}, {0x09, 0x004b},
	{0x09, 0x014b}, {0x09, 0x00cb}, {0x09, 0x01cb},
	{0x09, 0x002b}, {0x09, 0x012b}, {0x09, 0x00ab},
	{0x09, 0x01ab}, {0x09, 0x006b}, {0x09, 0x016b},
	{0x09, 0x00eb}, {0x09, 0x01eb}, {0x09, 0x001b},
	{0x09, 0x011b}, {0x09, 0x009b}, {0x09, 0x019b},
	{0x09, 0x005b}, {0x09, 0x015b}, {0x09, 0x00db},
	{0x09, 0x01db}, {0x09, 0x003b}, {0x09, 0x013b},
	{0x09, 0x00bb}, {0x09, 0x01bb}, {0x09, 0x007b},
	{0x09, 0x017b}, {0x09, 0x00fb}, {0x09, 0x01fb},
	{0x09, 0x0007}, {0x09, 0x0107}, {0x09, 0x0087},
	{0x09, 0x0187}, {0x09, 0x0047}, {0x09, 0x0147},
	{0x09, 0x00c7}, {0x09, 0x01c7}, {0x09, 0x0027},
	{0x09, 0x0127}, {0x09, 0x00a7}, {0x09, 0x01a7},
	{0x09, 0x0067}, {0x09, 0x0167}, {0x09, 0x00e7},
	{0x09, 0x01e7}, {0x09, 0x0017}, {0x09, 0x0117},
	{0x09, 0x0097}, {0x09, 0x0197}, {0x09, 0x0057},
	{0x09, 0x0157}, {0x09, 0x00d7}, {0x09, 0x01d7},
	{0x09, 0x0037}, {0x09, 0x0137}, {0x09, 0x00b7},
	{0x09, 0x01b7}, {0x09, 0x0077}, {0x09, 0x0177},
	{0x09, 0x00f7}, {0x09, 0x01f7}, {0x09, 0x000f},
	{0x09, 0x010f}, {0x09, 0x008f}, {0x09, 0x018f},
	{0x09, 0x004f}, {0x09, 0x014f}, {0x09, 0x00cf},
	{0x09, 0x01cf}, {0x09, 0x002f}, {0x09, 0x012f},
	{0x09, 0x00af}, {0x09, 0x01af}, {0x09, 0x006f},
	{0x09, 0x016f}, {0x09, 0x00ef}, {0x09, 0x01ef},
	{0x09, 0x001f}, {0x09, 0x011f}, {0x09, 0x009f},
	{0x09, 0x019f}, {0x09, 0x005f}, {0x09, 0x015f},
	{0x09, 0x00df}, {0x09, 0x01df}, {0x09, 0x003f},
	{0x09, 0x013f}, {0x09, 0x00bf}, {0x09, 0x01bf},
	{0x09, 0x007f}, {0x09, 0x017f}, {0x09, 0x00ff},
	{0x09, 0x01ff}, {0x07, 0x0000}
};

static const struct THCode2 slnscodes[MAXLZCODES] = {
	{0x07, 0x00, 0x0040, 0x0003}, {0x07, 0x00, 0x0020, 0x0004},
	{0x07, 0x00, 0x0060, 0x0005}, {0x07, 0x00, 0x0010, 0x0006},
	{0x07, 0x00, 0x0050, 0x0007}, {0x07, 0x00, 0x0030, 0x0008},
	{0x07, 0x00, 0x0070, 0x0009}, {0x07, 0x00, 0x0008, 0x000a},
	{0x07, 0x01, 0x0048, 0x000b}, {0x07, 0x01, 0x0028, 0x000d},
	{0x07, 0x01, 0x0068, 0x000f}, {0x07, 0x01, 0x0018, 0x0011},
	{0x07, 0x02, 0x0058, 0x0013}, {0x07, 0x02, 0x0038, 0x0017},
	{0x07, 0x02, 0x0078, 0x001b}, {0x07, 0x02, 0x0004, 0x001f},
	{0x07, 0x03, 0x0044, 0x0023}, {0x07, 0x03, 0x0024, 0x002b},
	{0x07, 0x03, 0x0064, 0x0033}, {0x07, 0x03, 0x0014, 0x003b},
	{0x07, 0x04, 0x0054, 0x0043}, {0x07, 0x04, 0x0034, 0x0053},
	{0x07, 0x04, 0x0074, 0x0063}, {0x08, 0x04, 0x0003, 0x0073},
	{0x08, 0x05, 0x0083, 0x0083}, {0x08, 0x05, 0x0043, 0x00a3},
	{0x08, 0x05, 0x00c3, 0x00c3}, {0x08, 0x05, 0x0023, 0x00e3},
	{0x08, 0x00, 0x00a3, 0x0102}, {0x08, 0x00, 0x0063, 0x0102}
};

static const struct THCode2 sdstcodes[MAXLZCODES] = {
	{0x05, 0x00, 0x0000, 0x0001}, {0x05, 0x00, 0x0010, 0x0002},
	{0x05, 0x00, 0x0008, 0x0003}, {0x05, 0x00, 0x0018, 0x0004},
	{0x05, 0x01, 0x0004, 0x0005}, {0x05, 0x01, 0x0014, 0x0007},
	{0x05, 0x02, 0x000c, 0x0009}, {0x05, 0x02, 0x001c, 0x000d},
	{0x05, 0x03, 0x0002, 0x0011}, {0x05, 0x03, 0x0012, 0x0019},
	{0x05, 0x04, 0x000a, 0x0021}, {0x05, 0x04, 0x001a, 0x0031},
	{0x05, 0x05, 0x0006, 0x0041}, {0x05, 0x05, 0x0016, 0x0061},
	{0x05, 0x06, 0x000e, 0x0081}, {0x05, 0x06, 0x001e, 0x00c1},
	{0x05, 0x07, 0x0001, 0x0101}, {0x05, 0x07, 0x0011, 0x0181},
	{0x05, 0x08, 0x0009, 0x0201}, {0x05, 0x08, 0x0019, 0x0301},
	{0x05, 0x09, 0x0005, 0x0401}, {0x05, 0x09, 0x0015, 0x0601},
	{0x05, 0x0a, 0x000d, 0x0801}, {0x05, 0x0a, 0x001d, 0x0c01},
	{0x05, 0x0b, 0x0003, 0x1001}, {0x05, 0x0b, 0x0013, 0x1801},
	{0x05, 0x0c, 0x000b, 0x2001}, {0x05, 0x0c, 0x001b, 0x3001},
	{0x05, 0x0d, 0x0007, 0x4001}, {0x05, 0x0d, 0x0017, 0x6001}
};


#define AUTOINCLUDE_1
	#include __FILE__
#undef  AUTOINCLUDE_1

#endif
