#ifndef PIXIE_BUFFER_H_
#define PIXIE_BUFFER_H_

#include "pixie.h"
#include "pxe_alloc.h"
#include "pxe_varint.h"

struct pxe_memory_arena;

typedef struct pxe_buffer {
  u8* data;
  size_t size;
  size_t max_size;
} pxe_buffer;

typedef struct pxe_buffer_chain {
  pxe_buffer* buffer;
  struct pxe_buffer_chain* next;

  bool32 reference_counted;
  i32 reference_count;

  bool32 was_free;
} pxe_buffer_chain;

typedef struct pxe_buffer_reader {
  size_t read_pos;
  pxe_buffer_chain* chain;
} pxe_buffer_reader;

typedef struct pxe_buffer_writer {
  pxe_buffer_chain* head;
  pxe_buffer_chain* last;
  pxe_buffer_chain* current;
  size_t relative_write_pos;
  pxe_pool* pool;
} pxe_buffer_writer;

size_t pxe_buffer_size(pxe_buffer_chain* chain);

bool32 pxe_buffer_read_u8(pxe_buffer_reader* reader, u8* out);
bool32 pxe_buffer_read_u16(pxe_buffer_reader* reader, u16* out);
bool32 pxe_buffer_read_u32(pxe_buffer_reader* reader, u32* out);
bool32 pxe_buffer_read_u64(pxe_buffer_reader* reader, u64* out);
bool32 pxe_buffer_read_varint(pxe_buffer_reader* reader, i32* out);
bool32 pxe_buffer_read_varlong(pxe_buffer_reader* reader, i64* out);
bool32 pxe_buffer_read_float(pxe_buffer_reader* reader, float* out);
bool32 pxe_buffer_read_double(pxe_buffer_reader* reader, double* out);

// This will set size only without moving the read_pos if out is NULL.
bool32 pxe_buffer_read_length_string(pxe_buffer_reader* reader, char* out,
                                     size_t* size);
bool32 pxe_buffer_read_raw_string(pxe_buffer_reader* reader, char* out,
                                  size_t size);

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

pxe_buffer_writer pxe_buffer_writer_create(pxe_pool* pool);

inline size_t pxe_buffer_chain_count(pxe_buffer_chain* chain) {
  size_t count = 0;

  while (chain) {
    ++count;
    chain = chain->next;
  }

  return count;
}

#endif
