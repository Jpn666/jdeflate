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

#ifndef eb30206a_e4f3_4390_b437_3d1715b2936d
#define eb30206a_e4f3_4390_b437_3d1715b2936d

/*
 * ctype.h
 * Character clasification functions.
 */

#include "ctoolbox.h"


extern const uint8 ctb_ctypeproperties[256];


#define CTB_CTYPEUPPER 0x01  /* upper */
#define CTB_CTYPELOWER 0x02  /* lower */
#define CTB_CTYPESPACE 0x04  /* space */
#define CTB_CTYPEPRINT 0x08  /* print */
#define CTB_CTYPECNTRL 0x10  /* cntrl */
#define CTB_CTYPEPUNCT 0x20  /* punct */
#define CTB_CTYPEDIGIT 0x40  /* digit */

#define CTB_CTYPEALPHA (CTB_CTYPEUPPER | CTB_CTYPELOWER)
#define CTB_CTYPEALNUM (CTB_CTYPEALPHA | CTB_CTYPEDIGIT)


/*
 * */
CTB_INLINE intxx ctb_isspace(uintxx c);

/*
 * */
CTB_INLINE intxx ctb_isalnum(uintxx c);

/*
 * */
CTB_INLINE intxx ctb_isalpha(uintxx c);

/*
 * */
CTB_INLINE intxx ctb_isdigit(uintxx c);

/*
 * */
CTB_INLINE intxx ctb_iscntrl(uintxx c);

/*
 * */
CTB_INLINE intxx ctb_ispunct(uintxx c);

/*
 * */
CTB_INLINE intxx ctb_islower(uintxx c);

/*
 * */
CTB_INLINE intxx ctb_isupper(uintxx c);

/*
 * */
CTB_INLINE intxx ctb_isprint(uintxx c);

/*
 * */
CTB_INLINE intxx ctb_isascii(uintxx c);


/*
 * Inlines */

#define GET_PROPERTY(c) (ctb_ctypeproperties[(intxx) ((uint8) (c))])


CTB_INLINE intxx 
ctb_isspace(uintxx c)
{
	return (GET_PROPERTY(c) & (CTB_CTYPESPACE)) != 0;
}

CTB_INLINE intxx 
ctb_isalpha(uintxx c)
{
	return (GET_PROPERTY(c) & (CTB_CTYPEALPHA)) != 0;
}

CTB_INLINE intxx
ctb_isdigit(uintxx c)
{
	return (GET_PROPERTY(c) & (CTB_CTYPEDIGIT)) != 0;
}

CTB_INLINE intxx
ctb_iscntrl(uintxx c)
{
	return (GET_PROPERTY(c) & (CTB_CTYPECNTRL)) != 0;
}

CTB_INLINE intxx
ctb_ispunct(uintxx c)
{
	return (GET_PROPERTY(c) & (CTB_CTYPEPUNCT)) != 0;
}

CTB_INLINE intxx
ctb_isalnum(uintxx c)
{
	return (GET_PROPERTY(c) & (CTB_CTYPEALNUM)) != 0;
}

CTB_INLINE intxx
ctb_isupper(uintxx c)
{
	return (GET_PROPERTY(c) & (CTB_CTYPEUPPER)) != 0;
}

CTB_INLINE intxx
ctb_islower(uintxx c)
{
	return (GET_PROPERTY(c) & (CTB_CTYPELOWER)) != 0;
}

CTB_INLINE intxx
ctb_isprint(uintxx c)
{
	return (GET_PROPERTY(c) & (CTB_CTYPEPRINT)) != 0;
}

CTB_INLINE intxx
ctb_isascii(uintxx c)
{
	if (c < 0x80)
		return 1;
	return 0;
}

#undef GET_PROPERTY

#endif
