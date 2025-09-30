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

#include <jdeflate/inflator.h>


/* Deflate format definitions */
#define DEFLT_MAXBITS 15
#define DEFLT_WINDOWSIZE 32768

#define DEFLT_LMAXSYMBOL 288
#define DEFLT_DMAXSYMBOL 32
#define DEFLT_CMAXSYMBOL 19


/* Root bits for main tables */
#define LROOTBITS 10
#define DROOTBITS 8
#define CROOTBITS 7


/* These values were calculated using enough in zlib/examples, if the
 * rootbits values are changed these values must be recalculated */
#if LROOTBITS ==  8
	#define ENOUGHL 660
#endif
#if LROOTBITS ==  9
	#define ENOUGHL 852
#endif
#if LROOTBITS == 10
	#define ENOUGHL 1332
#endif
#if LROOTBITS == 11
	#define ENOUGHL 2340
#endif

#if DROOTBITS ==  8
	#define ENOUGHD 400
#endif
#if DROOTBITS ==  9
	#define ENOUGHD 592
#endif
#if DROOTBITS == 10
	#define ENOUGHD 1072
#endif
#if DROOTBITS == 11
	#define ENOUGHD 2080
#endif

#if !defined(ENOUGHL) || !defined(ENOUGHD)
	#error "table size not defined"
#endif

/* Window buffer size, this must be greather than or equal
 * to 32768 + 258 + 32 */
#define WNDWSIZE 65536


/* Private stuff */
struct TINFLTPrvt {
	/* public fields */
	struct TINFLTPblc {
		/* state */
		uint32 state;
		uint32 error;
		uint32 flags;
		uint32 finalinput;

		/* last result from inflate call */
		uint32 status;

		/* stream buffers */
		const uint8* source;
		const uint8* sbgn;
		const uint8* send;

		uint8* target;
		uint8* tbgn;
		uint8* tend;
	} public;

    /* 
	 * Internal flag:
	 * When set, the window buffer is used directly as the
	 * output buffer. This allows us to avoid updating the window buffer after
	 * each inflate call.
     * When enabled, the public fields 'target', 'tbgn', and 'tend' will be set
	 * after each inflate call to point into the window buffer. */
	uintxx towindow;

	/* state */
	uint32 substate;
	uint32 final;
	uint32 used;

	/* auxiliar fields */
	uintxx aux0;
	uintxx aux1;
	uintxx aux2;
	uintxx aux3;
	uintxx aux4;

	/* decoding tables */
	uint32* ltable;
	uint32* dtable;

	/* custom allocator */
	const struct TAllocator* allctr;

	/* bit buffer */
#if defined(CTB_ENV64)
	uint64 bbuffer;
#else
	uint32 bbuffer;
#endif
	uintxx bcount;

	/* window buffer end and total bytes */
	uintxx wndwend;
	uintxx wndwcnt;

	/* dynamic tables (trees) */
	struct TTINFLTTables {
		/* */
		uint32 symbols[ENOUGHL + ENOUGHD];

		uint16 lengths[
			DEFLT_LMAXSYMBOL +
			DEFLT_DMAXSYMBOL
		];
	}
	*tables;

	/* window buffer */
	uint8 wndwbuffer[1];
};

/* */
typedef union {
	char a[-1 + (sizeof(struct TInflator) == sizeof(struct TINFLTPblc)) * 2];
} TINFLTStaticAssert;


#define PRVT ((struct TINFLTPrvt*) state)
#define PBLC ((struct TINFLTPblc*) state)

TInflator*
inflator_create(uintxx flags, const TAllocator* allctr)
{
	uintxx n;
	struct TInflator* state;

	n = sizeof(struct TINFLTPrvt) + WNDWSIZE + 32;
	if (allctr == NULL) {
		allctr = (const void*) ctb_getdefaultallocator();
	}

	state = allctr->request(n, allctr->user);
	if (state == NULL) {
		return NULL;
	}
	PRVT->allctr = allctr;

    PRVT->tables = NULL;
	inflator_reset(state);
	if (PBLC->error) {
		inflator_destroy(state);
		return NULL;
	}

	PBLC->flags = (uint32) flags;
	return state;
}


#define SETSTATE(STATE) (PBLC->state = (STATE))
#define SETERROR(ERROR) (PBLC->error = (ERROR))

void
inflator_reset(TInflator* state)
{
	CTB_ASSERT(state);

	/* public fields */
	PBLC->state = 0;
	PBLC->error = 0;
	PBLC->finalinput = 0;
	PBLC->status = 0;

	PBLC->source = NULL;
	PBLC->sbgn   = NULL;
	PBLC->send   = NULL;
	PBLC->target = NULL;
	PBLC->tbgn   = NULL;
	PBLC->tend   = NULL;

	/* private fields */
	PRVT->towindow = 0;
	PRVT->final    = 0;
	PRVT->substate = 0;

	PRVT->used = 0;
	PRVT->aux0 = 0;
	PRVT->aux1 = 0;
	PRVT->aux2 = 0;
	PRVT->aux3 = 0;
	PRVT->aux4 = 0;

	PRVT->bbuffer = 0;
	PRVT->bcount  = 0;
	PRVT->wndwend = 0;
	PRVT->wndwcnt = 0;

	/* */
	if (PRVT->tables == NULL) {
		const struct TAllocator* a;

		a = PRVT->allctr;
		PRVT->tables = a->request(sizeof(struct TTINFLTTables), a->user);
		if (PRVT->tables == NULL) {
			goto L_ERROR;
		}
	}
	return;

L_ERROR:
	SETERROR(INFLT_EOOM);
	SETSTATE(0xDEADBEEF);
}

void
inflator_destroy(TInflator* state)
{
	const struct TAllocator* a;

	if (state == NULL) {
		return;
	}

	a = PRVT->allctr;
	if (PRVT->tables) {
		a->dispose(PRVT->tables, sizeof(struct TTINFLTTables), a->user);
	}
	a->dispose(PRVT, sizeof(struct TINFLTPrvt) + WNDWSIZE + 32, a->user);
}


CTB_FORCEINLINE uint32
reversecode(uint32 code, uintxx length)
{
#if __has_builtin(__builtin_bitreverse32) && defined(__aarch64__)
	return __builtin_bitreverse32(code) >> (0x20 - length);
#else
	static const unsigned char rtable[] = {
		0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0,
		0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
		0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8,
		0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
		0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4,
		0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
		0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec,
		0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
		0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2,
		0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
		0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea,
		0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
		0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6,
		0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
		0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee,
		0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
		0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1,
		0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
		0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9,
		0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
		0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5,
		0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
		0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed,
		0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
		0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3,
		0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
		0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb,
		0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
		0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7,
		0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
		0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef,
		0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff
	};

	if (length > 8) {
		uint32 a;
		uint32 b;
		uint32 r;

		a = (uint8) (code >> 0);
		b = (uint8) (code >> 8);
		r = ((uint32) rtable[a]) << 8 | (uint32) rtable[b];
		return r >> (0x10 - length);
	}
	return (uint32) ((uint32) rtable[code]) >> (0x08 - length);
#endif
}


/* Tags for the table entries */
#define TAG_LIT 0x80000000  /* literal */
#define TAG_END 0x00004000  /* end of block */
#define TAG_SUB 0x00002000  /* subtable */


/* Entry layout
 * Literals:
 * txxxxxxx vvvvvvvv xxxxxxxx xxxxllll
 * 
 * Length or distance (or subtable offset)
 * vvvvvvvv vvvvvvvv tttteeee xxxxllll */

#define DOENTRY(BASE, EXTRA) (((BASE) << 16) | ((EXTRA) << 8))

/* Length base value and extra bits */
static const uint32 lnsinfo[] = {
	DOENTRY(0x0000, 0x00), DOENTRY(0x0003, 0x00),
	DOENTRY(0x0004, 0x00), DOENTRY(0x0005, 0x00),
	DOENTRY(0x0006, 0x00), DOENTRY(0x0007, 0x00),
	DOENTRY(0x0008, 0x00), DOENTRY(0x0009, 0x00),
	DOENTRY(0x000a, 0x00), DOENTRY(0x000b, 0x01),
	DOENTRY(0x000d, 0x01), DOENTRY(0x000f, 0x01),
	DOENTRY(0x0011, 0x01), DOENTRY(0x0013, 0x02),
	DOENTRY(0x0017, 0x02), DOENTRY(0x001b, 0x02),
	DOENTRY(0x001f, 0x02), DOENTRY(0x0023, 0x03),
	DOENTRY(0x002b, 0x03), DOENTRY(0x0033, 0x03),
	DOENTRY(0x003b, 0x03), DOENTRY(0x0043, 0x04),
	DOENTRY(0x0053, 0x04), DOENTRY(0x0063, 0x04),
	DOENTRY(0x0073, 0x04), DOENTRY(0x0083, 0x05),
	DOENTRY(0x00a3, 0x05), DOENTRY(0x00c3, 0x05),
	DOENTRY(0x00e3, 0x05), DOENTRY(0x0102, 0x00)
};

