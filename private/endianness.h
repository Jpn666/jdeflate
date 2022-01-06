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

#ifndef FEE95E12_79BC_407A_A207_787DA31265E4
#define FEE95E12_79BC_407A_A207_787DA31265E4

/*
 * endianness.h
 * Endianness stuff.
 */

#if !defined(CTB_INTERNAL_INCLUDE_GUARD)
#	error "this file can't be included directly"
#endif

#include "platform.h"
#include "types.h"


#define CTB_LITTLEENDIAN 1234
#define CTB_BIGENDIAN    4321


#if defined(__GLIBC__)
#	include <endian.h>
#	if __BYTE_ORDER == __LITTLE_ENDIAN
#		define CTB_BYTEORDER CTB_LITTLEENDIAN
#	endif
#	if __BYTE_ORDER == __BIG_ENDIAN
#		define CTB_BYTEORDER CTB_BIGENDIAN
#	endif
#endif


 /* configuration flags */
#if defined(CTB_CFG_LITTLEENDIAN) && defined(CTB_CFG_BIGENDIAN)
#	error "pick just one endianess"
#endif

#if defined(CTB_CFG_LITTLEENDIAN)
#	if defined(CTB_BYTEORDER)
#		undef CTB_BYTEORDER
#	endif
#	define CTB_BYTEORDER CTB_LITTLEENDIAN
#endif

#if defined(CTB_CFG_BIGENDIAN)
#	if defined(CTB_BYTEORDER)
#		undef CTB_BYTEORDER
#	endif
#	define CTB_BYTEORDER CTB_BIGENDIAN
#endif


#if !defined(CTB_BYTEORDER)
#	error "can't determine the correct endiannes"
#endif

/*
 * Swap macros/functions */

/* little endian */
#if CTB_BYTEORDER == CTB_LITTLEENDIAN
#	define CTB_IS_LITTLEENDIAN 1
#	define CTB_IS_BIGENDIAN    0

#	if defined(CTB_HAVEINT64) && CTB_HAVEINT64
#		define CTB_SWAP64ONLE(x) (ctb_swap64(x))
#		define CTB_SWAP64ONBE(x) (x)
#	endif
#	define CTB_SWAP32ONLE(x) (ctb_swap32(x))
#	define CTB_SWAP16ONLE(x) (ctb_swap16(x))
#	define CTB_SWAP32ONBE(x) (x)
#	define CTB_SWAP16ONBE(x) (x)

#endif

/* big endian */
#if CTB_BYTEORDER == CTB_BIGENDIAN
#	define CTB_IS_LITTLEENDIAN 0
#	define CTB_IS_BIGENDIAN    1

#	if defined(CTB_HAVEINT64) && CTB_HAVEINT64
#		define CTB_SWAP64ONBE(x) (ctb_swap64(x))
#		define CTB_SWAP64ONLE(x) (x)
#	endif
#	define CTB_SWAP32ONBE(x) (ctb_swap32(x))
#	define CTB_SWAP16ONBE(x) (ctb_swap16(x))
#	define CTB_SWAP32ONLE(x) (x)
#	define CTB_SWAP16ONLE(x) (x)
#endif


#if defined(__GNUC__) && !defined(__clang__)
#	if __GNUC__ >= 4 && __GNUC_MINOR__ >= 3
#		define ctb_swap16(val) (((val) << 8) | ((val) >> 8))
#		define ctb_swap32(val) (__builtin_bswap32((val)))
#		define ctb_swap64(val) (__builtin_bswap64((val)))
#	endif
#endif

#if defined(__clang__)  /* fixme */
#	if defined(__has_builtin) && __has_builtin(__builtin_bswap16)
#		define ctb_swap16(val) (__builtin_bswap16((val)))
#		define ctb_swap32(val) (__builtin_bswap32((val)))
#		define ctb_swap64(val) (__builtin_bswap64((val)))
#	endif
#endif


/*
 * Inlines */

#if !defined(ctb_swap16)

CTB_INLINE uint16
ctb_swap16(uint16 n)
{
	return (n << 8) | (n >> 8);
}

CTB_INLINE uint32
ctb_swap32(uint32 n)
{
	return ((n & 0x000000FFUL) << 24) |
	       ((n & 0xFF000000UL) >> 24) |
	       ((n & 0x00FF0000UL) >> 8 ) |
	       ((n & 0x0000FF00UL) << 8 );
}

#if CTB_HAVEINT64

CTB_INLINE uint64
ctb_swap64(uint64 n)
{
	return (n >> 56) |
	   ((n << 40) & 0x00FF000000000000LL) |
	   ((n << 24) & 0x0000FF0000000000LL) |
	   ((n << 8 ) & 0x000000FF00000000LL) |
	   ((n >> 8 ) & 0x00000000FF000000LL) |
	   ((n >> 24) & 0x0000000000FF0000LL) |
	   ((n >> 40) & 0x000000000000FF00LL) |
	   (n << 56);
}

#endif
#endif

#endif

