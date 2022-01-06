/*
 * Copyright (C) 2016, jpn jpn@gsforce.net
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

#ifndef f659f361_d083_478a_80a4_a6b6df1b4d79
#define f659f361_d083_478a_80a4_a6b6df1b4d79

/*
 * dynarray.h
 * Dynamic array of fixed size elements using a unique block of memory.
 */

#include "ctoolbox.h"
#include "iterator.h"


#define DYNARRAY_MINSIZE  8


/* */
struct TDynArray {
	uintxx capacity;
	uintxx used;
	uintxx datasize;
	
	uint8* buffer;
};

typedef struct TDynArray TDynArray;


#define DYNARRAY_AT(a, i) ((a)->buffer + ((a)->datasize * (i)))


/*
 * Creates a new array, returns NULL on failure. */
TDynArray* dynarray_create(uintxx capacity, uintxx datasize);

/*
 * Destroys and free the given array. */
void dynarray_destroy(TDynArray*);

/*
 * Inserts an element at the given index. If the index is greater than the
 * array size the array will be expanded. */
eintxx dynarray_insert(TDynArray*, void* element, uintxx index);

/*
 * Removes the element at the given index. */
eintxx dynarray_remove(TDynArray*, uintxx index);

/*
 * Returns the element at the given index. */
CTB_INLINE void* dynarray_at(TDynArray*, uintxx index);

/*
 * Same as array_at, but will not assert on failure. */
CTB_INLINE void* dynarray_safe_at(TDynArray*, uintxx index);

/*
 * Reduces the array capacity to the minimun size. */
eintxx dynarray_shrink(TDynArray*);

/*
 * Ensures that the array has (at least) a size equal to size. */
eintxx dynarray_reserve(TDynArray*, uintxx size);

/*
 * Sort the elements in the array using the given function. */
void dynarray_sort(TDynArray*, TCmpFn cmpfn, void* swapv);

/*
 * Added to keep the same interface for all array like types. */
CTB_INLINE void* dynarray_next(TDynArray*, TIterator* it);

/*
 * Clears the array. */
eintxx dynarray_clear(TDynArray*, TFreeFn freefn);

/*
 * Removes the last element in the array. */
CTB_INLINE eintxx dynarray_pop(TDynArray*);

/*
 * Inserts an element at the end. */
CTB_INLINE eintxx dynarray_append(TDynArray*, void* element);

/*
 * Set the value of the element at the given index. */
CTB_INLINE eintxx dynarray_set(TDynArray*, uintxx index, void* element);

/*
 * Returns the array size. */
CTB_INLINE uintxx dynarray_size(TDynArray*);

/*
 * Returns the array capacity. */
CTB_INLINE uintxx dynarray_capacity(TDynArray*);

/*
 * Returns true is the given value is into the array range. */
CTB_INLINE bool dynarray_checkrange(TDynArray*, uintxx index);


/* 
 * Inlines */

CTB_INLINE eintxx
dynarray_append(TDynArray* array, void* element)
{
	ASSERT(array);
	return dynarray_insert(array, element, array->used);
}

CTB_INLINE eintxx
dynarray_pop(TDynArray* array)
{
	ASSERT(array);

	if (array->used) {
		return dynarray_remove(array, array->used - 1);
	}
	return CTB_ENKEY;
}

CTB_INLINE void*
dynarray_at(TDynArray* array, uintxx index)
{
	ASSERT(array && dynarray_checkrange(array, index));
	
	return (void*) DYNARRAY_AT(array, index);
}

CTB_INLINE void*
dynarray_safe_at(TDynArray* array, uintxx index)
{
	ASSERT(array);

	if (!dynarray_checkrange(array, index)) {
		return NULL;
	}
	
	return (void*) DYNARRAY_AT(array, index);
}

CTB_INLINE void*
dynarray_next(TDynArray* array, TIterator* it)
{
	void* tmp;
	ASSERT(array && it);
	
	if ((tmp = dynarray_safe_at(array, it->index1))) {
		it->index1++;
		return tmp;
	}
	iterator_reset(it);
	return NULL;
}

CTB_INLINE eintxx
dynarray_set(TDynArray* array, uintxx index, void* element)
{
	void* p;
	ASSERT(array);
	
	if ((p = dynarray_safe_at(array, index))) {
		if (p != element)
			memcpy(p, element, array->datasize);
		
		return CTB_OK;
	}
	return dynarray_insert(array, element, index);
}

CTB_INLINE uintxx
dynarray_size(TDynArray* array)
{
	ASSERT(array);
	return array->used;
}

CTB_INLINE uintxx
dynarray_capacity(TDynArray* array)
{
	ASSERT(array);
	return array->capacity;
}

CTB_INLINE bool
dynarray_checkrange(TDynArray* array, uintxx index)
{
	ASSERT(array);
	return (index < array->used);
}


#endif
