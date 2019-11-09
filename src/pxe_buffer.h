#ifndef PIXIE_BUFFER_H_
#define PIXIE_BUFFER_H_

#include "pixie.h"
#include "pxe_alloc.h"
#include "pxe_varint.h"

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

typedef struct pxe_buffer_writer {
  size_t write_pos;
  pxe_buffer* buffer;
} pxe_buffer_writer;

pxe_buffer_chain* pxe_chain_insert(struct pxe_memory_arena* arena,
                                   pxe_buffer_chain* chain, u8* data,
                                   size_t size);

size_t pxe_buffer_chain_size(pxe_buffer_chain* chain);

bool32 pxe_buffer_chain_read_u8(pxe_buffer_chain_reader* reader, u8* out);
bool32 pxe_buffer_chain_read_u16(pxe_buffer_chain_reader* reader, u16* out);
bool32 pxe_buffer_chain_read_u32(pxe_buffer_chain_reader* reader, u32* out);
bool32 pxe_buffer_chain_read_u64(pxe_buffer_chain_reader* reader, u64* out);
bool32 pxe_buffer_chain_read_varint(pxe_buffer_chain_reader* reader, i32* out);
bool32 pxe_buffer_chain_read_varlong(pxe_buffer_chain_reader* reader, i64* out);
bool32 pxe_buffer_chain_read_float(pxe_buffer_chain_reader* reader, float* out);
bool32 pxe_buffer_chain_read_double(pxe_buffer_chain_reader* reader,
                                    double* out);

// This will set size only without moving the read_pos if out is NULL.
bool32 pxe_buffer_chain_read_length_string(pxe_buffer_chain_reader* reader,
                                           char* out, size_t* size);
bool32 pxe_buffer_chain_read_raw_string(pxe_buffer_chain_reader* reader,
                                        char* out, size_t size);

bool32 pxe_buffer_write_u8(pxe_buffer_writer* writer, u8 data);
bool32 pxe_buffer_write_u16(pxe_buffer_writer* writer, u16 data);
bool32 pxe_buffer_write_u32(pxe_buffer_writer* writer, u32 data);
bool32 pxe_buffer_write_u64(pxe_buffer_writer* writer, u64 data);
bool32 pxe_buffer_write_varint(pxe_buffer_writer* writer, i32 data);
bool32 pxe_buffer_write_varlong(pxe_buffer_writer* writer, i64 data);
bool32 pxe_buffer_write_float(pxe_buffer_writer* writer, float data);
bool32 pxe_buffer_write_double(pxe_buffer_writer* writer, double data);
bool32 pxe_buffer_write_length_string(pxe_buffer_writer* writer,
                                      const char* data, size_t length);
bool32 pxe_buffer_write_raw_string(pxe_buffer_writer* writer, const char* data,
                                   size_t length);

inline bool32 pxe_buffer_push_u8(pxe_buffer_writer* writer, u8 data,
                                 struct pxe_memory_arena* arena) {
  pxe_arena_alloc_unaligned(arena, sizeof(data));
  writer->buffer->size += sizeof(data);

  return pxe_buffer_write_u8(writer, data);
}

inline bool32 pxe_buffer_push_u16(pxe_buffer_writer* writer, u16 data,
                                  struct pxe_memory_arena* arena) {
  pxe_arena_alloc_unaligned(arena, sizeof(data));
  writer->buffer->size += sizeof(data);

  return pxe_buffer_write_u16(writer, data);
}

inline bool32 pxe_buffer_push_u32(pxe_buffer_writer* writer, u32 data,
                                  struct pxe_memory_arena* arena) {
  pxe_arena_alloc_unaligned(arena, sizeof(data));
  writer->buffer->size += sizeof(data);

  return pxe_buffer_write_u32(writer, data);
}

inline bool32 pxe_buffer_push_u64(pxe_buffer_writer* writer, u64 data,
                                  struct pxe_memory_arena* arena) {
  pxe_arena_alloc_unaligned(arena, sizeof(data));
  writer->buffer->size += sizeof(data);

  return pxe_buffer_write_u64(writer, data);
}

inline bool32 pxe_buffer_push_varint(pxe_buffer_writer* writer, i32 data,
                                     struct pxe_memory_arena* arena) {
  pxe_arena_alloc_unaligned(arena, pxe_varint_size(data));
  writer->buffer->size += pxe_varint_size(data);

  return pxe_buffer_write_varint(writer, data);
}

inline bool32 pxe_buffer_push_varlong(pxe_buffer_writer* writer, i64 data,
                                      struct pxe_memory_arena* arena) {
  pxe_arena_alloc_unaligned(arena, pxe_varlong_size(data));
  writer->buffer->size += pxe_varlong_size(data);

  return pxe_buffer_write_varlong(writer, data);
}

inline bool32 pxe_buffer_push_float(pxe_buffer_writer* writer, float data,
                                    struct pxe_memory_arena* arena) {
  pxe_arena_alloc_unaligned(arena, sizeof(data));
  writer->buffer->size += sizeof(data);

  return pxe_buffer_write_float(writer, data);
}

inline bool32 pxe_buffer_push_double(pxe_buffer_writer* writer, double data,
                                     struct pxe_memory_arena* arena) {
  pxe_arena_alloc_unaligned(arena, sizeof(data));
  writer->buffer->size += sizeof(data);

  return pxe_buffer_write_double(writer, data);
}

inline bool32 pxe_buffer_push_length_string(pxe_buffer_writer* writer,
                                            const char* data, size_t length,
                                            struct pxe_memory_arena* arena) {
  pxe_arena_alloc_unaligned(arena, pxe_varint_size((i32)length) + length);
  writer->buffer->size += pxe_varint_size((i32)length) + length;

  return pxe_buffer_write_length_string(writer, data, length);
}

inline bool32 pxe_buffer_push_raw_string(pxe_buffer_writer* writer,
                                         const char* data, size_t length,
                                         struct pxe_memory_arena* arena) {
  pxe_arena_alloc_unaligned(arena, length);
  writer->buffer->size += length;

  return pxe_buffer_write_raw_string(writer, data, length);
}

pxe_buffer_writer pxe_buffer_writer_create(struct pxe_memory_arena* arena,
                                           size_t size);

#endif
