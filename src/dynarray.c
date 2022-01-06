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

#include "../dynarray.h"


TDynArray*
dynarray_create(uintxx capacity, uintxx datasize)
{
	struct TDynArray* array;
	
	if (datasize == 0) {
		return NULL;
	}
	
	array = CTB_MALLOC(sizeof(struct TDynArray));
	if (array == NULL) {
		return NULL;
	}
	
	if (DYNARRAY_MINSIZE > capacity)
		capacity = DYNARRAY_MINSIZE;
	array->capacity = capacity;
	array->datasize = datasize;
	array->used = 0;
	
	array->buffer = CTB_MALLOC(capacity * datasize);
	if (array->buffer == NULL) {
		CTB_FREE(array);
		return NULL;
	}
	
	return array;
}

void
dynarray_destroy(TDynArray* array)
{
	if (array == NULL) {
		return;
	}
	
	CTB_FREE(array->buffer);
	CTB_FREE(array);
}


#define AS_POINTER(A) ((void*) DYNARRAY_AT(array, (A)))

eintxx
dynarray_insert(TDynArray* array, void* element, uintxx index)
{
	uintxx i;
	ASSERT(array);
	
	if (index < array->used) {
		if (array->used >= array->capacity) {
			if (dynarray_reserve(array, array->used + 1)) {
				return CTB_EOOM;
			}
		}
		array->used++;
		for (i = array->used; index < i; i--) {
			memcpy(AS_POINTER(i), AS_POINTER(i - 1), array->datasize);
		}
	}
	else {
		if (index >= array->capacity) {
			if (dynarray_reserve(array, index + 1)) {
				return CTB_EOOM;
			}
		}
		for (i = array->used; i < index; i++) {
			memset(AS_POINTER(i), 0, array->datasize);
		}
		array->used = index + 1;
	}
	memcpy(AS_POINTER(index), element, array->datasize);
	
	return CTB_OK;
}

eintxx
dynarray_remove(TDynArray* array, uintxx index)
{
	uintxx i;
	ASSERT(array && dynarray_checkrange(array, index));
	
	for (i = index + 1; i < array->used; i++) {
		memcpy(AS_POINTER(i - 1), AS_POINTER(i), array->datasize);
	}
	array->used--;
	
	return CTB_OK;
}

eintxx
dynarray_clear(TDynArray* array, TFreeFn freefn)
{
	ASSERT(array);
	
	if (freefn) {
		while (array->used) {
			array->used--;
			freefn(AS_POINTER(array->used));
		}
	}
	else {
		array->used = 0;
	}
	
	if (dynarray_shrink(array)) {
		return CTB_EOOM;
	}
	
	return CTB_OK;
}

static void
sortdynarray_r(TDynArray* array, uintxx a, uintxx b, TCmpFn cmpfn, void* s)
{
	uint8* p1;
	uint8* p2;
	uint8* pp;
	void*  sp;
	uintxx p, j, i;
	
	p  = a;
	j  = b;
	sp = s;
	pp = AS_POINTER(p);
	for (i = a;;) {
		while (cmpfn(pp, (p1 = AS_POINTER(i))) >= 0 && i < b)
			i++;
		while (cmpfn(pp, (p2 = AS_POINTER(j))) <  0 && j > a)
			j--;
		
		if (i >= j) {
			break;
		}
		memcpy(sp, p1, array->datasize);
		memcpy(p1, p2, array->datasize);
		memcpy(p2, sp, array->datasize);
	}
	p2 = AS_POINTER(j);
	memcpy(sp, p2, array->datasize);
	memcpy(p2, pp, array->datasize);
	memcpy(pp, sp, array->datasize);
	
	if (j > a)
		sortdynarray_r(array,     a, j - 1, cmpfn, s);
	if (j < b)
		sortdynarray_r(array, j + 1,     b, cmpfn, s);
}

#undef AS_POINTER


void
dynarray_sort(TDynArray* array, TCmpFn cmpfn, void* swapv)
{
	ASSERT(array);
	if (cmpfn && (array->used > 0))
		sortdynarray_r(array, 0, array->used - 1, cmpfn, swapv);
}

static eintxx
dynarray_resize(TDynArray* array, uintxx size)
{
	uint8* buffer;
	uintxx v;
	
	v = size;
	v--;
	v |= v >> 0x01;
	v |= v >> 0x02;
	v |= v >> 0x04;
	v |= v >> 0x08;
	v |= v >> 0x10;
#if defined(CTB_ENV64)
	v |= v >> 0x20;
#endif
	v++;
	
	if (v > array->capacity) {
		if (array->capacity > v - (array->capacity >> 1))
			v = v << 1;
		
		buffer = CTB_REALLOC(array->buffer, v * array->datasize);
		if (buffer == NULL) {
			return CTB_EOOM;
		}
		array->capacity = v;
		array->buffer   = buffer;
	}
	
	return CTB_OK;
}

eintxx
dynarray_reserve(TDynArray* array, uintxx size)
{
	ASSERT(array);
	
	if (size >= array->capacity) {
		if (dynarray_resize(array, size)) {
			return CTB_EOOM;
		}
	}
	return CTB_OK;
}

eintxx
dynarray_shrink(TDynArray* array)
{
	uint8* buffer;
	uintxx v;
	ASSERT(array);
	
	v = array->used;
	if (v < DYNARRAY_MINSIZE)
		v = DYNARRAY_MINSIZE;
	
	if (array->capacity > v) {
		buffer = CTB_REALLOC(array->buffer, v * array->datasize);
		if (buffer == NULL) {
			return CTB_EOOM;
		}
		
		array->capacity = v;
		array->buffer   = buffer;
	}
	return CTB_OK;
}
