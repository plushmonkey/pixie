#include "pxe_alloc.h"

void pxe_arena_initialize(pxe_memory_arena* arena, void* memory,
                          size_t max_size) {
  arena->base = memory;
  arena->current = arena->base;
  arena->size = 0;
  arena->max_size = max_size;
}

void* pxe_arena_alloc(pxe_memory_arena* arena, size_t size) {
  void* result = arena->current;

  arena->current = (u8*)arena->current + size;
  arena->size += size;

  return result;
}

void pxe_arena_reset(pxe_memory_arena* arena) { arena->current = arena->base; }