/* Distance base value and extra bits */
static const uint32 dstinfo[] = {
	DOENTRY(0x0001, 0x00), DOENTRY(0x0002, 0x00),
	DOENTRY(0x0003, 0x00), DOENTRY(0x0004, 0x00),
	DOENTRY(0x0005, 0x01), DOENTRY(0x0007, 0x01),
	DOENTRY(0x0009, 0x02), DOENTRY(0x000d, 0x02),
	DOENTRY(0x0011, 0x03), DOENTRY(0x0019, 0x03),
	DOENTRY(0x0021, 0x04), DOENTRY(0x0031, 0x04),
	DOENTRY(0x0041, 0x05), DOENTRY(0x0061, 0x05),
	DOENTRY(0x0081, 0x06), DOENTRY(0x00c1, 0x06),
	DOENTRY(0x0101, 0x07), DOENTRY(0x0181, 0x07),
	DOENTRY(0x0201, 0x08), DOENTRY(0x0301, 0x08),
	DOENTRY(0x0401, 0x09), DOENTRY(0x0601, 0x09),
	DOENTRY(0x0801, 0x0a), DOENTRY(0x0c01, 0x0a),
	DOENTRY(0x1001, 0x0b), DOENTRY(0x1801, 0x0b),
	DOENTRY(0x2001, 0x0c), DOENTRY(0x3001, 0x0c),
	DOENTRY(0x4001, 0x0d), DOENTRY(0x6001, 0x0d)
};


#define LTABLEMODE 0
#define DTABLEMODE 1
#define CTABLEMODE 2

static uint32
buildtable(uint16* lengths, uintxx n, uint32* table, uintxx mode)
{
	intxx left;
	intxx i;
	intxx j;
	uintxx limit;
	uint32 code;
	uintxx mlen;
	uintxx symbol;
	uintxx length;
	uintxx mbits;
	uintxx mmask;
	uint32* entry;
	const uint32* sinfo;

	uint16 counts[DEFLT_MAXBITS + 1];
	uint16 ncodes[DEFLT_MAXBITS + 1];

	sinfo = dstinfo;
	switch (mode) {
		case LTABLEMODE: mbits = LROOTBITS; sinfo = lnsinfo - 256; break;
		case DTABLEMODE: mbits = DROOTBITS; break;
		default:
			mbits = CROOTBITS;
	}

	limit = ENOUGHD;
	if (mode == LTABLEMODE) {
		limit = ENOUGHL;
	}
	for (i = 0; (uintxx) i < limit; i++) {
		table[i] = 0x00;
	}

	for (i = 0; i <= DEFLT_MAXBITS; i++) {
		counts[i] = 0;
		ncodes[i] = 0;
	}

	/* count the number of lengths for each length */
	i = (intxx) n - 1;
	while (i >= 0) {
		counts[lengths[i--]] += 1;
	}

	if (counts[0] == n) {
		/* RFC:
		 * One distance code of zero bits means that there are no distance
		 * codes used at all (the data is all literals). */
		if (mode == DTABLEMODE) {
			return 0;
		}
		/* we need at least one symbol (256) for literal-length codes */
		return INFLT_ERROR;
	}
	counts[0] = 0;

	/* get the longest length */
	i = DEFLT_MAXBITS;
	while (counts[i] == 0) {
		i--;
	}
	mlen = (uintxx) i;

	/* check for vality */
	left = 1;
	for (i = 1; i <= DEFLT_MAXBITS; i++) {
		left = (left << 1) - counts[i];

		if (left < 0) {
			/* over subscribed */
			return INFLT_ERROR;
		}
	}
	if (left) {
		/* incomplete */
		if (mlen != 1) {
			return INFLT_ERROR;
		}
		else {
			if (mode != DTABLEMODE) {
				return INFLT_ERROR;
			}
		}
	}

	/* determine the first codeword of each length */
	code = 0;
	for (i = 1; (uintxx) i <= mlen; i++) {
		code = (code + counts[i - 1]) << 1;
		ncodes[i] = (uint16) code;
	}
	mmask = ((uintxx) 1 << mbits) - 1;

	if (mlen > mbits) {
		uintxx offset;
		uintxx r;
		intxx count;

		/* here we mark the entries on the main table as sub tables */
		offset = mmask + 1;
		for (r = mlen - mbits; r; r--) {
			count = counts[mbits + r];
			if (count == 0) {
				continue;
			}

			j = count >> r;
			if (count & ((1l << r) - 1)) {
				j++;
			}

			code = ((uint32) ncodes[mbits + r]) >> r;
			for (i = 0; i < j; i++, code++) {
				entry = table + reversecode(code, mbits);
				if (entry[0] & TAG_SUB) {
					continue;
				}
				entry[0] = (uint32) (TAG_SUB | (offset << 16) | (mbits + r));

				offset += (uintxx) 1 << r;
				if (offset > limit) {
					return INFLT_ERROR;
				}
			}
		}
	}

	/* populate the table */
	code = 0;
	for (symbol = 0; symbol < n; symbol++) {
		uint32 e;

		length = lengths[symbol];
		if (length == 0) {
			continue;
		}

		if (mode == DTABLEMODE || symbol >= 256) {
			if (symbol == 256) {
				e = TAG_END;
			}
			else {
				e = sinfo[symbol];
			}
		}
		else {
			e = (uint32) symbol << 16;
			if (mode == LTABLEMODE) {
				e |= TAG_LIT;
			}
		}
		e |= (uint32) length;

		code = reversecode((uint32) ncodes[length], length);
		ncodes[length] = ncodes[length] + 1;
		if (length > mbits) {
			uint32 s;

			/* secondary table */
			s = table[code & mmask];
			j = ((uint8) s) - (intxx) length;
			i = (intxx) (s >> 16);

			length -= mbits;
			code  >>= mbits;
		}
		else {
			j = (intxx) (mbits - length);
			i = 0;
		}

		for (j = (1l << j) - 1; j >= 0; j--) {
			table[(uint32) i + (code | ((uint32) j << length))] = e;
		}
	}

	return 0;
}


#if defined(CTB_ENV64)
typedef uint64 bitbuffer;
#else
typedef uint32 bitbuffer;
#endif

/*
 * BIT input operations */

CTB_FORCEINLINE bool
tryreadbits(struct TINFLTPrvt* state, uintxx n)
{
	for(; n > PRVT->bcount; PRVT->bcount += 8) {
		if (PBLC->source >= PBLC->send) {
			return 0;
		}
		PRVT->bbuffer |= ((bitbuffer) *PBLC->source++) << PRVT->bcount;
	}
	return 1;
}

CTB_FORCEINLINE bool
fetchbyte(struct TINFLTPrvt* state)
{
	if (PBLC->source < PBLC->send) {
		PRVT->bbuffer |= ((bitbuffer) *PBLC->source++) << PRVT->bcount;
		PRVT->bcount += 8;
		return 1;
	}
	return 0;
}

CTB_FORCEINLINE void
dropbits(struct TINFLTPrvt* state, uintxx n)
{
	PRVT->bbuffer = PRVT->bbuffer >> n;
	PRVT->bcount -= n;
}

CTB_FORCEINLINE uintxx
readbits(struct TINFLTPrvt* state, uintxx n)
{
	return PRVT->bbuffer & ((((bitbuffer) 1) << n) - 1);
}


static void
updatewindow(struct TINFLTPrvt* state)
{
	uintxx total;
	uint8* begin;

	total = (uintxx) (PBLC->target - PBLC->tbgn);
	if (total == 0) {
		return;
	}

	if (total >= DEFLT_WINDOWSIZE) {
		if (PRVT->towindow == 0) {
			begin = PBLC->target - DEFLT_WINDOWSIZE;
			ctb_memcpy(PRVT->wndwbuffer, begin, DEFLT_WINDOWSIZE);

			PRVT->wndwend = DEFLT_WINDOWSIZE;
		}
		else {
			if (PRVT->wndwend == WNDWSIZE) {
				PRVT->wndwend = 0;
			}
			PRVT->wndwend += total;
		}
		PRVT->wndwcnt = DEFLT_WINDOWSIZE;
		return;
	}

	if (PRVT->wndwcnt < DEFLT_WINDOWSIZE) {
		PRVT->wndwcnt += total;
		if (PRVT->wndwcnt > DEFLT_WINDOWSIZE)
			PRVT->wndwcnt = DEFLT_WINDOWSIZE;
	}

	if (PRVT->towindow == 0) {
		uintxx maxrun;

		maxrun = WNDWSIZE - PRVT->wndwend;
		if (total < maxrun)
			maxrun = total;

		begin = PBLC->target - total;
		ctb_memcpy(PRVT->wndwbuffer + PRVT->wndwend, begin, maxrun);

		total -= maxrun;
		if (total) {
			ctb_memcpy(PRVT->wndwbuffer, begin + maxrun, total);
			PRVT->wndwend = total;
		}
		else {
			PRVT->wndwend = PRVT->wndwend + maxrun;
		}
	}
	else {
		if (PRVT->wndwend == WNDWSIZE) {
			PRVT->wndwend = 0;
		}
		PRVT->wndwend += total;
	}
}


#if LROOTBITS == 10 && DROOTBITS == 8

static const uint32* dsttctable;
static const uint32* lsttctable;

#endif

