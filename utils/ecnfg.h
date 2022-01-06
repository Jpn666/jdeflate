/*
 * Copyright (C) 2017, jpn jpn@gsforce.net
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

#ifndef ded5069c_f51d_4cf2_a759_7488070cc0ea
#define ded5069c_f51d_4cf2_a759_7488070cc0ea

/*
 * ecnfg.h
 * An small event based configuration parser.
 * 
 * Example sintaxis:
 * ############################################################################
 * # Single entry key-value
 * ############################################################################
 * 
 * # Integers.
 * Entry0: 55544  # decimal
 * Entry1: 0x124  # hexadecinal integer
 * 
 * # Floats.
 * Entry3: 1.23554
 * Entry4: 1E10
 * Entry5: 1.5E10
 * Entry6: .25
 * 
 * # Strings.
 * Entry7: "String value"
 * 
 * # Multi line string.
 * Entry8: "This is a multi-line string."
 *         "This line is part of the same string above."
 *         "This line too."
 * Entry9: "This string" " " "will be merged with the previews one."
 * 
 * # Escape sequences takes the form of $(unicode codepoint value):
 * EntryA: "Hello$(20)World"
 * EntryB: "Hello$(0a 0a 0a)World"
 * 
 * ############################################################################
 * # Complex stuff
 * ############################################################################
 * 
 * # Directives.
 * Directive0 "some string" 80 23.8E10
 * EnableSomething
 * 
 * # Sections.
 * # The colon can be omitted if you are defining a section.
 * Section0 {
 *     
 *     # A nested section
 *     Section1: {
 *         Entry1: 40.2
 *     }
 * }
 * 
 * 
 * ############################################################################
 *
 * Usage:
 * ecnfg_setinputfn(cfg, inputcallback, context);
 * ...
 * while ((evnttype = ecnfg_nextevnttype(cfg))) {
 *     switch(evnttype) {
 *         case ECNFG_EVNTSECTIONBGN: {
 *             const char* identifier;
 *             
 *             identifier = ecnfg_getlval(cfg);
 *             break;
 *         }
 *         case ECNFG_EVNTSECTIONEND:
 *             break;
 *         case ECNFG_EVNTDIRECTIVE: {
 *             const char* identifier;
 *             
 *             identifier = ecnfg_getlval(cfg);
 *             while ((rvaltype = ecnfg_nextrvaltype(cfg))) {
 *                 const char* parameter;
 *                 
 *                 parameter = ecnfg_getrval(cfg);
 *                 switch (rvaltype) {
 *                     case ECNFG_TYPEFLT: break;
 *                     case ECNFG_TYPEINT: break;
 *                     case ECNFG_TYPESTR: break;
 *                 }
 *             }
 *             break;
 *         }
 *         case ECNFG_EVNTENTRY: {
 *             const char* identifier;
 *             
 *             identifier = ecnfg_getlval(cfg);
 *             switch ((rvaltype = ecnfg_nextrvaltype(cnfg))) {
 *                 case ECNFG_TYPEFLT: break;
 *                 case ECNFG_TYPEINT: break;
 *                 case ECNFG_TYPESTR: break;
 *             }
 *             const char* value = ecnfg_getrval(cfg);
 *             break;
 *         }
 *     }
 * }
 * 
 * ...check for errors
 * if (ecnfg_getstate(cfg, NULL, NULL) == ECNFG_ABORTED) {
 *     ...
 * }
 * 
 */

#include "../ctoolbox.h"


#define ECNFG_MINBFFRSZ 0x1000UL
#define ECNFG_MAXBFFRSZ 0x4000UL


/* Events */
typedef enum {
	ECNFG_EVNTNONE       = 0,
	ECNFG_EVNTSECTIONBGN = 1,
	ECNFG_EVNTSECTIONEND = 2,
	ECNFG_EVNTENTRY      = 3,
	ECNFG_EVNTDIRECTIVE  = 4
} eECNFGEvent;


