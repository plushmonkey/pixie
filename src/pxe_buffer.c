#include "pxe_buffer.h"

#include "pxe_alloc.h"
#include "pxe_varint.h"

#include <stdlib.h>

#include <Windows.h>
// TODO: Endianness

#ifdef _MSC_VER
#define bswap_16(x) _byteswap_ushort(x)
#define bswap_32(x) _byteswap_ulong(x)
#define bswap_64(x) _byteswap_uint64(x)
#else
#define bswap_16(x) ((((x)&0xFF) << 8) | (((x)&0xFF00) >> 8))
#endif

pxe_buffer_chain* pxe_chain_insert(pxe_memory_arena* arena,
                                   pxe_buffer_chain* chain, u8* data,
                                   size_t size) {
  pxe_buffer* buffer = pxe_arena_push_type(arena, pxe_buffer);
  pxe_buffer_chain* new_chain = pxe_arena_push_type(arena, pxe_buffer_chain);

  buffer->data = data;
  buffer->size = size;

  new_chain->buffer = buffer;
  new_chain->next = chain;

  return new_chain;
}

size_t pxe_chain_size(pxe_buffer_chain* chain) {
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
bool32 pxe_buffer_get_pos_and_chain(pxe_buffer_chain_reader* reader,
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

bool32 pxe_buffer_chain_read_u8(pxe_buffer_chain_reader* reader, u8* out) {
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

bool32 pxe_buffer_chain_read_u16(pxe_buffer_chain_reader* reader, u16* out) {
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

    char buf[2] = {0};

    size_t first_len = current->buffer->size - read_index;
    size_t second_len = array_size(buf) - first_len;

    for (size_t i = 0; i < first_len; ++i) {
      buf[i] = (char)current->buffer->data[read_index + i];
    }

    current = current->next;

    for (size_t i = 0; i < second_len; ++i) {
      buf[i] = (char)current->buffer->data[i];
    }

    data = *(u16*)buf;
  } else {
    data = *(u16*)&current->buffer->data[read_index];
  }

  *out = bswap_16(data);

  reader->read_pos += sizeof(u16);

  return 1;
}

bool32 pxe_buffer_chain_read_u32(pxe_buffer_chain_reader* reader, u32* out) {
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
    size_t second_len = array_size(buf) - first_len;

    for (size_t i = 0; i < first_len; ++i) {
      buf[i] = (char)current->buffer->data[read_index + i];
    }

    current = current->next;

    for (size_t i = 0; i < second_len; ++i) {
      buf[i] = (char)current->buffer->data[i];
    }

    data = *(u32*)buf;
  } else {
    data = *(u32*)&current->buffer->data[read_index];
  }

  *out = bswap_32(data);

  reader->read_pos += sizeof(u32);

  return 1;
}

bool32 pxe_buffer_chain_read_u64(pxe_buffer_chain_reader* reader, u64* out) {
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
      // There's not enough data to read the u16 if there's no next chain.
      return 0;
    }

    char buf[8] = {0};

    size_t first_len = current->buffer->size - read_index;
    size_t second_len = array_size(buf) - first_len;

    for (size_t i = 0; i < first_len; ++i) {
      buf[i] = (char)current->buffer->data[read_index + i];
    }

    current = current->next;

    for (size_t i = 0; i < second_len; ++i) {
      buf[i] = (char)current->buffer->data[i];
    }

    data = *(u64*)buf;
  } else {
    data = *(u64*)&current->buffer->data[read_index];
  }

  *out = bswap_64(data);

  reader->read_pos += sizeof(u64);

  return 1;
}

bool32 pxe_buffer_chain_read_varint(pxe_buffer_chain_reader* reader,
                                    i64* value) {
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
      continue;
    }

    *value |= (i64)(current->buffer->data[read_index + i] & 0x7F) << shift;
    shift += 7;
  } while ((current->buffer->data[read_index + i++] & 0x80) != 0);

  reader->read_pos += i;

  return 1;
}

bool32 pxe_buffer_chain_read_length_string(pxe_buffer_chain_reader* reader,
                                           char* out, size_t* size) {
  i64 str_len;

  size_t pos_snapshot = reader->read_pos;

  if (pxe_buffer_chain_read_varint(reader, &str_len) == 0) {
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

  return 1;
}

bool32 pxe_buffer_chain_read_raw_string(pxe_buffer_chain_reader* reader,
                                        char* out, size_t size) {
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
  if (writer->write_pos + sizeof(data) > writer->buffer->size) return 0;

  writer->buffer->data[writer->write_pos++] = data;

  return 1;
}

bool32 pxe_buffer_write_u16(pxe_buffer_writer* writer, u16 data) {
  if (writer->write_pos + sizeof(data) > writer->buffer->size) return 0;

  *(u16*)(writer->buffer->data + writer->write_pos) = bswap_16(data);

  writer->write_pos += sizeof(u16);

  return 1;
}

bool32 pxe_buffer_write_u32(pxe_buffer_writer* writer, u32 data) {
  if (writer->write_pos + sizeof(data) > writer->buffer->size) return 0;

  *(u32*)(writer->buffer->data + writer->write_pos) = bswap_32(data);

  writer->write_pos += sizeof(u32);

  return 1;
}

bool32 pxe_buffer_write_u64(pxe_buffer_writer* writer, u64 data) {
  if (writer->write_pos + sizeof(data) > writer->buffer->size) return 0;

  *(u64*)(writer->buffer->data + writer->write_pos) = bswap_64(data);

  writer->write_pos += sizeof(u64);

  return 1;
}

bool32 pxe_buffer_write_varint(pxe_buffer_writer* writer, i64 data) {
  writer->write_pos += pxe_varint_write(data, (char*)writer->buffer->data);

  return 1;
}

bool32 pxe_buffer_write_length_string(pxe_buffer_writer* writer, char* data,
                                      size_t length) {
  if (writer->write_pos + pxe_varint_size(length) > writer->buffer->size)
    return 0;

  size_t length_size =
      pxe_varint_write(length, (char*)writer->buffer->data + writer->write_pos);

  writer->write_pos += length_size;

  if (writer->write_pos + length > writer->buffer->size) return 0;

  for (size_t i = 0; i < length; ++i) {
    writer->buffer->data[writer->write_pos + i] = data[i];
  }

  writer->write_pos += length;

  return 1;
}