CTB_INLINE void
setstatictables(struct TINFLTPrvt* state)
{
#if LROOTBITS == 10 && DROOTBITS == 8
	PRVT->ltable = CTB_CONSTCAST(lsttctable);
	PRVT->dtable = CTB_CONSTCAST(dsttctable);
#else
	uintxx j;
	struct TTINFLTTables* tables;

	tables = PRVT->tables;

	PRVT->ltable = tables->symbols;
	PRVT->dtable = tables->symbols + ENOUGHL;

	/* literal-lengths */
	j = 0;
	for (; j < 144; j++)
		tables->lengths[j] = 8;
	for (; j < 256; j++)
		tables->lengths[j] = 9;
	for (; j < 280; j++)
		tables->lengths[j] = 7;
	for (; j < 288; j++)
		tables->lengths[j] = 8;

	j = buildtable(tables->lengths, 288, PRVT->ltable, LTABLEMODE);
	CTB_ASSERT(j == 0);

	/* distances */
	j = 0;
	for (; j < 32; j++)
		tables->lengths[j] = 5;
	j = buildtable(tables->lengths,  32, PRVT->dtable, DTABLEMODE);
	CTB_ASSERT(j == 0);
#endif
}


static uint32 decodeblock(struct TINFLTPrvt*);

static uint32 decodestrd(struct TINFLTPrvt*);
static uint32 decodednmc(struct TINFLTPrvt*);

CTB_INLINE bool
validate(struct TINFLTPrvt* state)
{
	if (PBLC->source == NULL) {
		SETERROR(INFLT_EINCORRECTUSE);
		return 0;
	}
	if (PBLC->target == NULL) {
		if (PRVT->towindow == 0) {
			SETERROR(INFLT_EINCORRECTUSE);
			return 0;
		}
	}

	switch (PBLC->status) {
		case INFLT_SRCEXHSTD:
			if (PBLC->source == PBLC->send) {
				if (PBLC->finalinput == 0) {
					SETERROR(INFLT_EINCORRECTUSE);
					return 0;
				}
			}
			break;
		case INFLT_TGTEXHSTD:
			if (PRVT->towindow == 0) {
				if (PBLC->target == PBLC->tend) {
					SETERROR(INFLT_EINCORRECTUSE);
					return 0;
				}
			}
			break;
	}
	return 1;
}

eINFLTResult
inflator_inflate(TInflator* state, uintxx final)
{
	uint32 r;

	if (CTB_EXPECT1(PBLC->state ^ 0xDEADBEEF)) {
		if (CTB_EXPECT0(PBLC->finalinput == 0 && final)) {
			PBLC->finalinput = 1;
		}

		if (validate(PRVT) == 0) {
			SETSTATE(0xDEADBEEF);
			return INFLT_ERROR;
		}
	}
	else {
		return INFLT_ERROR;
	}

	PRVT->used = 1;
	if (CTB_EXPECT0(PRVT->towindow)) {
		uintxx left;

		left = WNDWSIZE - PRVT->wndwend;
		if (PRVT->wndwend == WNDWSIZE) {
			left = WNDWSIZE;
			PBLC->target = PRVT->wndwbuffer;
		}
		else {
			PBLC->target = PRVT->wndwbuffer + PRVT->wndwend;
		}

		PBLC->tbgn = PBLC->target;
		PBLC->tend = PBLC->target + left;
	}

L_DECODE:
	if (CTB_EXPECT1(PBLC->state == 5)) {
		r = decodeblock(PRVT);
		if (CTB_EXPECT1(r)) {
			switch (r) {
				case INFLT_SRCEXHSTD: {
					if (PBLC->finalinput) {
						SETERROR(INFLT_EINPUTEND);
						goto L_ERROR;
					}
				}

				/* fallthrough */
				case INFLT_TGTEXHSTD: {
					updatewindow(PRVT);
					break;
				}
				
				case INFLT_ERROR: {
					goto L_ERROR;
				}
			}
			return (PBLC->status = r);
		}
		SETSTATE(0);
	}

	for (;;) {
		switch (PBLC->state) {
			case 0: {
				if (CTB_EXPECT0(PRVT->final)) {
					SETSTATE(0xDEADBEEF);
					return INFLT_OK;
				}

				if (tryreadbits(PRVT, 3)) {
					PRVT->final = (uint32) readbits(PRVT, 1);
					dropbits(PRVT, 1);
					PBLC->state = (uint32) readbits(PRVT, 2);
					dropbits(PRVT, 2);

					SETSTATE(PBLC->state + 1);
					continue;
				}

				if (PBLC->finalinput) {
					SETERROR(INFLT_EINPUTEND);
					goto L_ERROR;
				}
				updatewindow(PRVT);
				return (PBLC->status = INFLT_SRCEXHSTD);
			}

			/* stored */
			case 1: {
				if (CTB_EXPECT1((r = decodestrd(PRVT)) != 0)) {
					if (PBLC->finalinput && r == INFLT_SRCEXHSTD) {
						SETERROR(INFLT_EINPUTEND);
						goto L_ERROR;
					}
					return (PBLC->status = r);
				}
				SETSTATE(0);
				continue;
			}

			/* static */
			case 2: {
				setstatictables(PRVT);

				SETSTATE(5);
				goto L_DECODE;
			}

			/* dynamic */
			case 3: {
				if (CTB_EXPECT1((r = decodednmc(PRVT)) != 0)) {
					if (PBLC->finalinput && r == INFLT_SRCEXHSTD) {
						SETERROR(INFLT_EINPUTEND);
						goto L_ERROR;
					}
					return (PBLC->status = r);
				}

				SETSTATE(5);
				goto L_DECODE;
			}

			case 4:
				SETERROR(INFLT_EBADBLOCK);

			/* fallthrough */
			default:
				goto L_ERROR;
		}
	}

L_ERROR:
	if (PBLC->error == 0) {
		SETERROR(INFLT_EBADSTATE);
	}
	SETSTATE(0xDEADBEEF);
	return INFLT_ERROR;
}

void
inflator_setdctnr(TInflator* state, const uint8* dict, uintxx size)
{
	CTB_ASSERT(state && dict && size);

	if (PRVT->used) {
		SETERROR(INFLT_EINCORRECTUSE);
		SETSTATE(0xDEADBEEF);
		return;
	}

	if (size > DEFLT_WINDOWSIZE) {
		dict = (dict + size) - DEFLT_WINDOWSIZE;
		size = DEFLT_WINDOWSIZE;
	}

	ctb_memcpy(PRVT->wndwbuffer, dict, size);
	PRVT->wndwend = size;
	PRVT->wndwcnt = size;
	PRVT->used = 1;
}


#define slength PRVT->aux0

static uint32
decodestrd(struct TINFLTPrvt* state)
{
	uintxx maxrun;
	uintxx sourceleft;
	uintxx targetleft;

	switch (PRVT->substate) {
		case 0:
			break;
		case 1: goto L_STATE1;
		case 2: goto L_STATE2;
		case 3: goto L_STATE3;
	}

	if (tryreadbits(PRVT, 8)) {
		dropbits(PRVT, PRVT->bcount & 7);
	}
	else {
		updatewindow(PRVT);
		return INFLT_SRCEXHSTD;
	}

	PRVT->substate++;

L_STATE1:
	if (tryreadbits(PRVT, 16)) {
		uint32 a;
		uint32 b;

		a = (uint32) readbits(PRVT, 8); dropbits(PRVT, 8);
		b = (uint32) readbits(PRVT, 8); dropbits(PRVT, 8);
		slength = a | (b << 8);
	}
	else {
		updatewindow(PRVT);
		return INFLT_SRCEXHSTD;
	}
	PRVT->substate++;

L_STATE2:
	if (tryreadbits(PRVT, 16)) {
		uintxx nlength;
		uint32 a;
		uint32 b;

		a = (uint32) readbits(PRVT, 8); dropbits(PRVT, 8);
		b = (uint32) readbits(PRVT, 8); dropbits(PRVT, 8);
		nlength = a | (b << 8);

		if ((uint16) ~slength != nlength) {
			SETERROR(INFLT_EBADBLOCK);
			return INFLT_ERROR;
		}
	}
	else {
		updatewindow(PRVT);
		return INFLT_SRCEXHSTD;
	}
	PRVT->substate++;

L_STATE3:
	sourceleft = (uintxx) (PBLC->send - PBLC->source);
	targetleft = (uintxx) (PBLC->tend - PBLC->target);
	maxrun = slength;

	if (targetleft < maxrun)
		maxrun = targetleft;
	if (sourceleft < maxrun)
		maxrun = sourceleft;

	ctb_memcpy(PBLC->target, PBLC->source, maxrun);
	PBLC->target += maxrun;
	PBLC->source += maxrun;

	slength -= maxrun;
	if (slength) {
		if (PRVT->final == 0) {
			updatewindow(PRVT);
		}
		if ((targetleft - maxrun) == 0) {
			return INFLT_TGTEXHSTD;
		}

		return INFLT_SRCEXHSTD;
	}

	PRVT->substate = 0;
	return INFLT_OK;
}

#undef slength


#define slcount PRVT->aux0
#define sdcount PRVT->aux1
#define sccount PRVT->aux2
#define scindex PRVT->aux3

