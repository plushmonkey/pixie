#include "pxe_buffer.h"

#include "pxe_alloc.h"
#include "pxe_varint.h"

#include <stdlib.h>
#include <string.h>

// TODO: Endianness

inline bool32 pxe_buffer_writer_alloc(pxe_buffer_writer* writer) {
  pxe_buffer_chain* new_chain = pxe_pool_alloc(writer->pool);

  if (new_chain == NULL) {
    return 0;
  }

  new_chain->next = NULL;

  if (writer->head == NULL) {
    writer->head = new_chain;
    writer->current = new_chain;
    writer->last = new_chain;
  } else {
    writer->last->next = new_chain;
    writer->last = new_chain;
  }

  return 1;
}

inline size_t pxe_buffer_allocated_size(pxe_buffer_chain* chain) {
  if (chain == NULL) return 0;

  size_t size = 0;

  do {
    size += chain->buffer->max_size;

    chain = chain->next;
  } while (chain);

  return size;
}

// Reserves space in the buffer writer by allocating if the size would go over
// the currently allocated space.
inline bool32 pxe_buffer_writer_reserve(pxe_buffer_writer* writer,
                                        size_t size) {
#if 0
  if (writer->head == NULL || (writer->relative_write_pos + size > writer->pool->element_size)) {
#else
  while (writer->head == NULL || pxe_buffer_allocated_size(writer->current) -
                                         writer->relative_write_pos <
                                     size) {
#endif
  if (!pxe_buffer_writer_alloc(writer)) {
    return 0;
  }
}

return 1;
}

inline size_t pxe_buffer_writer_remaining(pxe_buffer_writer* writer) {
  return writer->pool->element_size - writer->relative_write_pos;
}

// Returns 1 if the current buffer chain has enough room for 'size'.
inline bool32 pxe_buffer_writer_available(pxe_buffer_writer* writer,
                                          size_t size) {
  return pxe_buffer_writer_remaining(writer) >= size;
}

size_t pxe_buffer_size(pxe_buffer_chain* chain) {
  if (chain == NULL) return 0;

  size_t size = 0;

  do {
    size += chain->buffer->size;

    chain = chain->next;
  } while (chain);

  return size;
}

// Returns the current buffer_chain that falls in the reader's read_pos and its
// base position.
bool32 pxe_buffer_get_pos_and_chain(pxe_buffer_reader* reader,
                                    pxe_buffer_chain** base, size_t* base_pos) {
  pxe_buffer_chain* current = reader->chain;
  pxe_buffer_chain* prev = current;

  if (current == NULL) return 0;

  size_t pos = 0;

  do {
    pos += current->buffer->size;
    prev = current;
    current = current->next;
  } while (pos <= reader->read_pos && current);

  if (prev == NULL || (current == NULL && pos <= reader->read_pos)) {
    return 0;
  }

  *base = prev;
  *base_pos = pos - prev->buffer->size;

  return 1;
}

bool32 pxe_buffer_read_u8(pxe_buffer_reader* reader, u8* out) {
  pxe_buffer_chain* current = NULL;
  size_t base_pos = 0;

  if (pxe_buffer_get_pos_and_chain(reader, &current, &base_pos) == 0) {
    return 0;
  }

  size_t read_index = reader->read_pos - base_pos;

  *out = current->buffer->data[read_index];

  reader->read_pos++;

  return 1;
}

bool32 pxe_buffer_read_u16(pxe_buffer_reader* reader, u16* out) {
  pxe_buffer_chain* current = NULL;
  size_t base_pos = 0;

  if (pxe_buffer_get_pos_and_chain(reader, &current, &base_pos) == 0) {
    return 0;
  }

  size_t read_index = reader->read_pos - base_pos;

  u16 data = 0;

  // This lies right on the boundary of the two chains
  if (read_index + sizeof(u16) > current->buffer->size) {
    if (current->next == NULL) {
      // There's not enough data to read the u16 if there's no next chain.
      return 0;
    }

    char buf[4] = {0};

    size_t first_len = current->buffer->size - read_index;
    size_t second_len = sizeof(data) - first_len;

    for (size_t i = 0; i < first_len; ++i) {
      buf[i] = (char)current->buffer->data[read_index + i];
    }

    current = current->next;

    for (size_t i = 0; i < second_len; ++i) {
      buf[i + first_len] = (char)current->buffer->data[i];
    }

    data = *(u16*)buf;
  } else {
    data = *(u16*)&current->buffer->data[read_index];
  }

  *out = bswap_16(data);

  reader->read_pos += sizeof(u16);

  return 1;
}

bool32 pxe_buffer_read_u32(pxe_buffer_reader* reader, u32* out) {
  pxe_buffer_chain* current = NULL;
  size_t base_pos = 0;

  if (pxe_buffer_get_pos_and_chain(reader, &current, &base_pos) == 0) {
    return 0;
  }

  size_t read_index = reader->read_pos - base_pos;

  u32 data = 0;

  // This lies right on the boundary of the two chains
  if (read_index + sizeof(u32) > current->buffer->size) {
    if (current->next == NULL) {
      // There's not enough data to read the u32 if there's no next chain.
      return 0;
    }

    char buf[4] = {0};

    size_t first_len = current->buffer->size - read_index;
    size_t second_len = pxe_array_size(buf) - first_len;

    for (size_t i = 0; i < first_len; ++i) {
      buf[i] = (char)current->buffer->data[read_index + i];
    }

    current = current->next;

    for (size_t i = 0; i < second_len; ++i) {
      buf[i + first_len] = (char)current->buffer->data[i];
    }

    data = *(u32*)buf;
  } else {
    data = *(u32*)&current->buffer->data[read_index];
  }

  *out = bswap_32(data);

  reader->read_pos += sizeof(u32);

  return 1;
}

bool32 pxe_buffer_read_u64(pxe_buffer_reader* reader, u64* out) {
  pxe_buffer_chain* current = NULL;
  size_t base_pos = 0;

  if (pxe_buffer_get_pos_and_chain(reader, &current, &base_pos) == 0) {
    return 0;
  }

  size_t read_index = reader->read_pos - base_pos;

  u64 data = 0;

  // This lies right on the boundary of the two chains
  if (read_index + sizeof(u64) > current->buffer->size) {
    if (current->next == NULL) {
      // There's not enough data to read the u64 if there's no next chain.
      return 0;
    }

    char buf[8] = {0};

    size_t first_len = current->buffer->size - read_index;
    size_t second_len = pxe_array_size(buf) - first_len;

    for (size_t i = 0; i < first_len; ++i) {
      buf[i] = (char)current->buffer->data[read_index + i];
    }

    current = current->next;

    for (size_t i = 0; i < second_len; ++i) {
      buf[i + first_len] = (char)current->buffer->data[i];
    }

    data = *(u64*)buf;
  } else {
    data = *(u64*)&current->buffer->data[read_index];
  }

  *out = bswap_64(data);

  reader->read_pos += sizeof(u64);

  return 1;
}

bool32 pxe_buffer_read_varint(pxe_buffer_reader* reader, i32* value) {
  pxe_buffer_chain* current = NULL;
  size_t base_pos = 0;

  if (pxe_buffer_get_pos_and_chain(reader, &current, &base_pos) == 0) {
    return 0;
  }

  size_t read_index = reader->read_pos - base_pos;
  int shift = 0;
  size_t i = 0;

  *value = 0;

  do {
    if (read_index >= current->buffer->size) {
      if (current->next == NULL) {
        // The buffer doesn't have enough data to fully read this VarInt.
        *value = 0;
        return 0;
      }

      read_index = 0;
      current = current->next;
    }

    *value |= (i32)(current->buffer->data[read_index] & 0x7F) << shift;
    shift += 7;
    ++i;
  } while ((current->buffer->data[read_index++] & 0x80) != 0);

  reader->read_pos += i;

  return 1;
}

bool32 pxe_buffer_read_varlong(pxe_buffer_reader* reader, i64* value) {
  pxe_buffer_chain* current = NULL;
  size_t base_pos = 0;

  if (pxe_buffer_get_pos_and_chain(reader, &current, &base_pos) == 0) {
    return 0;
  }

  size_t read_index = reader->read_pos - base_pos;
  int shift = 0;
  size_t i = 0;

  *value = 0;

  do {
    if (read_index + i >= current->buffer->size) {
      if (current->next == NULL) {
        // The buffer doesn't have enough data to fully read this VarInt.
        *value = 0;
        return 0;
      }

      read_index = 0;
      current = current->next;
    }

    *value |= (i64)(current->buffer->data[read_index] & 0x7F) << shift;
    shift += 7;
    ++i;
  } while ((current->buffer->data[read_index++] & 0x80) != 0);

  reader->read_pos += i;

  return 1;
}

bool32 pxe_buffer_read_float(pxe_buffer_reader* reader, float* out) {
  pxe_buffer_chain* current = NULL;
  size_t base_pos = 0;

  if (pxe_buffer_get_pos_and_chain(reader, &current, &base_pos) == 0) {
    return 0;
  }

  size_t read_index = reader->read_pos - base_pos;

  float data = 0.0f;

  // This lies right on the boundary of the two chains
  if (read_index + sizeof(float) > current->buffer->size) {
    if (current->next == NULL) {
      // There's not enough data to read the float if there's no next chain.
      return 0;
    }

    char buf[4] = {0};

    size_t first_len = current->buffer->size - read_index;
    size_t second_len = pxe_array_size(buf) - first_len;

    for (size_t i = 0; i < first_len; ++i) {
      buf[i] = (char)current->buffer->data[read_index + i];
    }

    current = current->next;

    for (size_t i = 0; i < second_len; ++i) {
      buf[i + first_len] = (char)current->buffer->data[i];
    }

    data = *(float*)buf;
  } else {
    data = *(float*)&current->buffer->data[read_index];
  }

  u32 int_rep = *(u32*)&data;
  int_rep = bswap_32(int_rep);
  data = *(float*)&int_rep;

  *out = data;

  reader->read_pos += sizeof(float);

  return 1;
}

bool32 pxe_buffer_read_double(pxe_buffer_reader* reader, double* out) {
  pxe_buffer_chain* current = NULL;
  size_t base_pos = 0;

  if (pxe_buffer_get_pos_and_chain(reader, &current, &base_pos) == 0) {
    return 0;
  }

  size_t read_index = reader->read_pos - base_pos;

  double data = 0.0;

  // This lies right on the boundary of the two chains
  if (read_index + sizeof(double) > current->buffer->size) {
    if (current->next == NULL) {
      // There's not enough data to read the double if there's no next chain.
      return 0;
    }

    char buf[8] = {0};

    size_t first_len = current->buffer->size - read_index;
    size_t second_len = pxe_array_size(buf) - first_len;

    for (size_t i = 0; i < first_len; ++i) {
      buf[i] = (char)current->buffer->data[read_index + i];
    }

    current = current->next;

    for (size_t i = 0; i < second_len; ++i) {
      buf[i + first_len] = (char)current->buffer->data[i];
    }

    data = *(double*)buf;
  } else {
    data = *(double*)&current->buffer->data[read_index];
  }

  u64 int_rep = *(u64*)&data;
  int_rep = bswap_64(int_rep);
  data = *(double*)&int_rep;

  *out = data;

  reader->read_pos += sizeof(double);

  return 1;
}

bool32 pxe_buffer_read_length_string(pxe_buffer_reader* reader, char* out,
                                     size_t* size) {
  i32 str_len;

  size_t pos_snapshot = reader->read_pos;

  if (pxe_buffer_read_varint(reader, &str_len) == 0) {
    return 0;
  }

  if (out == NULL) {
    reader->read_pos = pos_snapshot;
    *size = str_len;
    return 1;
  }

  pxe_buffer_chain* current = NULL;
  size_t base_pos = 0;

  if (pxe_buffer_get_pos_and_chain(reader, &current, &base_pos) == 0) {
    reader->read_pos = pos_snapshot;
    return 0;
  }

  size_t read_index = reader->read_pos - base_pos;

  for (size_t i = 0; i < (size_t)str_len;) {
    out[i] = (char)current->buffer->data[read_index];

    ++read_index;

    if (read_index > current->buffer->size) {
      if (current->next == NULL) {
        reader->read_pos = pos_snapshot;
        return 0;
      }

      current = current->next;
      read_index = 0;
      continue;
    }

    ++i;
  }

  reader->read_pos += str_len;

  if (size) {
    *size = str_len;
  }

  return 1;
}

bool32 pxe_buffer_read_raw_string(pxe_buffer_reader* reader, char* out,
                                  size_t size) {
  pxe_buffer_chain* current = NULL;
  size_t base_pos = 0;

  if (pxe_buffer_get_pos_and_chain(reader, &current, &base_pos) == 0) {
    return 0;
  }

  size_t read_index = reader->read_pos - base_pos;

  for (size_t i = 0; i < (size_t)size;) {
    out[i] = (char)current->buffer->data[read_index];

    ++read_index;

    if (read_index > current->buffer->size) {
      if (current->next == NULL) {
        return 0;
      }

      current = current->next;
      read_index = 0;
      continue;
    }

    ++i;
  }

  reader->read_pos += size;

  return 1;
}

bool32 pxe_buffer_write_u8(pxe_buffer_writer* writer, u8 data) {
  if (!pxe_buffer_writer_reserve(writer, sizeof(data))) return 0;

  pxe_buffer* buffer = writer->current->buffer;

  buffer->data[writer->relative_write_pos++] = data;
  buffer->size++;

  return 1;
}

bool32 pxe_buffer_write_u16(pxe_buffer_writer* writer, u16 data) {
  size_t size = sizeof(data);

  if (!pxe_buffer_writer_reserve(writer, size)) return 0;

  data = bswap_16(data);

  if (pxe_buffer_writer_available(writer, size)) {
    pxe_buffer* buffer = writer->current->buffer;

    *(u16*)(buffer->data + writer->relative_write_pos) = data;

    writer->relative_write_pos += size;
    buffer->size += size;
  } else {
    size_t first_len = pxe_buffer_writer_remaining(writer);
    size_t second_len = size - first_len;

    for (size_t i = 0; i < first_len; ++i) {
      writer->current->buffer->data[writer->relative_write_pos++] =
          ((u8*)&data)[i];
    }

    writer->current->buffer->size += first_len;

    writer->current = writer->current->next;
    writer->relative_write_pos = 0;

    for (size_t i = 0; i < second_len; ++i) {
      writer->current->buffer->data[writer->relative_write_pos++] =
          ((u8*)&data)[i + first_len];
    }

    writer->current->buffer->size += second_len;
  }

  return 1;
}

bool32 pxe_buffer_write_u32(pxe_buffer_writer* writer, u32 data) {
  size_t size = sizeof(data);

  if (!pxe_buffer_writer_reserve(writer, size)) return 0;

  data = bswap_32(data);

  if (pxe_buffer_writer_available(writer, size)) {
    pxe_buffer* buffer = writer->current->buffer;

    *(u32*)(buffer->data + writer->relative_write_pos) = data;

    writer->relative_write_pos += size;
    buffer->size += size;
  } else {
    size_t first_len = pxe_buffer_writer_remaining(writer);
    size_t second_len = size - first_len;

    for (size_t i = 0; i < first_len; ++i) {
      writer->current->buffer->data[writer->relative_write_pos++] =
          ((u8*)&data)[i];
    }

    writer->current->buffer->size += first_len;
    writer->current = writer->current->next;
    writer->relative_write_pos = 0;

    for (size_t i = 0; i < second_len; ++i) {
      writer->current->buffer->data[writer->relative_write_pos++] =
          ((u8*)&data)[i + first_len];
    }

    writer->current->buffer->size += second_len;
  }

  return 1;
}

bool32 pxe_buffer_write_u64(pxe_buffer_writer* writer, u64 data) {
  size_t size = sizeof(data);

  if (!pxe_buffer_writer_reserve(writer, size)) return 0;

  data = bswap_64(data);

  if (pxe_buffer_writer_available(writer, size)) {
    pxe_buffer* buffer = writer->current->buffer;

    *(u64*)(buffer->data + writer->relative_write_pos) = data;

    writer->relative_write_pos += size;
    buffer->size += size;
  } else {
    size_t first_len = pxe_buffer_writer_remaining(writer);
    size_t second_len = size - first_len;

    for (size_t i = 0; i < first_len; ++i) {
      writer->current->buffer->data[writer->relative_write_pos++] =
          ((u8*)&data)[i];
    }

    writer->current->buffer->size += first_len;
    writer->current = writer->current->next;
    writer->relative_write_pos = 0;

    for (size_t i = 0; i < second_len; ++i) {
      writer->current->buffer->data[writer->relative_write_pos++] =
          ((u8*)&data)[i + first_len];
    }

    writer->current->buffer->size += second_len;
  }

  return 1;
}

bool32 pxe_buffer_write_float(pxe_buffer_writer* writer, float data) {
  size_t size = sizeof(data);

  if (!pxe_buffer_writer_reserve(writer, size)) return 0;

  u32 int_rep = *(u32*)&data;
  int_rep = bswap_32(int_rep);
  data = *(float*)&int_rep;

  if (pxe_buffer_writer_available(writer, size)) {
    pxe_buffer* buffer = writer->current->buffer;

    *(float*)(buffer->data + writer->relative_write_pos) = data;

    writer->relative_write_pos += size;
    buffer->size += size;
  } else {
    size_t first_len = pxe_buffer_writer_remaining(writer);
    size_t second_len = size - first_len;

    for (size_t i = 0; i < first_len; ++i) {
      writer->current->buffer->data[writer->relative_write_pos++] =
          ((u8*)&data)[i];
    }

    writer->current->buffer->size += first_len;
    writer->current = writer->current->next;
    writer->relative_write_pos = 0;

    for (size_t i = 0; i < second_len; ++i) {
      writer->current->buffer->data[writer->relative_write_pos++] =
          ((u8*)&data)[i + first_len];
    }

    writer->current->buffer->size += second_len;
  }

  return 1;
}

bool32 pxe_buffer_write_double(pxe_buffer_writer* writer, double data) {
  size_t size = sizeof(data);

  if (!pxe_buffer_writer_reserve(writer, size)) return 0;

  u64 int_rep = *(u64*)&data;
  int_rep = bswap_64(int_rep);
  data = *(double*)&int_rep;

  if (pxe_buffer_writer_available(writer, size)) {
    pxe_buffer* buffer = writer->current->buffer;

    *(double*)(buffer->data + writer->relative_write_pos) = data;

    writer->relative_write_pos += size;
    buffer->size += size;
  } else {
    size_t first_len = pxe_buffer_writer_remaining(writer);
    size_t second_len = size - first_len;

    for (size_t i = 0; i < first_len; ++i) {
      writer->current->buffer->data[writer->relative_write_pos++] =
          ((u8*)&data)[i];
    }

    writer->current->buffer->size += first_len;
    writer->current = writer->current->next;
    writer->relative_write_pos = 0;

    for (size_t i = 0; i < second_len; ++i) {
      writer->current->buffer->data[writer->relative_write_pos++] =
          ((u8*)&data)[i + first_len];
    }

    writer->current->buffer->size += second_len;
  }

  return 1;
}

bool32 pxe_buffer_write_length_string(pxe_buffer_writer* writer,
                                      const char* data, size_t length) {
  if (!pxe_buffer_write_varint(writer, (i32)length)) {
    return 0;
  }

  return pxe_buffer_write_raw_string(writer, data, length);
}

bool32 pxe_buffer_write_raw_string(pxe_buffer_writer* writer, const char* data,
                                   size_t length) {
  if (!pxe_buffer_writer_reserve(writer, length)) return 0;

  for (size_t i = 0; i < length; ++i) {
    if (!pxe_buffer_writer_available(writer, 1)) {
      writer->current = writer->current->next;
      writer->relative_write_pos = 0;
    }

    writer->current->buffer->data[writer->relative_write_pos++] = data[i];
    ++writer->current->buffer->size;
  }

  return 1;
}

bool32 pxe_buffer_write_varint(pxe_buffer_writer* writer, i32 data) {
  size_t size = pxe_varint_size(data);
  char buf[5];

  pxe_varint_write(data, buf);

  return pxe_buffer_write_raw_string(writer, buf, size);
}

bool32 pxe_buffer_write_varlong(pxe_buffer_writer* writer, i64 data) {
  size_t size = pxe_varlong_size(data);
  char buf[10];

  pxe_varlong_write(data, buf);

  return pxe_buffer_write_raw_string(writer, buf, size);
}

pxe_buffer_writer pxe_buffer_writer_create(pxe_pool* pool) {
  pxe_buffer_writer writer;

  writer.pool = pool;
  writer.current = NULL;
  writer.head = NULL;
  writer.last = NULL;
  writer.relative_write_pos = 0;

  return writer;
}
