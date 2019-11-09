#ifndef PIXIE_UUID_H_
#define PIXIE_UUID_H_

#include "pixie.h"

#include "pxe_buffer.h"

typedef struct pxe_uuid {
  u64 most_significant;
  u64 least_significant;
} pxe_uuid;

pxe_uuid pxe_uuid_create(u64 most, u64 least);
pxe_uuid pxe_uuid_create_from_string(const char* str, bool32 dashes);

// TODO: profile this because it's probably really slow and could be improved
// significantly if it's a problem.
void pxe_uuid_to_string(pxe_uuid* uuid, char* str, bool32 dashes);

struct pxe_buffer_writer;
bool32 pxe_buffer_write_uuid(struct pxe_buffer_writer* writer, pxe_uuid* uuid);

inline bool32 pxe_buffer_push_uuid(pxe_buffer_writer* writer, pxe_uuid* uuid,
                                   struct pxe_memory_arena* arena) {
  pxe_arena_alloc_unaligned(arena, sizeof(u64) + sizeof(u64));
  writer->buffer->size += sizeof(u64) + sizeof(u64);

  return pxe_buffer_write_uuid(writer, uuid);
}

#endif