CTB_INLINE uint32
readlengths(struct TINFLTPrvt* state, uintxx n, uint16* lengths)
{
	uint32 e;
	uintxx length;
	uintxx replen;

	const uint8 sinfo[][2] = {
		{0x02, 0x03},
		{0x03, 0x03},
		{0x07, 0x0b}
	};

	while (scindex < n) {
		uint32 sl;
		uint32 ln;

		for (;;) {
			e = PRVT->ltable[readbits(state, CROOTBITS)];
			if ((uint8) e <= PRVT->bcount) {
				break;
			}

			if (fetchbyte(PRVT) == 0) {
				updatewindow(PRVT);
				return INFLT_SRCEXHSTD;
			}
		}

		sl = e >> 0x10;
		if (sl < 16) {
			lengths[scindex++] = (uint16) sl;
			dropbits(PRVT, (uint8) e);
			continue;
		}

		replen = sinfo[sl - 16][1];
		length = sinfo[sl - 16][0];

		ln = (uint8) e;
		if (tryreadbits(PRVT, ln + length)) {
			dropbits(PRVT, ln);

			replen += readbits(PRVT, length);
			dropbits(PRVT, length);
		}
		else {
			updatewindow(PRVT);
			return INFLT_SRCEXHSTD;
		}

		if (length == 2) {
			if (scindex == 0) {
				SETERROR(INFLT_EBADTREE);
				return INFLT_ERROR;
			}
			length = lengths[scindex - 1];
		}
		else {
			length = 0;
		}

		if (scindex + replen > DEFLT_DMAXSYMBOL + DEFLT_LMAXSYMBOL) {
			SETERROR(INFLT_EBADTREE);
			return INFLT_ERROR;
		}

		while (replen--)
			lengths[scindex++] = (uint16) length;
	}

	return 0;
}

static uint32
decodednmc(struct TINFLTPrvt* state)
{
	static const uint8 lcorder[] = {
		16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
	};
	uint32 r;
	uint16* lengths;

	switch (PRVT->substate) {
		case 0:
			break;
		case 1: goto L_STATE1;
		case 2: goto L_STATE2;
	}

	PRVT->ltable = PRVT->tables->symbols;
	PRVT->dtable = PRVT->tables->symbols + ENOUGHL;

	if (tryreadbits(PRVT, 14)) {
		slcount = readbits(PRVT, 5) + 257; dropbits(PRVT, 5);
		sdcount = readbits(PRVT, 5) +   1; dropbits(PRVT, 5);
		sccount = readbits(PRVT, 4) +   4; dropbits(PRVT, 4);

		if (slcount > 286 || sdcount > 30) {
			SETERROR(INFLT_EBADTREE);
			return INFLT_ERROR;
		}
	}
	else {
		updatewindow(PRVT);
		return INFLT_SRCEXHSTD;
	}
	PRVT->substate++;
	scindex = 0;

L_STATE1:
	lengths = PRVT->tables->lengths;
	for (; sccount > scindex; scindex++) {
		if (tryreadbits(PRVT, 3)) {
			lengths[lcorder[scindex]] = (uint16) readbits(PRVT, 3);
			dropbits(PRVT, 3);
		}
		else {
			updatewindow(PRVT);
			return INFLT_SRCEXHSTD;
		}
	}
	sccount = DEFLT_CMAXSYMBOL;
	for (; sccount > scindex; scindex++)
		lengths[lcorder[scindex]] = 0;

	r = buildtable(lengths, DEFLT_CMAXSYMBOL, PRVT->ltable, CTABLEMODE);
	if (r) {
		SETERROR(INFLT_EBADTREE);
		return r;
	}

	PRVT->substate++;
	scindex = 0;

L_STATE2:
	lengths = PRVT->tables->lengths;
	r = readlengths(PRVT, slcount + sdcount, lengths);
	if (r) {
		return r;
	}

	if (lengths[256] == 0) {
		SETERROR(INFLT_EBADTREE);
		return INFLT_ERROR;
	}

	r = buildtable(lengths, slcount, PRVT->ltable, LTABLEMODE);
	if (r) {
		SETERROR(INFLT_EBADTREE);
		return INFLT_ERROR;
	}
	lengths = lengths + slcount;
	r = buildtable(lengths, sdcount, PRVT->dtable, DTABLEMODE);
	if (r) {
		SETERROR(INFLT_EBADTREE);
		return INFLT_ERROR;
	}

	PRVT->substate = 0;
	return 0;
}

#undef slcount
#undef sdcount
#undef sccount
#undef scindex


#if defined(__clang__) && defined(CTB_FASTUNALIGNED)
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wcast-align"
#endif

#define slength PRVT->aux0
#define soffset PRVT->aux1
#define sextra  PRVT->aux2

#if defined(CTB_ENV64)
	#define PLATFORMWORDSIZE 8
#else
	#define PLATFORMWORDSIZE 4
#endif

static uint32
copybytes(struct TINFLTPrvt* state)
{
	uint8* buffer;
	uint8* target;
	uintxx maxrun;
	uintxx total;
	uintxx rmnng;

	target = PBLC->target;

	rmnng = (uintxx) (PBLC->tend -      target);
	total = (uintxx) (target     - PBLC->tbgn);
	do {
		if (CTB_EXPECT0(rmnng == 0)) {
			PBLC->target = target;
			return INFLT_TGTEXHSTD;
		}

		if (soffset > total) {
			maxrun = soffset - total;
			if (CTB_EXPECT0(maxrun > PRVT->wndwcnt)) {
				SETERROR(INFLT_EFAROFFSET);
				return INFLT_ERROR;
			}

			buffer = PRVT->wndwbuffer;
			if (maxrun > PRVT->wndwend) {
				maxrun -= PRVT->wndwend;
				buffer += WNDWSIZE - maxrun;
			}
			else {
				buffer += PRVT->wndwend - maxrun;
			}

			if (maxrun > slength)
				maxrun = slength;
		}
		else {
			buffer = target - soffset;
			maxrun = slength;
		}

		if (maxrun > rmnng)
			maxrun = rmnng;
		slength -= maxrun;

		total += maxrun;
		rmnng -= maxrun;

		if (soffset >= PLATFORMWORDSIZE) {
			for (;maxrun > 8; maxrun -= 8) {
#if !defined(CTB_STRICTALIGNMENT) && defined(CTB_FASTUNALIGNED)
				((uint32*) target)[0] = ((uint32*) buffer)[0];
				((uint32*) target)[1] = ((uint32*) buffer)[1];
#else
				target[0] = buffer[0];
				target[1] = buffer[1];
				target[2] = buffer[2];
				target[3] = buffer[3];

				target[4] = buffer[4];
				target[5] = buffer[5];
				target[6] = buffer[6];
				target[7] = buffer[7];
#endif
				target += 8;
				buffer += 8;
			}
		}

		do {
			*target++ = *buffer++;
		} while (--maxrun);
	} while (slength);

	PBLC->target = target;
	return 0;
}


#if defined(__MSVC__)
	#pragma warning(push)
	#pragma warning(disable: 4702)
#endif


#define MASKBITS(BB, N) ((BB) & ((1ul << (N)) - 1))

#if !defined(CTB_STRICTALIGNMENT) && defined(CTB_FASTUNALIGNED)
	#define LOAD64(S) \
		((CTB_SWAP64ONBE(((const uint64*) (S))[0]) & 0x00ffffffffffffffull))

	#define LOAD32(S) \
		((CTB_SWAP32ONBE(((const uint32*) (S))[0]) & 0x00fffffful))
#else
	#define LOAD64(S) \
		((((uint64) (S)[0]) << 0x00) | \
		 (((uint64) (S)[1]) << 0x08) | \
		 (((uint64) (S)[2]) << 0x10) | \
		 (((uint64) (S)[3]) << 0x18) | \
		 (((uint64) (S)[4]) << 0x20) | \
		 (((uint64) (S)[5]) << 0x28) | (((uint64) (S)[6]) << 0x30))

	#define LOAD32(S) \
		((((uint32) (S)[0]) << 0x00) | \
		 (((uint32) (S)[1]) << 0x08) | (((uint32) (S)[2]) << 0x10))
#endif

#define DROPBITS(BB, BC, N) (((BB) = (BB) >> (N)), ((BC) -= (N)))


#define FASTSRCLEFT  15
#define FASTTGTLEFT 266

static uint32 decodefast(struct TINFLTPrvt* state);

