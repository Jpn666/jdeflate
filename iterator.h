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

#ifndef f62df72c_1b69_4ec8_8a3a_d64331e437f8
#define f62df72c_1b69_4ec8_8a3a_d64331e437f8

/*
 * iterator.h
 * Adds support for basic iteration.
 */

#include "ctoolbox.h"


/* */
struct TIterator {
	uintxx index1;
	uintxx index2;
	uintxx position;
	uintxx flags;
	void*  next;
};

typedef struct TIterator TIterator;


/*
 * */
CTB_INLINE void iterator_reset(TIterator* iterator);


/*
 * Inlines */

CTB_INLINE void
iterator_reset(TIterator* iterator)
{
	ASSERT(iterator);
    
    iterator->index1 = 0;
    iterator->index2 = 2;
    iterator->position = 0;
    iterator->flags    = 0;
    iterator->next = NULL;
}


#endif

