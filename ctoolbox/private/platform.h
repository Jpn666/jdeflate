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

#ifndef C35BB22C_EEDB_4B27_AFBE_61BC41FE51C6
#define C35BB22C_EEDB_4B27_AFBE_61BC41FE51C6

/*
 * platform.h
 * Platform related macros/stuff.
 */

#if !defined(CTB_INTERNAL_INCLUDE_GUARD)
#	error "this file can't be included directly"
#endif


#define CTB_PLATFORM_UNIX     0x01
#define CTB_PLATFORM_BEOS     0x02
#define CTB_PLATFORM_WINDOWS  0x03
#define CTB_PLATFORM_DOS      0x04
#define CTB_PLATFORM_OS2      0x05
#define CTB_PLATFORM_UNKNOWN  0x00


/* */
#if !defined(CTB_PLATFORM) && defined(CTB_CFG_PLATFORM_UNIX)
#	define CTB_PLATFORM CTB_PLATFORM_UNIX
#endif

#if !defined(CTB_PLATFORM) && defined(CTB_CFG_PLATFORM_BEOS)
#	define CTB_PLATFORM CTB_PLATFORM_BEOS
#endif

#if !defined(CTB_PLATFORM) && defined(CTB_CFG_PLATFORM_WINDOWS)
#	define CTB_PLATFORM CTB_PLATFORM_WINDOWS
#endif

#if !defined(CTB_PLATFORM) && defined(CTB_CFG_PLATFORM_DOS)
#	define CTB_PLATFORM CTB_PLATFORM_DOS
#endif

#if !defined(CTB_PLATFORM) && defined(CTB_CFG_PLATFORM_OS2)
#	define CTB_PLATFORM CTB_PLATFORM_OS2
#endif

#if !defined(CTB_PLATFORM)
#	define CTB_PLATFORM CTB_PLATFORM_UNKNOWN
#endif


/* 
 * The follow code tries to determine the word size */

#if defined(__GNUC__) && defined(__WORDSIZE)
#	if __WORDSIZE == 32
#		define CTB_WORDSIZE 32
#	endif
#	if __WORDSIZE == 64
#		define CTB_WORDSIZE 64
#	endif
#endif

#if defined(__POCC__) && (__POCC__ >= 500)
#	if __POCC_TARGET__ == 1
#		define CTB_WORDSIZE 32
#	endif
#	if __POCC_TARGET__ == 3
#		define CTB_WORDSIZE 64
#	endif
#endif

#if defined(__MSVC__)
#	if defined(_M_X64)
#		define CTB_WORDSIZE 64
#	else
#		define CTB_WORDSIZE 32
#	endif
#endif


#if !defined(CTB_WORDSIZE)
#	if defined(__x86_64__)
#		define CTB_WORDSIZE 64
#	endif
#endif


/* ... */
#if defined(CTB_CFG_ENV64)
#	if defined(CTB_WORDSIZE)
#		undef CTB_WORDSIZE
#	endif
#	define CTB_WORDSIZE 64
#endif

#if defined(CTB_WORDSIZE)
#	if CTB_WORDSIZE == 64
#		define CTB_ENV64
#	endif
#	undef CTB_WORDSIZE
#endif


#if defined(CTB_CFG_STRICTALIGNMENT)
#	define CTB_STRICTALIGNMENT
#endif


#if defined(CTB_CFG_FASTUNALIGNED)
#	define CTB_FASTUNALIGNED
#endif


#undef CTB_CFG_ENV64
#undef CTB_CFG_STRICTALIGNMENT
#undef CTB_CFG_FASTUNALIGNED

#endif