static uint32
decodeblock(struct TINFLTPrvt* state)
{
	bitbuffer bb;
	uintxx bc;  /* bit count */
    uintxx or;  /* bit overread count */
	uint32 r;
	uint32 fastcheck;
	uint32 e;

	bb = PRVT->bbuffer;
	bc = PRVT->bcount;
    or = 0;

	fastcheck = 1;
	switch (PRVT->substate) {
		case 0:
			break;
		case 1: goto L_STATE1;
		case 2: goto L_STATE2;
		case 3: goto L_STATE3;
		case 4: goto L_STATE4;
	}

L_LOOP:
	if (CTB_EXPECT0(fastcheck)) {
		uintxx targetleft;
		uintxx sourceleft;

		targetleft = (uintxx) (PBLC->tend - PBLC->target);
		sourceleft = (uintxx) (PBLC->send - PBLC->source);
		if (targetleft >= FASTTGTLEFT && sourceleft >= FASTSRCLEFT) {
			PRVT->bbuffer = bb;
			PRVT->bcount  = bc;
			
			r = decodefast(PRVT);
			if (r == 256) {
				/* end of block */
				PRVT->substate = 0;
				return 0;
			}
			else {
				if (r) {
					return INFLT_ERROR;
				}
			}
			bb = PRVT->bbuffer;
			bc = PRVT->bcount;
		}
		fastcheck = 0;
	}

	for (; 15 > bc; bc += 8) {
		if (PBLC->source >= PBLC->send) {
            or += 8;
			continue;
		}
		bb |= ((bitbuffer) *PBLC->source++) << bc;
	}

	e = PRVT->ltable[MASKBITS(bb, LROOTBITS)];
	if (e & TAG_SUB) {
		uintxx base;

		base = e >> 0x10;
		e = PRVT->ltable[base + (MASKBITS(bb, (uint8) e) >> LROOTBITS)];
	}

	if (or) {
		bc -= or;
		if (bc < (uint8) e) {
			PRVT->substate = 0;
			goto L_SRCEXHSTD;
		}
		or = 0;
	}

	if (CTB_EXPECT1(e & TAG_LIT)) {
		if (PBLC->tend > PBLC->target) {
			*PBLC->target++ = (uint8) (e >> 0x10);
		}
		else {
			PRVT->bbuffer = bb;
			PRVT->bcount  = bc;

            PRVT->substate = 0;
			return INFLT_TGTEXHSTD;
		}
		DROPBITS(bb, bc, (uint8) e);
		goto L_LOOP;
	}

	if (CTB_EXPECT0(e & TAG_END)) {
		DROPBITS(bb, bc, (uint8) e);
		PRVT->bbuffer = bb;
		PRVT->bcount  = bc;

        PRVT->substate = 0;
		return 0;
	}

	if (CTB_EXPECT0((uint8) e) == 0) {
		SETERROR(INFLT_EBADCODE);
		PRVT->bbuffer = bb;
		PRVT->bcount  = bc;
		return INFLT_ERROR;
	}
	DROPBITS(bb, bc, (uint8) e);

	slength = (e >> 0x10);
	sextra  = (e >> 0x08) & 0x0f;

L_STATE1:
	for (; sextra > bc; bc += 8) {
		if (PBLC->source >= PBLC->send) {
            PRVT->substate = 1;
			goto L_SRCEXHSTD;
		}
		bb |= ((bitbuffer) *PBLC->source++) << bc;
	}

	slength += MASKBITS(bb, sextra);
	DROPBITS(bb, bc, sextra);

L_STATE2:
	/* decode distance */
	for (; 15 > bc; bc += 8) {
		if (PBLC->source >= PBLC->send) {
            or += 8;
			continue;
		}
		bb |= ((bitbuffer) *PBLC->source++) << bc;
	}

	e = PRVT->dtable[MASKBITS(bb, DROOTBITS)];
	if (CTB_EXPECT1(e & TAG_SUB)) {
		uintxx base;

		base = e >> 0x10;
		e = PRVT->dtable[base + (MASKBITS(bb, (uint8) e) >> DROOTBITS)];
	}

	if (or) {
		bc -= or;
		if (bc < (uint8) e) {
			PRVT->substate = 2;
			goto L_SRCEXHSTD;
		}
		or = 0;
	}

	if (CTB_EXPECT0((uint8) e) == 0) {
		SETERROR(INFLT_EBADCODE);
		PRVT->bbuffer = bb;
		PRVT->bcount  = bc;
		return INFLT_ERROR;
	}
	DROPBITS(bb, bc, (uint8) e);

	soffset = (e >> 0x10);
	sextra  = (e >> 0x08) & 0x0f;

L_STATE3:
	for (; sextra > bc; bc += 8) {
		if (PBLC->source >= PBLC->send) {
			PRVT->substate = 3;
			goto L_SRCEXHSTD;
		}
		bb |= ((bitbuffer) *PBLC->source++) << bc;
	}

	soffset += MASKBITS(bb, sextra);
	DROPBITS(bb, bc, sextra);

L_STATE4:
	r = copybytes(PRVT);
	if (r) {
		PRVT->bbuffer = bb;
		PRVT->bcount  = bc;

		PRVT->substate = 4;
		return r;
	}
	goto L_LOOP;

L_SRCEXHSTD:
	PRVT->bbuffer = bb;
	PRVT->bcount  = bc;
	return INFLT_SRCEXHSTD;
}

#undef slength
#undef soffset
#undef sextra

#if defined(__MSVC__)
	#pragma warning(pop)
#endif


static uint32
decodefast(struct TINFLTPrvt* state)
{
	uint32 r;
	const uint8* source;
	const uint8* send;
	uint8* target;
	uint8* tend;
#if !defined(CTB_ENV64)
	uint32 bb;
#else
	uint64 bb;
#endif
	uintxx bc;
	uintxx n;
	const uint32* ltable;
	const uint32* dtable;
	uint32 e;

	source = PBLC->source;
	target = PBLC->target;
	tend = PBLC->tend - FASTTGTLEFT;
	send = PBLC->send - FASTSRCLEFT;

	ltable = PRVT->ltable;
	dtable = PRVT->dtable;
	bb = PRVT->bbuffer;
	bc = PRVT->bcount;

	r = 0;
	do {
		uintxx length;
		uintxx offset;
		uintxx extra;
		uintxx targetbytes;
		uintxx maxrun;
		uint8* buffer;

#if !defined(CTB_ENV64)
		if (bc < 15) {
			bb |= LOAD32(source) << bc;
			source = (source + 3) - ((bc >> 3) & 0x07);
			bc |= 24;
		}
#else
		bb |= LOAD64(source) << bc;
		source = (source + 7) - ((bc >> 3) & 0x07);
		bc |= 56;
#endif

		/* decode literal or length */
		e = ltable[MASKBITS(bb, LROOTBITS)];
		if (CTB_EXPECT1(e & TAG_LIT)) {
			*target++ = (uint8) (e >> 0x10);
			DROPBITS(bb, bc, (uint8) e);

#if !defined(CTB_ENV64)
			continue;
#else
			e = ltable[MASKBITS(bb, LROOTBITS)];
			if (CTB_EXPECT1(e & TAG_LIT)) {
				*target++ = (uint8) (e >> 0x10);
				DROPBITS(bb, bc, (uint8) e);

				e = ltable[MASKBITS(bb, LROOTBITS)];
			}
#endif
		}

		if (CTB_EXPECT0(e & TAG_SUB)) {
			uintxx base;

			base = e >> 0x10;
			e = ltable[base + (MASKBITS(bb, (uint8) e) >> LROOTBITS)];
		}

		if (CTB_EXPECT1(e & TAG_LIT)) {
			*target++ = (uint8) (e >> 0x10);

			DROPBITS(bb, bc, (uint8) e);
			continue;
		}
		else {
			if (CTB_EXPECT0(e & TAG_END)) {
				r = 256;
				DROPBITS(bb, bc, (uint8) e);
				break;
			}
		}

		/* length */
		if (CTB_EXPECT0((uint8) e) == 0) {
			SETERROR(INFLT_EBADCODE);
			r = INFLT_ERROR;
			break;
		}
		DROPBITS(bb, bc, (uint8) e);

		extra = (e >> 0x08) & 0x0f;
#if !defined(CTB_ENV64)
		if (bc < extra) {
			bb |= LOAD32(source) << bc;
			source = (source + 3) - ((bc >> 3) & 0x07);
			bc |= 24;
		}
#endif
		length = (e >> 0x10) + (uintxx) MASKBITS(bb, extra);
		DROPBITS(bb, bc, extra);

		/* distance */
#if !defined(CTB_ENV64)
		if (bc < 15) {
			bb |= LOAD32(source) << bc;
			source = (source + 3) - ((bc >> 3) & 0x07);
			bc |= 24;
		}
#endif
		e = dtable[MASKBITS(bb, DROOTBITS)];
		if (CTB_EXPECT0(e & TAG_SUB)) {
			uintxx base;

			base = e >> 0x10;
			e = dtable[base + (MASKBITS(bb, (uint8) e) >> DROOTBITS)];
		}

		if (CTB_EXPECT0((uint8) e) == 0) {
			SETERROR(INFLT_EBADCODE);
			r = INFLT_ERROR;
			break;
		}
		DROPBITS(bb, bc, (uint8) e);

		extra = (e >> 0x08) & 0x0f;
#if !defined(CTB_ENV64)
		if (bc < extra) {
			bb |= LOAD32(source) << bc;
			source = (source + 3) - ((bc >> 3) & 0x07);
			bc |= 24;
		}
#else
		if (bc < 13) {
			bb |= LOAD64(source) << bc;
			source = (source + 7) - ((bc >> 3) & 0x07);
			bc |= 56;
		}
#endif
		offset = (e >> 0x10) + (uintxx) MASKBITS(bb, extra);
		DROPBITS(bb, bc, extra);

		targetbytes = (uintxx) (target - PBLC->tbgn);
		if (CTB_EXPECT0(offset < targetbytes)) {
			uint8* end;

			buffer = target - offset;
			maxrun = length;
			end = target + maxrun;

			if (CTB_EXPECT1(offset >= PLATFORMWORDSIZE)) {
#if !defined(CTB_STRICTALIGNMENT) && defined(CTB_FASTUNALIGNED)
#if defined(CTB_ENV64)
				((uint64*) target)[0] = ((uint64*) buffer)[0];
				((uint64*) target)[1] = ((uint64*) buffer)[1];
				target += 16;
				buffer += 16;
#else
				((uint32*) target)[0] = ((uint32*) buffer)[0];
				((uint32*) target)[1] = ((uint32*) buffer)[1];
				target += 8;
				buffer += 8;
#endif
#else
				target[0] = buffer[0];
				target[1] = buffer[1];
				target[2] = buffer[2];
				target[3] = buffer[3];
				target += 4;
				buffer += 4;
#endif
				do {
#if !defined(CTB_STRICTALIGNMENT) && defined(CTB_FASTUNALIGNED)
#if defined(CTB_ENV64)
					((uint64*) target)[0] = ((uint64*) buffer)[0];
					((uint64*) target)[1] = ((uint64*) buffer)[1];
					target += 16;
					buffer += 16;
#else
					((uint32*) target)[0] = ((uint32*) buffer)[0];
					((uint32*) target)[1] = ((uint32*) buffer)[1];
					target += 8;
					buffer += 8;
#endif
#else
					target[0] = buffer[0];
					target[1] = buffer[1];
					target[2] = buffer[2];
					target[3] = buffer[3];
					target += 4;
					buffer += 4;
#endif
				} while (target < end);
				target = end;
			}
			else {
				*target++ = *buffer++;
				*target++ = *buffer++;
				do {
					*target++ = *buffer++;
				} while (target < end);
			}
		}
		else {
			maxrun = targetbytes;
			do {
				if (CTB_EXPECT0(offset > maxrun)) {
					maxrun = offset - maxrun;
					if (CTB_EXPECT0(maxrun > PRVT->wndwcnt)) {
						SETERROR(INFLT_EFAROFFSET);
						return INFLT_ERROR;
					}

					buffer = PRVT->wndwbuffer;
					if (maxrun > PRVT->wndwend) {
						maxrun -= PRVT->wndwend;
						buffer += WNDWSIZE - maxrun;
					}
					else {
						buffer += PRVT->wndwend - maxrun;
					}

					if (maxrun > length)
						maxrun = length;
				}
				else {
					buffer = target - offset;
					maxrun = length;
				}

				length -= maxrun;
				if (CTB_EXPECT1(offset >= PLATFORMWORDSIZE)) {
					for (;maxrun > 8; maxrun -= 8) {
#if !defined(CTB_STRICTALIGNMENT) && defined(CTB_FASTUNALIGNED)
#if defined(CTB_ENV64)
						((uint64*) target)[0] = ((uint64*) buffer)[0];
#else
						((uint32*) target)[0] = ((uint32*) buffer)[0];
						((uint32*) target)[1] = ((uint32*) buffer)[1];
#endif
#else
						target[0] = buffer[0];
						target[1] = buffer[1];
						target[2] = buffer[2];
						target[3] = buffer[3];
						target[4] = buffer[4];
						target[5] = buffer[5];
						target[6] = buffer[6];
						target[7] = buffer[7];
#endif
						target += 8;
						buffer += 8;
					}
				}

				do {
					*target++ = *buffer++;
				} while (--maxrun);
				maxrun = (uintxx) (target - PBLC->tbgn);
			} while (length);
		}
	} while (tend > target && send > source);

	/* restore unused bytes */
	n = bc & 0x07;
	PRVT->bbuffer = (bitbuffer) (bb & ((((bitbuffer) 1ul) << n) - 1));
	PRVT->bcount  = n;

	PBLC->source = source - ((bc - n) >> 3);
	PBLC->target = target;
	return r;
}

