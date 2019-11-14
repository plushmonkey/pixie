#include "pxe_alloc.h"

#include <assert.h>
#include "pxe_buffer.h"

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

pxe_pool* pxe_pool_create(pxe_memory_arena* perm_arena, size_t element_size) {
  pxe_pool* pool = pxe_arena_push_type(perm_arena, pxe_pool);

  pool->arena = perm_arena;
  pool->element_size = element_size;
  pool->free = NULL;
  pool->allocated_count = 0;

  return pool;
}

struct pxe_buffer_chain* pxe_pool_alloc(pxe_pool* pool) {
  if (pool->free == NULL) {
    pxe_buffer_chain* chain =
        pxe_arena_push_type(pool->arena, pxe_buffer_chain);
    pxe_buffer* buffer = pxe_arena_push_type(pool->arena, pxe_buffer);
    u8* data = pxe_arena_alloc(pool->arena, pool->element_size);

    buffer->data = data;
    buffer->size = pool->element_size;

    chain->buffer = buffer;
    chain->next = NULL;

    ++pool->allocated_count;

    return chain;
  }

  pxe_buffer_chain* free = pool->free;

  pool->free = free->next;
  free->next = NULL;

  return free;
}

struct pxe_buffer_chain* pxe_pool_free(pxe_pool* pool,
                                       struct pxe_buffer_chain* chain,
                                       bool32 free_chain) {
  if (!free_chain) {
    pxe_buffer_chain* next = chain->next;

    chain->next = pool->free;
    pool->free = chain;

    return next;
  }

  while (chain) {
    pxe_buffer_chain* next = chain->next;

    chain->next = pool->free;
    pool->free = chain;

    chain = next;
  }

  return NULL;
}
