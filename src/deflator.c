/*
 * Copyright (C) 2023, jpn
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

#include <jdeflate/deflator.h>
#include <ctoolbox/ulog2.h>

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

/* Cache size */
#define HBITS 16
#define CBITS WNDWBITS
#define QBITS 14

#define HMASK ((1ul << HBITS) - 1)
#define CMASK ((1ul << CBITS) - 1)
#define QMASK ((1ul << QBITS) - 1)

/* Number of literals (including end of block), match symbols and precodes */
#define MAXLTCODES 257
#define MAXLZCODES 32
#define MAXPCCODES 19


/* */
struct TDEFLTPrvt1 {
	uint16 mhlist[HMASK + 1];
	uint16 mchain[CMASK + 1];
};

/* */
struct TDEFLTPrvt2 {
	uint16 mhlist[HMASK + 1];
	uint16 mchain[CMASK + 1];

	uint16 shlist[QMASK + 1];
	uint16 schain[QMASK + 1];

	/* block splitting stats */
	struct TDEFLTStats {
		uint32 currobs[32];
		uint32 prevobs[32];
		uint32 obscount;
		uint32 newcount;
		uint32 obstotal;
	} stats[1];
};

/* Private stuff */
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
	uintxx aux4;
	uintxx aux5;
	uintxx aux6;

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

	/* window span end (the used part of the window buffer) */
	uint8* inputend;

	/* cursor, position in the window buffer */
	uintxx cursor;
	uintxx whence3;
	uintxx whence4;

	/* cache */
	uint16* mhlist;
	uint16* mchain;
	uint16* shlist;
	uint16* schain;

	/* match search parameters */
	uintxx nicelength;
	uintxx goodlength;
	uintxx maxchain;

	/* block splitting stats */
	struct TDEFLTStats* stats;

	/* lz token and literal buffer */
	uint16* lzlist;
	uint16* lzlistend;

	/* last lz token */
	uint16* zend;

	/* token index */
	uint16* zptr;

	/* used for any level other than 0 or 1 */
	struct TDEFLTExtra {
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

	/* custom allocator */
	struct TAllocator* allctr;
};

typedef struct TMFinder TMFinder;

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
		case 5:
			return BUILDMEMINFO(WNDWBITS + 1, 0x0f);
		case 6:
		case 7:
			return BUILDMEMINFO(WNDWBITS + 2, 0x10);
		case 8:
		case 9:
			return BUILDMEMINFO(WNDWBITS + 2, 0x11);
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
	uintxx good, nice, chain;

	switch (level) {
		case 1: good =   8; nice =   4; chain =   2; break;
		case 2: good =   8; nice =   8; chain =   8; break;
		case 3: good =   8; nice =  16; chain =  16; break;
		case 4: good =   8; nice =  32; chain =  32; break;
		case 5: good =   8; nice =  64; chain = 128; break;
		case 6: good =  16; nice =  16; chain =  48; break;
		case 7: good =  32; nice =  64; chain = 128; break;
		case 8: good =  64; nice = 128; chain = 320; break;
		case 9: good = 192; nice = 256; chain = 512; break;
		default:
			nice = 0; good = 0; chain = 0;
			break;
	}
	PRVT->goodlength = good;
	PRVT->nicelength = nice;
	PRVT->maxchain   = chain;
}


CTB_INLINE void*
request_(struct TDEFLTPrvt* prvt, uintxx amount)
{
	struct TAllocator* a;

	a = prvt->allctr;
	return a->request(amount, a->user);
}

CTB_INLINE void
dispose_(struct TDEFLTPrvt* prvt, void* memory, uintxx amount)
{
	struct TAllocator* a;

	a = prvt->allctr;
	a->dispose(memory, amount, a->user);
}

CTB_INLINE uintxx
allocateextra(TDeflator* state)
{
	uintxx i;
	uintxx n;
	struct TDEFLTExtra* e;

	if (PRVT->level > 5) {
		struct TDEFLTPrvt2* p2;

		n = sizeof(struct TDEFLTPrvt2);
		if ((e = request_(PRVT, sizeof(struct TDEFLTExtra) + n)) == NULL) {
			return 0;
		}
		p2 = (struct TDEFLTPrvt2*) (e + 1);
		PRVT->mhlist = p2->mhlist;
		PRVT->mchain = p2->mchain;
		PRVT->shlist = p2->shlist;
		PRVT->schain = p2->schain;

		PRVT->stats = p2->stats;
	}
	else {
		struct TDEFLTPrvt1* p1;

		n = sizeof(struct TDEFLTPrvt1);
		if ((e = request_(PRVT, sizeof(struct TDEFLTExtra) + n)) == NULL) {
			return 0;
		}
		p1 = (struct TDEFLTPrvt1*) (e + 1);
		PRVT->mhlist = p1->mhlist;
		PRVT->mchain = p1->mchain;
	}

	/* set the base and extra bit values */
	for (i = 0; i < MAXLZCODES; i++) {
		e->lnscodes[i] = slnscodes[i];
		e->dstcodes[i] = sdstcodes[i];
	}

	PRVT->extra = e;
	return 1;
}


#define MINMATCH   3
#define MAXMATCH 258