#if defined(__clang__) && defined(CTB_FASTUNALIGNED)
	#pragma clang diagnostic pop
#endif


#undef PRVT
#undef WNDWSIZE


/* ****************************************************************************
 * Fixed code Tables
 *************************************************************************** */

#if LROOTBITS == 10 && DROOTBITS == 8

static const uint32 lsttctable_[] = {
	0x0100c007, 0x80500008, 0x80100008, 0x00730408,
	0x001f0207, 0x80700008, 0x80300008, 0x80c00009,
	0x000a0007, 0x80600008, 0x80200008, 0x80a00009,
	0x80000008, 0x80800008, 0x80400008, 0x80e00009,
	0x00060007, 0x80580008, 0x80180008, 0x80900009,
	0x003b0307, 0x80780008, 0x80380008, 0x80d00009,
	0x00110107, 0x80680008, 0x80280008, 0x80b00009,
	0x80080008, 0x80880008, 0x80480008, 0x80f00009,
	0x00040007, 0x80540008, 0x80140008, 0x00e30508,
	0x002b0307, 0x80740008, 0x80340008, 0x80c80009,
	0x000d0107, 0x80640008, 0x80240008, 0x80a80009,
	0x80040008, 0x80840008, 0x80440008, 0x80e80009,
	0x00080007, 0x805c0008, 0x801c0008, 0x80980009,
	0x00530407, 0x807c0008, 0x803c0008, 0x80d80009,
	0x00170207, 0x806c0008, 0x802c0008, 0x80b80009,
	0x800c0008, 0x808c0008, 0x804c0008, 0x80f80009,
	0x00030007, 0x80520008, 0x80120008, 0x00a30508,
	0x00230307, 0x80720008, 0x80320008, 0x80c40009,
	0x000b0107, 0x80620008, 0x80220008, 0x80a40009,
	0x80020008, 0x80820008, 0x80420008, 0x80e40009,
	0x00070007, 0x805a0008, 0x801a0008, 0x80940009,
	0x00430407, 0x807a0008, 0x803a0008, 0x80d40009,
	0x00130207, 0x806a0008, 0x802a0008, 0x80b40009,
	0x800a0008, 0x808a0008, 0x804a0008, 0x80f40009,
	0x00050007, 0x80560008, 0x80160008, 0x00000008,
	0x00330307, 0x80760008, 0x80360008, 0x80cc0009,
	0x000f0107, 0x80660008, 0x80260008, 0x80ac0009,
	0x80060008, 0x80860008, 0x80460008, 0x80ec0009,
	0x00090007, 0x805e0008, 0x801e0008, 0x809c0009,
	0x00630407, 0x807e0008, 0x803e0008, 0x80dc0009,
	0x001b0207, 0x806e0008, 0x802e0008, 0x80bc0009,
	0x800e0008, 0x808e0008, 0x804e0008, 0x80fc0009,
	0x0100c007, 0x80510008, 0x80110008, 0x00830508,
	0x001f0207, 0x80710008, 0x80310008, 0x80c20009,
	0x000a0007, 0x80610008, 0x80210008, 0x80a20009,
	0x80010008, 0x80810008, 0x80410008, 0x80e20009,
	0x00060007, 0x80590008, 0x80190008, 0x80920009,
	0x003b0307, 0x80790008, 0x80390008, 0x80d20009,
	0x00110107, 0x80690008, 0x80290008, 0x80b20009,
	0x80090008, 0x80890008, 0x80490008, 0x80f20009,
	0x00040007, 0x80550008, 0x80150008, 0x01020008,
	0x002b0307, 0x80750008, 0x80350008, 0x80ca0009,
	0x000d0107, 0x80650008, 0x80250008, 0x80aa0009,
	0x80050008, 0x80850008, 0x80450008, 0x80ea0009,
	0x00080007, 0x805d0008, 0x801d0008, 0x809a0009,
	0x00530407, 0x807d0008, 0x803d0008, 0x80da0009,
	0x00170207, 0x806d0008, 0x802d0008, 0x80ba0009,
	0x800d0008, 0x808d0008, 0x804d0008, 0x80fa0009,
	0x00030007, 0x80530008, 0x80130008, 0x00c30508,
	0x00230307, 0x80730008, 0x80330008, 0x80c60009,
	0x000b0107, 0x80630008, 0x80230008, 0x80a60009,
	0x80030008, 0x80830008, 0x80430008, 0x80e60009,
	0x00070007, 0x805b0008, 0x801b0008, 0x80960009,
	0x00430407, 0x807b0008, 0x803b0008, 0x80d60009,
	0x00130207, 0x806b0008, 0x802b0008, 0x80b60009,
	0x800b0008, 0x808b0008, 0x804b0008, 0x80f60009,
	0x00050007, 0x80570008, 0x80170008, 0x00000008,
	0x00330307, 0x80770008, 0x80370008, 0x80ce0009,
	0x000f0107, 0x80670008, 0x80270008, 0x80ae0009,
	0x80070008, 0x80870008, 0x80470008, 0x80ee0009,
	0x00090007, 0x805f0008, 0x801f0008, 0x809e0009,
	0x00630407, 0x807f0008, 0x803f0008, 0x80de0009,
	0x001b0207, 0x806f0008, 0x802f0008, 0x80be0009,
	0x800f0008, 0x808f0008, 0x804f0008, 0x80fe0009,
	0x0100c007, 0x80500008, 0x80100008, 0x00730408,
	0x001f0207, 0x80700008, 0x80300008, 0x80c10009,
	0x000a0007, 0x80600008, 0x80200008, 0x80a10009,
	0x80000008, 0x80800008, 0x80400008, 0x80e10009,
	0x00060007, 0x80580008, 0x80180008, 0x80910009,
	0x003b0307, 0x80780008, 0x80380008, 0x80d10009,
	0x00110107, 0x80680008, 0x80280008, 0x80b10009,
	0x80080008, 0x80880008, 0x80480008, 0x80f10009,
	0x00040007, 0x80540008, 0x80140008, 0x00e30508,
	0x002b0307, 0x80740008, 0x80340008, 0x80c90009,
	0x000d0107, 0x80640008, 0x80240008, 0x80a90009,
	0x80040008, 0x80840008, 0x80440008, 0x80e90009,
	0x00080007, 0x805c0008, 0x801c0008, 0x80990009,
	0x00530407, 0x807c0008, 0x803c0008, 0x80d90009,
	0x00170207, 0x806c0008, 0x802c0008, 0x80b90009,
	0x800c0008, 0x808c0008, 0x804c0008, 0x80f90009,
	0x00030007, 0x80520008, 0x80120008, 0x00a30508,
	0x00230307, 0x80720008, 0x80320008, 0x80c50009,
	0x000b0107, 0x80620008, 0x80220008, 0x80a50009,
	0x80020008, 0x80820008, 0x80420008, 0x80e50009,
	0x00070007, 0x805a0008, 0x801a0008, 0x80950009,
	0x00430407, 0x807a0008, 0x803a0008, 0x80d50009,
	0x00130207, 0x806a0008, 0x802a0008, 0x80b50009,
	0x800a0008, 0x808a0008, 0x804a0008, 0x80f50009,
	0x00050007, 0x80560008, 0x80160008, 0x00000008,
	0x00330307, 0x80760008, 0x80360008, 0x80cd0009,
	0x000f0107, 0x80660008, 0x80260008, 0x80ad0009,
	0x80060008, 0x80860008, 0x80460008, 0x80ed0009,
	0x00090007, 0x805e0008, 0x801e0008, 0x809d0009,
	0x00630407, 0x807e0008, 0x803e0008, 0x80dd0009,
	0x001b0207, 0x806e0008, 0x802e0008, 0x80bd0009,
	0x800e0008, 0x808e0008, 0x804e0008, 0x80fd0009,
	0x0100c007, 0x80510008, 0x80110008, 0x00830508,
	0x001f0207, 0x80710008, 0x80310008, 0x80c30009,
	0x000a0007, 0x80610008, 0x80210008, 0x80a30009,
	0x80010008, 0x80810008, 0x80410008, 0x80e30009,
	0x00060007, 0x80590008, 0x80190008, 0x80930009,
	0x003b0307, 0x80790008, 0x80390008, 0x80d30009,
	0x00110107, 0x80690008, 0x80290008, 0x80b30009,
	0x80090008, 0x80890008, 0x80490008, 0x80f30009,
	0x00040007, 0x80550008, 0x80150008, 0x01020008,
	0x002b0307, 0x80750008, 0x80350008, 0x80cb0009,
	0x000d0107, 0x80650008, 0x80250008, 0x80ab0009,
	0x80050008, 0x80850008, 0x80450008, 0x80eb0009,
	0x00080007, 0x805d0008, 0x801d0008, 0x809b0009,
	0x00530407, 0x807d0008, 0x803d0008, 0x80db0009,
	0x00170207, 0x806d0008, 0x802d0008, 0x80bb0009,
	0x800d0008, 0x808d0008, 0x804d0008, 0x80fb0009,
	0x00030007, 0x80530008, 0x80130008, 0x00c30508,
	0x00230307, 0x80730008, 0x80330008, 0x80c70009,
	0x000b0107, 0x80630008, 0x80230008, 0x80a70009,
	0x80030008, 0x80830008, 0x80430008, 0x80e70009,
	0x00070007, 0x805b0008, 0x801b0008, 0x80970009,
	0x00430407, 0x807b0008, 0x803b0008, 0x80d70009,
	0x00130207, 0x806b0008, 0x802b0008, 0x80b70009,
	0x800b0008, 0x808b0008, 0x804b0008, 0x80f70009,
	0x00050007, 0x80570008, 0x80170008, 0x00000008,
	0x00330307, 0x80770008, 0x80370008, 0x80cf0009,
	0x000f0107, 0x80670008, 0x80270008, 0x80af0009,
	0x80070008, 0x80870008, 0x80470008, 0x80ef0009,
	0x00090007, 0x805f0008, 0x801f0008, 0x809f0009,
	0x00630407, 0x807f0008, 0x803f0008, 0x80df0009,
	0x001b0207, 0x806f0008, 0x802f0008, 0x80bf0009,
	0x800f0008, 0x808f0008, 0x804f0008, 0x80ff0009,
	0x0100c007, 0x80500008, 0x80100008, 0x00730408,
	0x001f0207, 0x80700008, 0x80300008, 0x80c00009,
	0x000a0007, 0x80600008, 0x80200008, 0x80a00009,
	0x80000008, 0x80800008, 0x80400008, 0x80e00009,
	0x00060007, 0x80580008, 0x80180008, 0x80900009,
	0x003b0307, 0x80780008, 0x80380008, 0x80d00009,
	0x00110107, 0x80680008, 0x80280008, 0x80b00009,
	0x80080008, 0x80880008, 0x80480008, 0x80f00009,
	0x00040007, 0x80540008, 0x80140008, 0x00e30508,
	0x002b0307, 0x80740008, 0x80340008, 0x80c80009,
	0x000d0107, 0x80640008, 0x80240008, 0x80a80009,
	0x80040008, 0x80840008, 0x80440008, 0x80e80009,
	0x00080007, 0x805c0008, 0x801c0008, 0x80980009,
	0x00530407, 0x807c0008, 0x803c0008, 0x80d80009,
	0x00170207, 0x806c0008, 0x802c0008, 0x80b80009,
	0x800c0008, 0x808c0008, 0x804c0008, 0x80f80009,
	0x00030007, 0x80520008, 0x80120008, 0x00a30508,
	0x00230307, 0x80720008, 0x80320008, 0x80c40009,
	0x000b0107, 0x80620008, 0x80220008, 0x80a40009,
	0x80020008, 0x80820008, 0x80420008, 0x80e40009,
	0x00070007, 0x805a0008, 0x801a0008, 0x80940009,
	0x00430407, 0x807a0008, 0x803a0008, 0x80d40009,
	0x00130207, 0x806a0008, 0x802a0008, 0x80b40009,
	0x800a0008, 0x808a0008, 0x804a0008, 0x80f40009,
	0x00050007, 0x80560008, 0x80160008, 0x00000008,
	0x00330307, 0x80760008, 0x80360008, 0x80cc0009,
	0x000f0107, 0x80660008, 0x80260008, 0x80ac0009,
	0x80060008, 0x80860008, 0x80460008, 0x80ec0009,
	0x00090007, 0x805e0008, 0x801e0008, 0x809c0009,
	0x00630407, 0x807e0008, 0x803e0008, 0x80dc0009,
	0x001b0207, 0x806e0008, 0x802e0008, 0x80bc0009,
	0x800e0008, 0x808e0008, 0x804e0008, 0x80fc0009,
	0x0100c007, 0x80510008, 0x80110008, 0x00830508,
	0x001f0207, 0x80710008, 0x80310008, 0x80c20009,
	0x000a0007, 0x80610008, 0x80210008, 0x80a20009,
	0x80010008, 0x80810008, 0x80410008, 0x80e20009,
	0x00060007, 0x80590008, 0x80190008, 0x80920009,
	0x003b0307, 0x80790008, 0x80390008, 0x80d20009,
	0x00110107, 0x80690008, 0x80290008, 0x80b20009,
	0x80090008, 0x80890008, 0x80490008, 0x80f20009,
	0x00040007, 0x80550008, 0x80150008, 0x01020008,
	0x002b0307, 0x80750008, 0x80350008, 0x80ca0009,
	0x000d0107, 0x80650008, 0x80250008, 0x80aa0009,
	0x80050008, 0x80850008, 0x80450008, 0x80ea0009,
	0x00080007, 0x805d0008, 0x801d0008, 0x809a0009,
	0x00530407, 0x807d0008, 0x803d0008, 0x80da0009,
	0x00170207, 0x806d0008, 0x802d0008, 0x80ba0009,
	0x800d0008, 0x808d0008, 0x804d0008, 0x80fa0009,
	0x00030007, 0x80530008, 0x80130008, 0x00c30508,
	0x00230307, 0x80730008, 0x80330008, 0x80c60009,
	0x000b0107, 0x80630008, 0x80230008, 0x80a60009,
	0x80030008, 0x80830008, 0x80430008, 0x80e60009,
	0x00070007, 0x805b0008, 0x801b0008, 0x80960009,
	0x00430407, 0x807b0008, 0x803b0008, 0x80d60009,
	0x00130207, 0x806b0008, 0x802b0008, 0x80b60009,
	0x800b0008, 0x808b0008, 0x804b0008, 0x80f60009,
	0x00050007, 0x80570008, 0x80170008, 0x00000008,
	0x00330307, 0x80770008, 0x80370008, 0x80ce0009,
	0x000f0107, 0x80670008, 0x80270008, 0x80ae0009,
	0x80070008, 0x80870008, 0x80470008, 0x80ee0009,
	0x00090007, 0x805f0008, 0x801f0008, 0x809e0009,
	0x00630407, 0x807f0008, 0x803f0008, 0x80de0009,
	0x001b0207, 0x806f0008, 0x802f0008, 0x80be0009,
	0x800f0008, 0x808f0008, 0x804f0008, 0x80fe0009,
	0x0100c007, 0x80500008, 0x80100008, 0x00730408,
	0x001f0207, 0x80700008, 0x80300008, 0x80c10009,
	0x000a0007, 0x80600008, 0x80200008, 0x80a10009,
	0x80000008, 0x80800008, 0x80400008, 0x80e10009,
	0x00060007, 0x80580008, 0x80180008, 0x80910009,
	0x003b0307, 0x80780008, 0x80380008, 0x80d10009,
	0x00110107, 0x80680008, 0x80280008, 0x80b10009,
	0x80080008, 0x80880008, 0x80480008, 0x80f10009,
	0x00040007, 0x80540008, 0x80140008, 0x00e30508,
	0x002b0307, 0x80740008, 0x80340008, 0x80c90009,
	0x000d0107, 0x80640008, 0x80240008, 0x80a90009,
	0x80040008, 0x80840008, 0x80440008, 0x80e90009,
	0x00080007, 0x805c0008, 0x801c0008, 0x80990009,
	0x00530407, 0x807c0008, 0x803c0008, 0x80d90009,
	0x00170207, 0x806c0008, 0x802c0008, 0x80b90009,
	0x800c0008, 0x808c0008, 0x804c0008, 0x80f90009,
	0x00030007, 0x80520008, 0x80120008, 0x00a30508,
	0x00230307, 0x80720008, 0x80320008, 0x80c50009,
	0x000b0107, 0x80620008, 0x80220008, 0x80a50009,
	0x80020008, 0x80820008, 0x80420008, 0x80e50009,
	0x00070007, 0x805a0008, 0x801a0008, 0x80950009,
	0x00430407, 0x807a0008, 0x803a0008, 0x80d50009,
	0x00130207, 0x806a0008, 0x802a0008, 0x80b50009,
	0x800a0008, 0x808a0008, 0x804a0008, 0x80f50009,
	0x00050007, 0x80560008, 0x80160008, 0x00000008,
	0x00330307, 0x80760008, 0x80360008, 0x80cd0009,
	0x000f0107, 0x80660008, 0x80260008, 0x80ad0009,
	0x80060008, 0x80860008, 0x80460008, 0x80ed0009,
	0x00090007, 0x805e0008, 0x801e0008, 0x809d0009,
	0x00630407, 0x807e0008, 0x803e0008, 0x80dd0009,
	0x001b0207, 0x806e0008, 0x802e0008, 0x80bd0009,
	0x800e0008, 0x808e0008, 0x804e0008, 0x80fd0009,
	0x0100c007, 0x80510008, 0x80110008, 0x00830508,
	0x001f0207, 0x80710008, 0x80310008, 0x80c30009,
	0x000a0007, 0x80610008, 0x80210008, 0x80a30009,
	0x80010008, 0x80810008, 0x80410008, 0x80e30009,
	0x00060007, 0x80590008, 0x80190008, 0x80930009,
	0x003b0307, 0x80790008, 0x80390008, 0x80d30009,
	0x00110107, 0x80690008, 0x80290008, 0x80b30009,
	0x80090008, 0x80890008, 0x80490008, 0x80f30009,
	0x00040007, 0x80550008, 0x80150008, 0x01020008,
	0x002b0307, 0x80750008, 0x80350008, 0x80cb0009,
	0x000d0107, 0x80650008, 0x80250008, 0x80ab0009,
	0x80050008, 0x80850008, 0x80450008, 0x80eb0009,
	0x00080007, 0x805d0008, 0x801d0008, 0x809b0009,
	0x00530407, 0x807d0008, 0x803d0008, 0x80db0009,
	0x00170207, 0x806d0008, 0x802d0008, 0x80bb0009,
	0x800d0008, 0x808d0008, 0x804d0008, 0x80fb0009,
	0x00030007, 0x80530008, 0x80130008, 0x00c30508,
	0x00230307, 0x80730008, 0x80330008, 0x80c70009,
	0x000b0107, 0x80630008, 0x80230008, 0x80a70009,
	0x80030008, 0x80830008, 0x80430008, 0x80e70009,
	0x00070007, 0x805b0008, 0x801b0008, 0x80970009,
	0x00430407, 0x807b0008, 0x803b0008, 0x80d70009,
	0x00130207, 0x806b0008, 0x802b0008, 0x80b70009,
	0x800b0008, 0x808b0008, 0x804b0008, 0x80f70009,
	0x00050007, 0x80570008, 0x80170008, 0x00000008,
	0x00330307, 0x80770008, 0x80370008, 0x80cf0009,
	0x000f0107, 0x80670008, 0x80270008, 0x80af0009,
	0x80070008, 0x80870008, 0x80470008, 0x80ef0009,
	0x00090007, 0x805f0008, 0x801f0008, 0x809f0009,
	0x00630407, 0x807f0008, 0x803f0008, 0x80df0009,
	0x001b0207, 0x806f0008, 0x802f0008, 0x80bf0009,
	0x800f0008, 0x808f0008, 0x804f0008, 0x80ff0009
};

