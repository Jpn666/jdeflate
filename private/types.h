/*
 * Copyright (C) 2014, jpn jpn@gsforce.net
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

#ifndef FCEB37D0_F06E_4D01_A070_0F8C481D6196
#define FCEB37D0_F06E_4D01_A070_0F8C481D6196

/*
 * types.h
 * Portable data types definitions.
 */

#if !defined(CTB_INTERNAL_INCLUDE_GUARD)
#	error "this file can't be included directly"
#endif

#include <stddef.h>
#include <limits.h>


#ifndef NULL
#	define NULL ((*void) 0)
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#	if !defined(CTB_CFG_NOINTTYPES)
#		define CTB_HAVEINTTYPES
#		include <inttypes.h>
#	endif
#	if !defined(CTB_CFG_NOSTDBOOL)
#		define CTB_HAVESTDBOOL
#		include <stdbool.h>
#	endif
#endif


/* 
 * Boolean */

#if !defined(CTB_HAVESTDBOOL)
typedef int custombool;
#	if !defined(bool)
#       define bool custombool
#	endif
#endif


/*
 * Integers */

#if defined(CTB_HAVEINTTYPES)
#	define ADD_TYPE(A, B) typedef B##_t B
#else
#	define ADD_TYPE(A, B) typedef A B
#endif

ADD_TYPE(signed int, int32);
ADD_TYPE(unsigned int, uint32);
ADD_TYPE(signed short int, int16);
ADD_TYPE(unsigned short int, uint16);
ADD_TYPE(signed char, int8);
ADD_TYPE(unsigned char, uint8);


#if defined(CTB_CFG_NOINT64)  /* configuration flag */
#	define CTB_NOINT64
#endif

#if (defined(__MSVC__) && !defined(__POCC__)) || defined(__BORLANDC__)
typedef   signed __int64  int64;
typedef unsigned __int64 uint64;
#   define CTB_HAVEINT64 1
#endif

/* using gcc extension in c89 */
#if !defined(CTB_NOINT64) && !defined(__STDC_VERSION__) && defined(__GNUC__)
__extension__ typedef signed long long int    int64;
__extension__ typedef unsigned long long int uint64;
#	define CTB_HAVEINT64 1
#endif


/* ISO C90 does not support long long */
#if !defined(CTB_NOINT64) && !defined(__STDC_VERSION__)
#	if !defined(__POCC__)
#		define CTB_NOINT64
#	endif
#endif

#if !defined(CTB_HAVEINT64) && !defined(CTB_NOINT64)
ADD_TYPE(signed long long int, int64);
ADD_TYPE(unsigned long long int, uint64);
#	define CTB_HAVEINT64 1
#endif

#undef ADD_TYPE


#if !defined(CTB_HAVEINT64)
#	define CTB_HAVEINT64 0
#endif

/* fast (target platform word size) integer */
#if CTB_HAVEINT64 && defined(CTB_ENV64)
typedef  int64  intxx;
typedef uint64 uintxx;
#else
typedef  int32  intxx;  /* it's the same on 16-bits platforms */
typedef uint32 uintxx;
#endif

/* produce compile errors if the sizes aren't right */
typedef union
{
	char i1_incorrect[(sizeof(  int8) == 1) - 1];
	char u1_incorrect[(sizeof( uint8) == 1) - 1];
	char i2_incorrect[(sizeof( int16) == 2) - 1];
	char u2_incorrect[(sizeof(uint16) == 2) - 1];
	char i4_incorrect[(sizeof( int32) == 4) - 1];
	char u4_incorrect[(sizeof(uint32) == 4) - 1];
    
#if CTB_HAVEINT64 == 1
	char i8_incorrect[(sizeof( int64) == 8) - 1];
	char u8_incorrect[(sizeof(uint64) == 8) - 1];
#endif
} TTypeStaticAssert;


/*
 * Extra types */

/* error code */
typedef intxx eintxx;

/* added to ensure portability */
typedef  float float32;
typedef double float64;

typedef float32 flt32;
typedef float64 flt64;


/* 
 * Funtion types */

/* used for comparison */
typedef intxx (*TCmpFn)(const void*, const void*);

/* to get a hash value */
typedef uintxx (*THashFn)(void*);

/* to check for equality */
typedef bool (*TEqualFn)(const void*, const void*);

/* */
typedef void (*TFreeFn)(void*);

/* */
typedef void (*TUnaryFn)(void*);


/* integers limits */
#ifndef INT8_MIN
#	define  INT8_MIN 0x00000080UL
#endif
#ifndef INT8_MAX
#	define  INT8_MAX 0x0000007FUL
#endif
#ifndef INT16_MIN
#	define INT16_MIN 0x00008000UL
#endif
#ifndef INT16_MAX
#	define INT16_MAX 0x00007FFFUL
#endif
#ifndef INT32_MIN
#	define INT32_MIN 0x80000000UL
#endif
#ifndef INT32_MAX
#	define INT32_MAX 0x7FFFFFFFUL
#endif

#if CTB_HAVEINT64
#	ifndef INT64_MIN
#		define INT64_MIN 0x8000000000000000ULL
#	endif
#	ifndef INT64_MAX
#		define INT64_MAX 0x7FFFFFFFFFFFFFFFULL
#	endif
#endif


#ifndef UINT8_MAX
#	define  UINT8_MAX 0x000000FFUL
#endif
#ifndef UINT32_MAX
#	define UINT32_MAX 0xFFFFFFFFUL
#endif
#ifndef UINT16_MAX
#	define UINT16_MAX 0x0000FFFFUL
#endif

#if CTB_HAVEINT64
#	ifndef UINT64_MAX
#		define UINT64_MAX 0xFFFFFFFFFFFFFFFFULL
#	endif
#endif


#if CTB_HAVEINT64 && defined(CTB_ENV64)
	/* 64 bits */
#	define INTXX_MIN INT64_MIN
#	define INTXX_MAX INT64_MAX
	
#	define UINTXX_MAX UINT64_MAX
#else
	/* 32 bits */
#	define INTXX_MIN INT32_MIN
#	define INTXX_MAX INT32_MAX
	
#	define UINTXX_MAX UINT32_MAX
#endif


#endif