#if defined(CTB_ENV64)
	#define WNDNGUARDSZ (264 + (sizeof(uint64) * (4 + 1)))
#else
	#define WNDNGUARDSZ (260 + (sizeof(uint32) * (4 + 1)))
#endif

static uintxx
allocatemem(TDeflator* state, uintxx meminfo)
{
	uintxx wnsize;
	uintxx lzsize;
	void* buffer;

	PRVT->mhlist = NULL;
	PRVT->mchain = NULL;
	PRVT->shlist = NULL;
	PRVT->schain = NULL;
	PRVT->stats  = NULL;
	PRVT->extra  = NULL;

	wnsize = GETWNBFFSZ(meminfo) + WNDNGUARDSZ;
	lzsize = GETLZBFFSZ(meminfo);
	if (lzsize == 1) {
		lzsize--;
	}

	buffer = request_(PRVT, wnsize * sizeof(PRVT->window[0]));
	if (buffer == NULL) {
		return 0;
	}
	PRVT->window    = buffer;
    PRVT->windowend = PRVT->window + (wnsize - WNDNGUARDSZ);
	if (PRVT->level == 0) {
		return 1;
	}

    if (lzsize) {
	    buffer = request_(PRVT, lzsize * sizeof(PRVT->lzlist[0]));
	    if (buffer == NULL) {
		    return 0;
	    }
	    PRVT->lzlist = buffer;
    }
	PRVT->lzlistend = PRVT->lzlist + (lzsize);

	return allocateextra(state);
}

#define SETERROR(ERROR) (state->error = (ERROR))
#define SETSTATE(STATE) (state->state = (STATE))


TDeflator*
deflator_create(uintxx level, TAllocator* allctr)
{
	struct TDeflator* state;

	if (level >= 10) {
		/* invalid level */
		return NULL;
	}

	if (allctr == NULL) {
		allctr = (void*) ctb_getdefaultallocator();
	}
	state = allctr->request(sizeof(struct TDEFLTPrvt), allctr->user);
	if (state == NULL) {
		return NULL;
	}
	PRVT->allctr = allctr;

	PRVT->window = NULL;
	PRVT->lzlist = NULL;

	PRVT->level = level;
	if (allocatemem(state, getmeminfo(level)) == 0) {
		deflator_destroy(state);
		return NULL;
	}
	setparameters(state, level);

	deflator_reset(state);
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

	j = HMASK + 1;
	for (i = 0; j > i; i++) {
		PRVT->mhlist[i] = -(WNDWSIZE);
	}
	j = CMASK + 1;
	for (i = 0; j > i; i++) {
		PRVT->mchain[i] = -(WNDWSIZE);
	}

	if (PRVT->level > 5) {
		j = QMASK + 1;
		for (i = 0; j > i; i++) {
			PRVT->shlist[i] = 0;
			PRVT->schain[i] = 0;
		}
	}
}

static void
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
deflator_reset(TDeflator* state)
{
	uint8* buffer;
	uint8* end;
	CTB_ASSERT(state);

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
	PRVT->used     = 0;
	PRVT->substate = 0;

	PRVT->blockinit = 0;
	PRVT->blocktype = 0;
	PRVT->hasinput  = 0;
	PRVT->aux1 = 0;
	PRVT->aux2 = 0;
	PRVT->aux3 = 0;
	PRVT->aux4 = 0;
	PRVT->aux5 = 0;
	PRVT->aux6 = 0;

	PRVT->bbuffer = 0;
	PRVT->bcount  = 0;
	if (PRVT->level) {
		PRVT->whence3 = 0;
		PRVT->whence4 = 0;
		PRVT->cursor  = 0;

		PRVT->zend = PRVT->lzlist;
		PRVT->zptr = PRVT->lzlist;
		resetcache(state);
	}

	buffer = PRVT->window;
	for (end = PRVT->windowend + WNDNGUARDSZ; buffer < end;) {
		*buffer++ = 0;
	}
	PRVT->inputend = PRVT->window;
}

void
deflator_destroy(TDeflator* state)
{
	uintxx meminfo;

	if (state == NULL) {
		return;
	}

	if (PRVT->level) {
		uintxx n;

		if (PRVT->level > 5) {
			n = sizeof(struct TDEFLTPrvt2) + sizeof(struct TDEFLTExtra);
		}
		else {
			n = sizeof(struct TDEFLTPrvt1) + sizeof(struct TDEFLTExtra);
		}
		dispose_(PRVT, PRVT->extra, n);
	}

	meminfo = getmeminfo(PRVT->level);
	if (meminfo) {
		uintxx sz1;
		uintxx sz2;

		sz1 = GETWNBFFSZ(meminfo) + WNDNGUARDSZ;
		sz2 = GETLZBFFSZ(meminfo);

		if (PRVT->lzlist) {
			dispose_(PRVT, PRVT->lzlist, sz2 * sizeof(PRVT->lzlist[0]));
		}
		if (PRVT->window) {
			dispose_(PRVT, PRVT->window, sz1 * sizeof(PRVT->window[0]));
		}
	}
	dispose_(PRVT, state, sizeof(struct TDEFLTPrvt));
}

#undef WNDNGUARDSZ

#undef GETWINBFFSZ
#undef GETTKNBFFSZ


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