static const uint32 dsttctable_[] = {
	0x00010005, 0x01010705, 0x00110305, 0x10010b05,
	0x00050105, 0x04010905, 0x00410505, 0x40010d05,
	0x00030005, 0x02010805, 0x00210405, 0x20010c05,
	0x00090205, 0x08010a05, 0x00810605, 0x3d3d206f,
	0x00020005, 0x01810705, 0x00190305, 0x18010b05,
	0x00070105, 0x06010905, 0x00610505, 0x60010d05,
	0x00040005, 0x03010805, 0x00310405, 0x30010c05,
	0x000d0205, 0x0c010a05, 0x00c10605, 0x00003025,
	0x00010005, 0x01010705, 0x00110305, 0x10010b05,
	0x00050105, 0x04010905, 0x00410505, 0x40010d05,
	0x00030005, 0x02010805, 0x00210405, 0x20010c05,
	0x00090205, 0x08010a05, 0x00810605, 0x3d3d206f,
	0x00020005, 0x01810705, 0x00190305, 0x18010b05,
	0x00070105, 0x06010905, 0x00610505, 0x60010d05,
	0x00040005, 0x03010805, 0x00310405, 0x30010c05,
	0x000d0205, 0x0c010a05, 0x00c10605, 0x00003025,
	0x00010005, 0x01010705, 0x00110305, 0x10010b05,
	0x00050105, 0x04010905, 0x00410505, 0x40010d05,
	0x00030005, 0x02010805, 0x00210405, 0x20010c05,
	0x00090205, 0x08010a05, 0x00810605, 0x3d3d206f,
	0x00020005, 0x01810705, 0x00190305, 0x18010b05,
	0x00070105, 0x06010905, 0x00610505, 0x60010d05,
	0x00040005, 0x03010805, 0x00310405, 0x30010c05,
	0x000d0205, 0x0c010a05, 0x00c10605, 0x00003025,
	0x00010005, 0x01010705, 0x00110305, 0x10010b05,
	0x00050105, 0x04010905, 0x00410505, 0x40010d05,
	0x00030005, 0x02010805, 0x00210405, 0x20010c05,
	0x00090205, 0x08010a05, 0x00810605, 0x3d3d206f,
	0x00020005, 0x01810705, 0x00190305, 0x18010b05,
	0x00070105, 0x06010905, 0x00610505, 0x60010d05,
	0x00040005, 0x03010805, 0x00310405, 0x30010c05,
	0x000d0205, 0x0c010a05, 0x00c10605, 0x00003025,
	0x00010005, 0x01010705, 0x00110305, 0x10010b05,
	0x00050105, 0x04010905, 0x00410505, 0x40010d05,
	0x00030005, 0x02010805, 0x00210405, 0x20010c05,
	0x00090205, 0x08010a05, 0x00810605, 0x3d3d206f,
	0x00020005, 0x01810705, 0x00190305, 0x18010b05,
	0x00070105, 0x06010905, 0x00610505, 0x60010d05,
	0x00040005, 0x03010805, 0x00310405, 0x30010c05,
	0x000d0205, 0x0c010a05, 0x00c10605, 0x00003025,
	0x00010005, 0x01010705, 0x00110305, 0x10010b05,
	0x00050105, 0x04010905, 0x00410505, 0x40010d05,
	0x00030005, 0x02010805, 0x00210405, 0x20010c05,
	0x00090205, 0x08010a05, 0x00810605, 0x3d3d206f,
	0x00020005, 0x01810705, 0x00190305, 0x18010b05,
	0x00070105, 0x06010905, 0x00610505, 0x60010d05,
	0x00040005, 0x03010805, 0x00310405, 0x30010c05,
	0x000d0205, 0x0c010a05, 0x00c10605, 0x00003025,
	0x00010005, 0x01010705, 0x00110305, 0x10010b05,
	0x00050105, 0x04010905, 0x00410505, 0x40010d05,
	0x00030005, 0x02010805, 0x00210405, 0x20010c05,
	0x00090205, 0x08010a05, 0x00810605, 0x3d3d206f,
	0x00020005, 0x01810705, 0x00190305, 0x18010b05,
	0x00070105, 0x06010905, 0x00610505, 0x60010d05,
	0x00040005, 0x03010805, 0x00310405, 0x30010c05,
	0x000d0205, 0x0c010a05, 0x00c10605, 0x00003025,
	0x00010005, 0x01010705, 0x00110305, 0x10010b05,
	0x00050105, 0x04010905, 0x00410505, 0x40010d05,
	0x00030005, 0x02010805, 0x00210405, 0x20010c05,
	0x00090205, 0x08010a05, 0x00810605, 0x3d3d206f,
	0x00020005, 0x01810705, 0x00190305, 0x18010b05,
	0x00070105, 0x06010905, 0x00610505, 0x60010d05,
	0x00040005, 0x03010805, 0x00310405, 0x30010c05,
	0x000d0205, 0x0c010a05, 0x00c10605, 0x00003025
};

static const uint32* dsttctable = dsttctable_;
static const uint32* lsttctable = lsttctable_;

#endif
