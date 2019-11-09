#include "pxe_alloc.h"

#include <assert.h>

void pxe_arena_initialize(pxe_memory_arena* arena, void* memory,
                          size_t max_size) {
  arena->base = memory;
  arena->current = arena->base;
  arena->size = 0;
  arena->max_size = max_size;
}

void* pxe_arena_alloc(pxe_memory_arena* arena, size_t size) {
  size_t adj = sizeof(uintptr_t) - 1;

  void* result = (u8*)(((uintptr_t)arena->current + adj) & ~(uintptr_t)adj);

  arena->current = (u8*)result + size;
  arena->size = (uintptr_t)arena->current - (uintptr_t)arena->base;

  assert(arena->size <= arena->max_size);
  assert(((uintptr_t)result & (sizeof(uintptr_t) - 1)) == 0);

  return result;
}

void* pxe_arena_alloc_unaligned(pxe_memory_arena* arena, size_t size) {
  void* result = arena->current;

  assert(arena->size + size < arena->max_size);

  arena->current = (u8*)arena->current + size;
  arena->size += size;

  return result;
}

void pxe_arena_reset(pxe_memory_arena* arena) {
  arena->current = arena->base;
  arena->size = 0;
}