static uintxx flushblock(struct TDeflator* state);

static uintxx compress0(struct TDeflator* state);
static uintxx compress1(struct TDeflator* state);
static uintxx compress2(struct TDeflator* state);

eDEFLTResult
deflator_deflate(TDeflator* state, eDEFLTFlush flush)
{
	uintxx r;
	CTB_ASSERT(state);

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
					case 5:
						r = compress1(state);
						break;
					default:  /* 6 7 8 9 */
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
				if ((r = flushblock(state))) {
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
	outputleft = (uintxx) (PRVT->windowend - PRVT->inputend);
	sourceleft = (uintxx) (state->send - state->source);

	maxrun = MAXSTRDSZ - PRVT->aux1;
	if (maxrun > outputleft)
		maxrun = outputleft;
	if (maxrun > sourceleft)
		maxrun = sourceleft;

	ctb_memcpy(PRVT->inputend, state->source, maxrun);
	PRVT->inputend += maxrun;
	state->source  += maxrun;

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
			PRVT->aux2 = 4;
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

	for (; PRVT->aux2 < 4; state->target++) {
		if (state->tend - state->target == 0) {
			PRVT->substate = 2;
			return DEFLT_TGTEXHSTD;
		}
		state->target[0] = (uint8) (PRVT->aux3 >> (PRVT->aux2++ << 3));
	}
	PRVT->aux3 = 0;

L_STATE3:
	targetleft = (uintxx) (state->tend - state->target);

	maxrun = PRVT->aux1;
	if (maxrun > targetleft)
		maxrun = targetleft;

	buffer = PRVT->inputend - PRVT->aux1;
	ctb_memcpy(state->target, buffer, maxrun);
	state->target += maxrun;

	PRVT->aux1 -= maxrun;
	if (PRVT->aux1) {
		PRVT->substate = 3;
		return DEFLT_TGTEXHSTD;
	}

	PRVT->inputend = PRVT->window;
	goto L_LOOP;

L_DONE:
	PRVT->aux1 = 0;
	PRVT->aux2 = 0;
	PRVT->aux3 = 0;
	PRVT->substate  = 0;
	PRVT->blockinit = 0;
	return DEFLT_OK;
}


/* ***************************************************************************
 * Code Generation
 *************************************************************************** */

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
			j++;
	}
	if (j == 0) {
		frqs[0] = 1;
		frqs[1] = 1;
	}
	else {
		if (j == 1) {
			if (frqs[0]) {
				frqs[1] = 1;
			}
			else {
				frqs[0] = 1;
			}
		}
	}

	j = 0;
	for (i = 0; (uintxx) i < size; i++) {
		if (frqs[i])
			extra->smap[j++] = i;
	}

	heapsort(extra->smap, frqs, j);
	for (i = 0; j > i; i++) {
		extra->clns[i] = frqs[extra->smap[i]];
	}
	katajainen(extra->clns, j);

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

	for (i = 0; i < DEFLT_CMAXSYMBOL; i++) {
		extra->cfrqs[i] = 0;
	}

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
#define ENSURE2ON64(T, B, C) if (CTB_EXPECT1(C > 48)) { W6(T, B); C -= 48; }
#define ENSURE3ON64(T, B, C) if (CTB_EXPECT1(C > 40)) { W5(T, B); C -= 40; }
#define ENSURE4ON64(T, B, C) if (CTB_EXPECT1(C > 32)) { W4(T, B); C -= 32; }

#else

