#ifndef PIXIE_ALLOC_H_
#define PIXIE_ALLOC_H_

#include "pixie.h"

typedef struct pxe_memory_arena {
  void* base;
  void* current;
  size_t size;
  size_t max_size;
} pxe_memory_arena;

void pxe_arena_initialize(pxe_memory_arena* arena, void* memory,
                          size_t max_size);
void* pxe_arena_alloc(pxe_memory_arena* arena, size_t size);
void pxe_arena_reset(pxe_memory_arena* arena);

#define pxe_arena_push_type(arena, type) \
  (type*)(pxe_arena_alloc(arena, sizeof(type)))

#define pxe_arena_push_type_count(arena, type, count) \
  (type*)(pxe_arena_alloc(arena, sizeof(type) * count))

#define pxe_kilobytes(n) ((n)*1024)
#define pxe_megabytes(n) ((n)*pxe_kilobytes(1024))
#define pxe_gigabytes(n) ((n)*pxe_megabytes(1024))

#endif
