#include "heap.h"

#include "debug.h"
#include "mutex.h"
#include "tlsf/tlsf.h"

#include <stddef.h>
#include <stdio.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct arena_t
{
	pool_t pool;
	struct arena_t* next;
} arena_t;

// Stores backtrace, address of memory, size of individual memory, ptr to next storage
/*typedef struct storage_t
{
	void** backtrace;
	void* address;
	size_t size;
	struct storage_t* s_next;
} storage_t;
*/

typedef struct heap_t
{
	tlsf_t tlsf;
	size_t grow_increment;
	arena_t* arena;
	/* struct storage_t* s_next; */ // ptr to first memory alloc data
	mutex_t* mutex;
} heap_t;


heap_t* heap_create(size_t grow_increment)
{
	heap_t* heap = VirtualAlloc(NULL, sizeof(heap_t) + tlsf_size(),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!heap)
	{
		debug_print(
			k_print_error,
			"OUT OF MEMORY!\n");
		return NULL;
	}

	heap->mutex = mutex_create();
	heap->grow_increment = grow_increment;
	heap->tlsf = tlsf_create(heap + 1);
	heap->arena = NULL;

	return heap;
}

void* heap_alloc(heap_t* heap, size_t size, size_t alignment)
{
	mutex_lock(heap->mutex);

	void* address = tlsf_memalign(heap->tlsf, alignment, size);
	if (!address)
	{
		size_t arena_size =
			__max(heap->grow_increment, size * 2) +
			sizeof(arena_t);
		arena_t* arena = VirtualAlloc(NULL,
			arena_size + tlsf_pool_overhead(),
			MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!arena)
		{
			debug_print(
				k_print_error,
				"OUT OF MEMORY!\n");
			return NULL;
		}

		arena->pool = tlsf_add_pool(heap->tlsf, arena + 1, arena_size);

		arena->next = heap->arena;
		heap->arena = arena;

		address = tlsf_memalign(heap->tlsf, alignment, size);
	}
	/* if (address) {
		// Adds data to the struct
		storage_t* storage = VirtualAlloc(NULL, sizeof(storage_t),
			MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		storage->address = address;
		// debug_backtrace(storage->backtrace, 8);
		storage->size = size;

		// Connects the ptr of last added memory data to current memory data
		if (heap->s_next) {
			storage_t* cur = heap->s_next;
			while (cur->s_next) {
				cur->s_next = cur->s_next;
			}
			cur->s_next = storage;
		}
		else {
			heap->s_next = storage;
		}
	}*/
	

	mutex_unlock(heap->mutex);

	return address;
}

void heap_free(heap_t* heap, void* address)
{
	mutex_lock(heap->mutex);
	tlsf_free(heap->tlsf, address);
	// finds which memory storage matches the address
	/*storage_t* prev = heap->s_next;
	storage_t* cur = heap->s_next;
	while (cur->address != address) {
		prev = cur;
		cur = cur->s_next;
	}

	// fixes ptrs
	if (cur->s_next == NULL) {
		if (prev == NULL) {
			heap->s_next = NULL;
		}
		else {
			prev->s_next = NULL;
		}
	}
	else {
		if (prev == NULL) {
			heap->s_next = cur->s_next;
		}
		else {
			prev->s_next = cur->s_next;
		}
	} */
	mutex_unlock(heap->mutex);
}

void heap_destroy(heap_t* heap)
{
	// if there is still unfreed memory
	/*
	if (heap->s_next) {
		storage_t* cur = heap->s_next;
		while (cur != NULL) {
			debug_print(
				k_print_info,
				"Memory leak of size %zu bytes with callstack:\n",
				cur->size,
				cur->backtrace);
			cur = cur->s_next;
		}
	}
	*/
	tlsf_destroy(heap->tlsf);

	arena_t* arena = heap->arena;
	while (arena)
	{
		arena_t* next = arena->next;
		VirtualFree(arena, 0, MEM_RELEASE);
		arena = next;
	}

	mutex_destroy(heap->mutex);

	VirtualFree(heap, 0, MEM_RELEASE);
}