#define ENSURE2ON64(T, B, C)
#define ENSURE3ON64(T, B, C)
#define ENSURE4ON64(T, B, C)
#define ENSURE2ON32(T, B, C) if (CTB_EXPECT1(C > 16)) { W2(T, B); C -= 16; }

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
		if (CTB_EXPECT1(lzlist[0] < 0x8000)) {
			code1 = littable[lzlist[0]];

			/* 15 */
			ENSURE2ON64(target, bb, bc);
			ENSURE2ON32(target, bb, bc);
			EMIT(bb, bc, code1.code, code1.bitlen);

			if (CTB_EXPECT0(lzlist[0] == BLOCKENDSYMBOL)) {
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

		if (CTB_EXPECT1(lcode.bextra)) {
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
	if (CTB_EXPECT0(fastcheck)) {
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

		if (CTB_EXPECT0(PRVT->zptr[0] == BLOCKENDSYMBOL)) {
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
flushblock(struct TDeflator* state)
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

	PRVT->blocktype = BLOCKDNMC;

	/* force an static block for small blocks */
	if (total < 0x400 || PRVT->level == 1) {
		PRVT->blocktype = BLOCKSTTC;
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


/* ***************************************************************************
 * Mathfinder related code
 *************************************************************************** */

CTB_INLINE uintxx
slidewindow(struct TDeflator* state)
{
	uintxx r;
	uint8* b;
	uint8* w;
	uint8* end;

	b = PRVT->window + (PRVT->cursor - WNDWSIZE);
	w = PRVT->window;
#if defined(CTB_ENV64)
	r = ((uintxx) b) & (8 - 1);
#else
	r = ((uintxx) b) & (4 - 1);
#endif
	b = b - r;

	/* move the window lower part of the buffer */
	end = PRVT->inputend;
#if defined(CTB_ENV64)
	while (end - b >= 32) {
		((uint64*) w)[0] = ((uint64*) b)[0];
		((uint64*) w)[1] = ((uint64*) b)[1];
		((uint64*) w)[2] = ((uint64*) b)[2];
		((uint64*) w)[3] = ((uint64*) b)[3];
		b += 32;
		w += 32;
	}
#else
	while (end - b >= 16) {
		((uint32*) w)[0] = ((uint32*) b)[0];
		((uint32*) w)[1] = ((uint32*) b)[1];
		((uint32*) w)[2] = ((uint32*) b)[2];
		((uint32*) w)[3] = ((uint32*) b)[3];
		b += 16;
		w += 16;
	}
#endif
	while (end > b) {
		*w++ = *b++;
	}

	PRVT->inputend = w;
	PRVT->cursor   = WNDWSIZE + r;
	return (uintxx) (b - w);
}

static uintxx
fillwindow(struct TDeflator* state)
{
	uintxx total;
	uintxx wleft;

	wleft = (uintxx) (PRVT->windowend - PRVT->inputend);
	total = (uintxx) (state->send - state->source);

	if (CTB_EXPECT1(total > wleft) && (CTB_EXPECT1(wleft < 0x400))) {
		uintxx slide;

		slide = slidewindow(state);
		PRVT->whence3 -= slide;
		PRVT->whence4 -= slide;

		wleft = (uintxx) (PRVT->windowend - PRVT->inputend);
	}

	if (total > wleft) {
		total = wleft;
	}
	if (total) {
		ctb_memcpy(PRVT->inputend, state->source, total);
		state->source  += total;
		PRVT->inputend += total;
	}
	return total;
}

static void
slidehash(struct TDeflator* state)
{
	uintxx j;
	int16* buffer;

	for (j = 0, buffer = (int16*) PRVT->mhlist; j < HMASK + 1; j++) {
		buffer[j] = 0x8000 | (buffer[j] & ~(buffer[j] >> 15));
	}
	for (j = 0, buffer = (int16*) PRVT->mchain; j < CMASK + 1; j++) {
		buffer[j] = 0x8000 | (buffer[j] & ~(buffer[j] >> 15));
	}
}


#if !defined(CTB_STRICTALIGNMENT) && defined(CTB_FASTUNALIGNED)
	#define GETSHEAD4(B, N) ((*((uint32*) ((B) + (N)))))
#else
	#define GETSHEAD4(B, N) \
	    ((B)[(N) + 0] << 0x00) | \
	    ((B)[(N) + 1] << 0x08) | \
	    ((B)[(N) + 2] << 0x10) | \
	    ((B)[(N) + 3] << 0x18)
#endif

CTB_FORCEINLINE uint32
gethead(struct TDeflator* state, uintxx offset)
{
	uint32 head;

	head = GETSHEAD4(PRVT->window, offset);
	return CTB_SWAP32ONLE(head);
}

CTB_FORCEINLINE uint32
gethash(uint32 head, uintxx bits)
{
	return (uint32) (head * 0x1e35a7bd) >> (32 - bits);
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


#if defined(CTB_ENV64)
	#define PREFIXTOTAL 32
#else
	#define PREFIXTOTAL 16
#endif

CTB_FORCEINLINE uintxx
getmatchlength(uint8* p1, uint8* p2)
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
    xor = *c1++ ^ *c2++;
    if (xor) {
        goto L1;
	}
    xor = *c1++ ^ *c2++;
    if (xor) {
        goto L1;
	}
    xor = *c1++ ^ *c2++;
    if (xor) {
        goto L1;
	}
    xor = *c1++ ^ *c2++;
    if (xor) {
        goto L1;
	}

	do {
		if ((uintxx) ((uint8*) c1 - p1) >= 258 - PREFIXTOTAL) {
			return (uintxx) (((uint8*) c1) - p1);
		}
	} while (
		((xor = *c1++ ^ *c2++) == 0) &&
		((xor = *c1++ ^ *c2++) == 0) &&
		((xor = *c1++ ^ *c2++) == 0) &&
		((xor = *c1++ ^ *c2++) == 0));

L1:
	p1 = ((uint8*) (c1 - 1)) + (CTZERO(xor) >> 3);
	return (uintxx) (p1 - pp);
#else

	do {
		if ((uintxx) ((uint8*) c1 - p1) >= 258) {
			return (uintxx) (((uint8*) c1) - p1);
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
	return (uintxx) (p1 - pp);
#endif
}

#undef PREFIXTOTAL


#if defined(CTZERO)
	#undef CTZERO
#endif

#else

CTB_FORCEINLINE uintxx
getmatchlength(uint8* p1, uint8* p2)
{
	uint8* pp;

	pp = p1;
	do {
		if ((uintxx) ((uint8*) p1 - pp) >= 258) {
			return (uintxx) (p1 - pp);
		}
	} while (
		(*p1++ == *p2++) &&
		(*p1++ == *p2++) &&
		(*p1++ == *p2++) &&
		(*p1++ == *p2++));

	p1--;
	return (uintxx) (p1 - pp);
}

#endif


void
deflator_setdctnr(TDeflator* state, uint8* dict, uintxx size)
{
	uintxx i;
	CTB_ASSERT(state && dict);

	if (PRVT->level == 0) {
		return;
	}
	else {
		if (PRVT->used) {
			SETERROR(DEFLT_EINCORRECTUSE);
			SETSTATE(DEFLT_BADSTATE);
			return;
		}
	}

	if (size > WNDWSIZE) {
		dict = (dict + size) - WNDWSIZE;
		size = WNDWSIZE;
	}
	ctb_memcpy(PRVT->window, dict, size);

	if (PRVT->level > 5) {
		if (size >= 4) {
			uintxx j;

			j = size - 4;
			for (i = 0; i <= j; i++) {
				uint32 h4;
				uint32 h3;
				uint32 hs;

				hs = gethead(state, i);
				h3 = gethash(hs >> 010, QBITS);
				h4 = gethash(hs >> 000, HBITS);

				PRVT->mchain[i & CMASK] = PRVT->mhlist[h4];
				PRVT->mhlist[h4] = i;
				PRVT->schain[i & QMASK] = PRVT->shlist[h3];
				PRVT->shlist[h3] = i;
			}
		}
	}
	else {
		if (size >= 4) {
			uintxx j;

			j = size - 4;
			for (i = 0; i <= j; i++) {
				uint32 h4;

				h4 = gethash(gethead(state, i), HBITS);
				PRVT->mchain[i & CMASK] = PRVT->mhlist[h4];
				PRVT->mhlist[h4] = i;
			}
		}
	}
	PRVT->inputend += size;
	PRVT->cursor    = size;

	PRVT->used = 1;
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

CTB_INLINE uintxx
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

CTB_INLINE uintxx
getlsymbol(uintxx n)
{
	return lsymbols[n - 3];
}


/* */
struct TMatch {
	uint32 length;
	uint32 offset;
};

CTB_FORCEINLINE void
addmatch(struct TDeflator* state, struct TMatch match, uintxx ls, uintxx ds)
{
	/* token layout:
	 *        length       distance            length symbol and distance
	 * 1------a aaaaaaaa | bbbbbbbb bbbbbbbb | cccccccc dddddddd */
	PRVT->zend[0] = (uint16) (match.length | 0x8000);
	PRVT->zend[1] = (uint16) (match.offset);
	PRVT->zend++;
	PRVT->zend++;
	PRVT->zend[0] = (uint16) ((ls << 0x08) | (ds << 0x00));
	PRVT->zend++;
}

CTB_FORCEINLINE void
addliteral(struct TDeflator* state, uintxx literal)
{
	PRVT->zend[0] = (uint16) literal;
	PRVT->zend++;
}


#if defined(__GNUC__)
	#define PREFETCH(A) __builtin_prefetch((A), 1)
#endif

#if !defined(PREFETCH)
	#define PREFETCH(A) ((void) (A))
#endif

#define MINLOOKAHEAD (MINMATCH + MAXMATCH)


/* ***************************************************************************
 * Greedy parser (level 1 2 3 4 5)
 *************************************************************************** */

CTB_FORCEINLINE struct TMatch
getmatch1(struct TDeflator* state, uint32 length, uint32 hash[1])
{
	uint8* strbgn;
	uint8* strend;
	uint8* pmatch;
	uint8* offset;
	uint32 chain;
	uint32 position4;
	uint32 head;
	uint32 h4;
	int16 next4;
	int16 limit;

	strbgn = strend = PRVT->window + PRVT->cursor;
	strend = strend + MAXMATCH;
	if (strend > PRVT->inputend) {
		strend = PRVT->inputend;
	}
	offset = strbgn;

	position4 = (uint16) (PRVT->cursor - PRVT->whence4);
	if (CTB_EXPECT0(position4 == WNDWSIZE)) {
		slidehash(state);
		PRVT->whence4 += WNDWSIZE;
		position4 = 0;
	}
	h4 = hash[0];
	next4 = PRVT->mhlist[h4];

	PRVT->mchain[position4 & CMASK] = PRVT->mhlist[h4];
	PRVT->mhlist[h4] = position4;

	head = gethead(state, PRVT->cursor + 1);
	h4 = gethash(head, HBITS);
	PREFETCH(&PRVT->mhlist[h4]);
	hash[0] = h4;

	chain = PRVT->maxchain;
	for (limit = position4 - WNDWSIZE; chain != 0; chain--) {
		if (next4 <= limit) {
			break;
		}

		pmatch = PRVT->window + (PRVT->whence4 + next4);
		if (strbgn[length] == pmatch[length]) {
			uintxx n;

			n = getmatchlength(strbgn, pmatch);
			if (n > length) {
				length = n;
				offset = pmatch;
				if (length >= PRVT->nicelength) {
					break;
				}
			}
		}

		next4 = PRVT->mchain[next4 & CMASK];
	}

	if (strbgn + length > strend) {
		length -= (uintxx) ((strbgn + length) - strend);
	}
	return (struct TMatch){ length, (uintxx) (strbgn - offset) };
}

CTB_FORCEINLINE void
skipbytes1(struct TDeflator* state, uintxx skip, uintxx total, uint32 hash[1])
{
	uint32 hs;
	uint32 h4;
	uint32 position4;

	h4 = hash[0];
	for (; skip < total; skip++) {
		PRVT->cursor++;

		position4 = (uint16) (PRVT->cursor - PRVT->whence4);
		if (CTB_EXPECT0(position4 == WNDWSIZE)) {
			slidehash(state);
			PRVT->whence4 += WNDWSIZE;
			position4 = 0;
		}

		PRVT->mchain[position4 & CMASK] = PRVT->mhlist[h4];
		PRVT->mhlist[h4] = position4;

		hs = gethead(state, PRVT->cursor + 1);
		h4 = gethash(hs, HBITS);
	}
	PREFETCH(&PRVT->mhlist[h4]);
	hash[0] = h4;
}

static uintxx
compress1(struct TDeflator* state)
{
	uintxx limit;
	uintxx srcleft;
	uintxx r;
	uintxx* lnsfrqs;
	uintxx* dstfrqs;
	uintxx* litfrqs;
	uint32 hash[1];

	if (PRVT->blockinit == 0) {
		resetfreqs(state);

		PRVT->blockinit = 1;
	}
	litfrqs = PRVT->extra->lfrqs;
	dstfrqs = PRVT->extra->dfrqs;
	lnsfrqs = PRVT->extra->lfrqs + MAXLTCODES;

	hash[0] = PRVT->aux4;

L_LOOP:
	limit = (uintxx) (PRVT->inputend - PRVT->window);
	if (limit - PRVT->cursor > MINLOOKAHEAD + 1) {
		srcleft = (uintxx) (state->send - state->source);
		if (state->flush == 0 || srcleft) {
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

	while (limit > PRVT->cursor) {
		struct TMatch match;

		match = getmatch1(state, MINMATCH, hash);
		if (match.length > MINMATCH) {
			uintxx lsymbol;
			uintxx dsymbol;

			lsymbol = getlsymbol(match.length);
			dsymbol = getdsymbol(match.offset);
			lnsfrqs[lsymbol]++;
			dstfrqs[dsymbol]++;
			addmatch(state, match, lsymbol, dsymbol);

			skipbytes1(state, 1, match.length, hash);
		}
		else {
			uintxx c;

			c = PRVT->window[PRVT->cursor];
			addliteral(state, c);
			litfrqs[c]++;
		}

		PRVT->cursor++;
		if (CTB_EXPECT0(PRVT->zend + 4 > PRVT->lzlistend)) {
			/* flush */
			SETSTATE(1);

			PRVT->aux4 = hash[0];
			PRVT->hasinput = 1;
			return 0;
		}
	}

	PRVT->aux4 = hash[0];
	r = fillwindow(state);
	if (r) {
		goto L_LOOP;
	}

	if (state->flush) {
		SETSTATE(1);
		PRVT->hasinput = 0;
		/* no more input */
		return 0;
	}
	return DEFLT_SRCEXHSTD;
}


/* ***************************************************************************
 * Lazy parser (level 6 7 8 9)
 *************************************************************************** */

CTB_FORCEINLINE void
obsmatch(struct TDEFLTStats* stats, struct TMatch match, uintxx ls)
{
	stats->currobs[16 + (ls >> 1)] += 1;
	stats->newcount++;
	stats->obstotal += match.length;
}

CTB_FORCEINLINE void
obsliteral(struct TDEFLTStats* stats, uintxx literal)
{
	stats->currobs[literal >> 4] += 1;
	stats->newcount++;
	stats->obstotal++;
}

CTB_FORCEINLINE void
resetobservations(struct TDEFLTStats* stats)
{
	uintxx j;
	for (j = 0; j < 32; j++) {
		stats->currobs[j] = 0;
		stats->prevobs[j] = 0;
	}
	stats->obscount = 0;
	stats->newcount = 0;
	stats->obstotal = 0;
}

CTB_FORCEINLINE uintxx
shouldsplit(struct TDEFLTStats* stats)
{
	uintxx j;

	if (stats->obscount > 0) {
		uintxx delta;

		delta = 0;
		for (j = 0; j < 32; j++) {
			uintxx a;
			uintxx b;

			a = stats->prevobs[j];
			b = stats->currobs[j];
			if (a > b) {
				delta += a - b;
			}
			else {
				delta += b - a;
			}
		}

		if (delta >= 320 && stats->obstotal >= 7168) {
			resetobservations(stats);
			return 1;
		}
	}

	for (j = 0; j < 32; j++) {
		uintxx m;

		m = (stats->prevobs[j] >> 1) + (stats->currobs[j] >> 1);
		stats->prevobs[j] = m;
		stats->currobs[j] = 0;
	}

	stats->obscount = stats->obscount + stats->newcount;
	stats->newcount = 0;
	return 0;
}


#if defined(CTB_IS_LITTLEENDIAN)
	#define U32BITMASK 0x00fffffful
#else
	#define U32BITMASK 0xffffff00ul
#endif

CTB_FORCEINLINE struct TMatch
getmatch2(struct TDeflator* state, uint32 length, uint32 hash[2], bool doshort)
{
	uint8* strbgn;
	uint8* strend;
	uint8* pmatch;
	uint8* offset;
	uint32 chain;
	uint32 position4;
	uint32 position3;
	uint32 head;
	uint32 h3;
	uint32 h4;
	int16 next4;
	int16 next3;
	int16 limit;

	strbgn = strend = PRVT->window + PRVT->cursor;
	strend = strend + MAXMATCH;
	if (strend > PRVT->inputend) {
		strend = PRVT->inputend;
	}
	offset = strbgn;

	position4 = (uint16) (PRVT->cursor - PRVT->whence4);
	position3 = (uint16) (PRVT->cursor - PRVT->whence3);
	if (CTB_EXPECT0(position4 == WNDWSIZE)) {
		slidehash(state);
		PRVT->whence4 += WNDWSIZE;
		position4 = 0;
	}
	h3 = hash[0];
	h4 = hash[1];
	next3 = PRVT->shlist[h3];
	next4 = PRVT->mhlist[h4];

	PRVT->mchain[position4 & CMASK] = PRVT->mhlist[h4];
	PRVT->mhlist[h4] = position4;
	PRVT->schain[position3 & QMASK] = PRVT->shlist[h3];
	PRVT->shlist[h3] = position3;

	head = gethead(state, PRVT->cursor + 1);
	hash[0] = gethash(head >> 010, QBITS);
	hash[1] = gethash(head >> 000, HBITS);

	chain = PRVT->maxchain;
	if (length >= 3) {
		chain = chain >> 1;
	}

	for (limit = position4 - WNDWSIZE; chain != 0; chain--) {
		if (next4 <= limit) {
			break;
		}

		pmatch = PRVT->window + (PRVT->whence4 + next4);
		if (strbgn[length] == pmatch[length]) {
			uintxx n;

			n = getmatchlength(strbgn, pmatch);
			if (n > length) {
				length = n;
				offset = pmatch;
				if (length >= PRVT->nicelength) {
					goto L_L1;
				}
			}
		}
		next4 = PRVT->mchain[next4 & CMASK];
	}

	if (CTB_EXPECT0(doshort && length < 3)) {
		uint32 s1;
		uintxx noffset;

		s1 = GETSHEAD4(strbgn, 0);
		if (next3 == 0) {
			goto L_L1;
		}
		noffset = (uint16) (position3 - next3);
		if (noffset > WNDWSIZE || noffset == 0) {
			goto L_L1;
		}

		pmatch = strbgn - noffset;
		if (((GETSHEAD4(pmatch, 0) ^ s1) & U32BITMASK) == 0) {
			length = 3;
			offset = pmatch;
			goto L_L1;
		}

		next3 = PRVT->schain[next3 & QMASK];
		if (next3 == 0) {
			goto L_L1;
		}
		noffset = (uint16) (position3 - next3);
		if (noffset > WNDWSIZE || noffset == 0) {
			goto L_L1;
		}

		pmatch = strbgn - noffset;
		if (((GETSHEAD4(pmatch, 0) ^ s1) & U32BITMASK) == 0) {
			length = 3;
			offset = pmatch;
			goto L_L1;
		}
	}

L_L1:
	PREFETCH(&PRVT->shlist[hash[0]]);
	PREFETCH(&PRVT->mhlist[hash[1]]);

	if (strbgn + length > strend) {
		length -= (uintxx) ((strbgn + length) - strend);
	}
	return (struct TMatch){ length, (uintxx) (strbgn - offset) };
}

#undef U32BITMASK

CTB_FORCEINLINE void
skipbytes2(struct TDeflator* state, uintxx skip, uintxx total, uint32 hash[2])
{
	uint32 hs;
	uint32 h3;
	uint32 h4;
	uint32 position4;
	uint32 position3;

	h3 = hash[0];
	h4 = hash[1];
	for (; skip < total; skip++) {
		PRVT->cursor++;

		position4 = (uint16) (PRVT->cursor - PRVT->whence4);
		position3 = (uint16) (PRVT->cursor - PRVT->whence3);
		if (CTB_EXPECT0(position4 == WNDWSIZE)) {
			slidehash(state);
			PRVT->whence4 += WNDWSIZE;
			position4 = 0;
		}

		PRVT->mchain[position4 & CMASK] = PRVT->mhlist[h4];
		PRVT->mhlist[h4] = position4;
		PRVT->schain[position3 & QMASK] = PRVT->shlist[h3];
		PRVT->shlist[h3] = position3;

		hs = gethead(state, PRVT->cursor + 1);
		h3 = gethash(hs >> 010, QBITS);
		h4 = gethash(hs >> 000, HBITS);
	}
	PREFETCH(&PRVT->shlist[h3]);
	PREFETCH(&PRVT->mhlist[h4]);
	hash[0] = h3;
	hash[1] = h4;
}

static uintxx
compress2(struct TDeflator* state)
{
	uintxx limit;
	uintxx srcleft;
	uintxx r;
	uintxx* lnsfrqs;
	uintxx* dstfrqs;
	uintxx* litfrqs;
	struct TDEFLTStats* stats;
	struct TMatch match;
	struct TMatch prevm;
	bool hasmatch;
	bool doshortmatches;
	uint32 hash[2];

	stats = PRVT->stats;
	if (PRVT->blockinit == 0) {
		resetfreqs(state);
		resetobservations(stats);

		PRVT->blockinit = 1;
	}

	litfrqs = PRVT->extra->lfrqs;
	dstfrqs = PRVT->extra->dfrqs;
	lnsfrqs = PRVT->extra->lfrqs + MAXLTCODES;

	match.length = (PRVT->aux5 >> 0x00) & 0xffff;
	match.offset = (PRVT->aux5 >> 0x10) & 0xffff;
	prevm.length = 0;
	prevm.offset = 0;
	hasmatch = 0;
	if (match.length) {
		hasmatch = 1;
	}
	doshortmatches = PRVT->aux6;
	hash[0] = PRVT->aux3;
	hash[1] = PRVT->aux4;

L_LOOP:
	limit = (uintxx) (PRVT->inputend - PRVT->window);
	if (limit - PRVT->cursor > MINLOOKAHEAD + 1) {
		srcleft = (uintxx) (state->send - state->source);
		if (state->flush == 0 || srcleft) {
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

	while (limit > PRVT->cursor) {
		if (hasmatch == 0) {
			match = getmatch2(state, MINMATCH - 1, hash, doshortmatches);
			if (match.length == MINMATCH && match.offset > 8192) {
				match.length = MINMATCH - 1;
			}

			if (match.length >= MINMATCH) {
				if (match.length >= PRVT->goodlength) {
					uintxx lsymbol;
					uintxx dsymbol;

					lsymbol = getlsymbol(match.length);
					dsymbol = getdsymbol(match.offset);
					lnsfrqs[lsymbol]++;
					dstfrqs[dsymbol]++;
					skipbytes2(state, 1, match.length, hash);

					addmatch(state, match, lsymbol, dsymbol);
					obsmatch(stats, match, lsymbol);
				}
				else {
					hasmatch = 1;
				}
			}
			else {
				uintxx c;

				c = PRVT->window[PRVT->cursor];
				addliteral(state, c);
				litfrqs[c]++;
				obsliteral(stats, c);
			}
		}
		else {
			bool acceptmatch;

			prevm = match;
			match = getmatch2(state, prevm.length - 1, hash, 0);
			if (match.length >= prevm.length) {
				int32 distance;

				distance = match.length - prevm.length;
				if (distance > 4) {
					acceptmatch = 1;
				}
				else {
					int32 l1;
					int32 l2;

					l1 = ctb_u32log2(prevm.offset);
					l2 = ctb_u32log2(match.offset);
					acceptmatch = (distance << 2) + (l1 - l2) >= 2;
				}
			}
			else {
				acceptmatch = 0;
			}

			if (acceptmatch) {
				uintxx c;

				c = PRVT->window[PRVT->cursor - 1];
				addliteral(state, c);
				litfrqs[c]++;
				obsliteral(stats, c);
			}
			else {
				uintxx lsymbol;
				uintxx dsymbol;

				lsymbol = getlsymbol(prevm.length);
				dsymbol = getdsymbol(prevm.offset);
				lnsfrqs[lsymbol]++;
				dstfrqs[dsymbol]++;
				skipbytes2(state, 2, prevm.length, hash);

				addmatch(state, prevm, lsymbol, dsymbol);
				obsmatch(stats, prevm, lsymbol);
				hasmatch = 0;
			}
		}

		PRVT->cursor++;
		if (CTB_EXPECT0(PRVT->zend + 4 > PRVT->lzlistend)) {
			/* flush */
			SETSTATE(1);
			resetobservations(stats);

			PRVT->aux3 = hash[0];
			PRVT->aux4 = hash[1];
			PRVT->aux5 = 0;
			if (hasmatch) {
				PRVT->aux5 |= match.length << 0x00;
				PRVT->aux5 |= match.offset << 0x10;
			}
			PRVT->aux6 = doshortmatches;
			PRVT->hasinput = 1;
			return 0;
		}

		if (CTB_EXPECT0(stats->newcount >= 512 && stats->obstotal >= 4096)) {
			if (stats->currobs[0] >= 16) {
				doshortmatches = 1;
			}
			else {
				doshortmatches = 0;
			}
			if (shouldsplit(stats)) {
				SETSTATE(1);

				PRVT->aux3 = hash[0];
				PRVT->aux4 = hash[1];
				PRVT->aux5 = 0;
				if (hasmatch) {
					PRVT->aux5 |= match.length << 0x00;
					PRVT->aux5 |= match.offset << 0x10;
				}
				PRVT->aux6 = doshortmatches;
				PRVT->hasinput = 1;
				return 0;
			}
		}
	}

	r = fillwindow(state);
	if (r) {
		goto L_LOOP;
	}
	else {
		PRVT->aux3 = hash[0];
		PRVT->aux4 = hash[1];
		PRVT->aux5 = 0;
		if (hasmatch) {
			PRVT->aux5 |= match.length << 0x00;
			PRVT->aux5 |= match.offset << 0x10;
		}
		PRVT->aux6 = doshortmatches;
	}

	if (state->flush) {
		SETSTATE(1);
		PRVT->hasinput = 0;
		/* no more input */
		return 0;
	}
	return DEFLT_SRCEXHSTD;
}

#undef PREFETCH
#undef MINLOOKAHEAD

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
	#include "deflator.c"
#undef  AUTOINCLUDE_1

#endif