/* Entry types */
typedef enum {
	ECNFG_TYPENONE = 0,
	ECNFG_TYPESTR  = 1,
	ECNFG_TYPEINT  = 2,
	ECNFG_TYPEFLT  = 3
} eECNFGType;


/* State */
typedef enum {
	ECNFG_READY   = 0,
	ECNFG_PARSING = 1,
	ECNFG_DONE    = 2,
	ECNFG_ABORTED = 3
} eECNFGState;


/* Error codes */
typedef enum {
	ECNFG_OK               = 0,
	ECNFG_EINVALIDTKN      = 1,
	ECNFG_EUNTERMINATEDSTR = 2,
	ECNFG_EBUFFERLIMIT     = 3,
	ECNFG_EUNSPECTEDTKN    = 4,
	ECNFG_EUNPAIREDSECTION = 5,
	ECNFG_EINVALIDCHR      = 6,
	ECNFG_EINVALIDESCAPESQ = 7,
	ECNFG_EINPUT           = 8,
	ECNFG_ELIMIT           = 9,
	ECNFG_EOOM             = 10
} eECNFGError;


/*
 * IO function prototype.
 * The return value must be the number of bytes read to the buffer (zero if
 * there is no more input or a negative value to indicate an error). */
typedef intxx (*TECInputFn)(uint8* buffer, uintxx size, void* payload);


/* */
struct TECnfg {
	uintxx state;
	uintxx event;
	intxx  rvcnt;  /* rvalues counter */
	intxx  sncnt;  /* section counter */
	
	/* input callback */
	TECInputFn inputfn;
	
	/* input callback parameter */
	void* payload;
	
	/* lexer */
	uintxx lastchr;
	uintxx line;
	uintxx token;
	uintxx error;
	
	uint8* buffer;
	uint8* bufferbgn;
	uint8* bufferend;
	
	uint8* buffermem;
	uintxx buffersz;
	
	uint8* input;
	uint8* inputend;
	
	/* input buffer */
	uint8* inputbgn;
};

typedef struct TECnfg TECnfg;


/*
 * Creates a new TECnfg struct. */
TECnfg* ecnfg_create(void);

/*
 * ... */
void ecnfg_destroy(TECnfg*);

/*
* ... */
void ecnfg_reset(TECnfg*);

/*
 * */
CTB_INLINE void ecnfg_setinputfn(TECnfg*, TECInputFn inputfn, void* payload);

/*
 * Get the current state */
CTB_INLINE eECNFGState ecnfg_getstate(TECnfg*, uintxx* error, uintxx* lline);

/*
 * Aborts the parsing. */
CTB_INLINE void ecnfg_abort(TECnfg*);

/*
 * Get the next event. */
eECNFGEvent ecnfg_nextevnttype(TECnfg*);

/*
 * Get the rvalue type. */
eECNFGType ecnfg_nextrvaltype(TECnfg*);

/*
 * Returns a const pointer to the identifier string. */
const char* ecnfg_getlval(TECnfg*);

/* 
 * Returns a const pointer to the rvalue as string. */
const char* ecnfg_getrval(TECnfg*);


/*
 * Inlines */

CTB_INLINE void
ecnfg_setinputfn(TECnfg* cfg, TECInputFn inputfn, void* payload)
{
	ASSERT(cfg);
	
	cfg->inputfn = inputfn;
	cfg->payload = payload;
}

CTB_INLINE eECNFGState
ecnfg_getstate(TECnfg* cfg, uintxx* error, uintxx* lline)
{
	ASSERT(cfg);
	
	if (error)
		error[0] = cfg->error;
	if (lline)
		lline[0] = cfg->line;
	return cfg->state;
}

CTB_INLINE void
ecnfg_abort(TECnfg* cfg)
{
	ASSERT(cfg);

	cfg->state = ECNFG_ABORTED;
}

#endif
