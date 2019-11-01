#ifndef PIXIE_BUFFER_H_
#define PIXIE_BUFFER_H_

#include "pixie.h"

struct pxe_memory_arena;

typedef struct pxe_buffer {
  u8* data;
  size_t size;
} pxe_buffer;

typedef struct pxe_buffer_chain {
  pxe_buffer* buffer;
  struct pxe_buffer_chain* next;
} pxe_buffer_chain;

typedef struct pxe_buffer_chain_reader {
  size_t read_pos;
  pxe_buffer_chain* chain;
} pxe_buffer_chain_reader;

pxe_buffer_chain* pxe_chain_insert(struct pxe_memory_arena* arena,
                                   pxe_buffer_chain* chain, u8* data,
                                   size_t size);

size_t pxe_chain_size(pxe_buffer_chain* chain);

bool32 pxe_buffer_chain_read_u8(pxe_buffer_chain_reader* reader, u8* out);
bool32 pxe_buffer_chain_read_u16(pxe_buffer_chain_reader* reader, u16* out);
bool32 pxe_buffer_chain_read_u32(pxe_buffer_chain_reader* reader, u32* out);
bool32 pxe_buffer_chain_read_u64(pxe_buffer_chain_reader* reader, u64* out);
bool32 pxe_buffer_chain_read_varint(pxe_buffer_chain_reader* reader, i64* out);
// This will set size only without moving the read_pos if out is NULL.
bool32 pxe_buffer_chain_read_length_string(pxe_buffer_chain_reader* reader,
                                           char* out, size_t* size);

#endif
