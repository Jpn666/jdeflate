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

#ifndef c71a75a9_d526_4e90_a75a_d8189a1c3a3e
#define c71a75a9_d526_4e90_a75a_d8189a1c3a3e

/*
 * ctoolbox.h
 * ...
 */


#include "config.h"
#include <stdlib.h>
#include <string.h>


#if defined(_MSC_VER)
    #define __MSVC__
#endif


#if !defined(NDEBUG)
#   define NDEBUG
#endif
#if defined(DEBUG) || defined(__DEBUG__) || defined(_DEBUG)
#   undef NDEBUG
#   define CTB_DEBUG
#endif

#define ASSERT(x) assert(x)
#include <assert.h>


/*
 * Inline and force inline */

#if defined(__MSVC__) || defined(__BORLANDC__) || \
    defined(__DMC__)      || \
    defined(__SC__)       || \
    defined(__WATCOMC__)  || \
    defined(__LCC__)      || \
    defined(__DECC)       || \
    defined(__POCC__)     || \
    defined (__MWERKS__)
#   define CTB_INLINE static __inline
#endif

#if !defined(CTB_INLINE)
#   if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#       define CTB_INLINE static inline
#   else  /* ansi c */
#       if defined(__GNUC__)
#           define CTB_INLINE static __inline__
#       else
#           define CTB_INLINE static
#       endif
#   endif
#endif


#if defined(__MSVC__)
#   define CTB_FORCEINLINE static __forceinline
#endif

#if !defined(CTB_FORCEINLINE)
#   if defined(__GNUC__) && (__GNUC__ >= 3 && __GNUC_MINOR__ >= 1)
#       define CTB_FORCEINLINE __attribute__((always_inline)) static __inline__
#   else
#       define CTB_FORCEINLINE CTB_INLINE
#   endif
#endif


#if defined(__GNUC__)
    #define   LIKELY(X) __builtin_expect((X), 1)
    #define UNLIKELY(X) __builtin_expect((X), 0)
#else
    #if defined(__has_builtin)
        #if __has_builtin(__builtin_expect)
            #define   LIKELY(X) __builtin_expect((X), 1)
            #define UNLIKELY(X) __builtin_expect((X), 0)
        #endif
    #endif
#endif

#if !defined(LIKELY)
    #define   LIKELY(X) (X)
    #define UNLIKELY(X) (X)
#endif


/*
 * Internal Includes */

#define CTB_INTERNAL_INCLUDE_GUARD
/* order matters */
#include "private/platform.h"
#include "private/types.h"
#include "private/endianness.h"
#undef CTB_INTERNAL_INCLUDE_GUARD


/*
 * Memory allocation */

#define CTB_MALLOC  malloc
#define CTB_CALLOC  calloc
#define CTB_REALLOC realloc
#define CTB_FREE    free


/* Error values */
typedef enum {
    CTB_OK     =  0,
    CTB_EPARAM = -1,
    CTB_ERANGE = -2,
    CTB_EOOM   = -3,
    CTB_ENKEY = -4
} eCTBError;


#endif
