#include "pxe_nbt.h"

#include "pxe_alloc.h"
#include "pxe_buffer.h"

#include <stdio.h>

void pxe_nbt_tag_compound_add(pxe_nbt_tag_compound* compound, pxe_nbt_tag tag) {
  pxe_nbt_tag* new_tag = compound->tags + compound->ntags++;

  *new_tag = tag;
}

/////////////////////////// NBT parsing

bool32 pxe_nbt_parse_tag(pxe_buffer_chain_reader* reader, pxe_nbt_tag* tag,
                         pxe_memory_arena* arena);
bool32 pxe_nbt_write_named_tag(pxe_buffer_writer* writer, pxe_nbt_tag* tag,
                               pxe_memory_arena* arena);

bool32 pxe_nbt_read_length_string(pxe_buffer_chain_reader* reader, char** data,
                                  size_t* size, pxe_memory_arena* arena) {
  u16 length;

  if (pxe_buffer_chain_read_u16(reader, &length) == 0) {
    return 0;
  }

  *size = length;

  *data = pxe_arena_alloc(arena, *size);

  if (pxe_buffer_chain_read_raw_string(reader, *data, *size) == 0) {
    return 0;
  }

  return 1;
}

bool32 pxe_nbt_parse_compound(pxe_buffer_chain_reader* reader,
                              pxe_nbt_tag_compound* compound,
                              pxe_memory_arena* arena) {
  compound->ntags = 0;

  pxe_nbt_tag_type type = PXE_NBT_TAG_TYPE_UNKNOWN;

  while (type != PXE_NBT_TAG_TYPE_END) {
    if (pxe_buffer_chain_read_u8(reader, (u8*)&type) == 0) {
      return 0;
    }

    if (type == PXE_NBT_TAG_TYPE_END) {
      break;
    }

    pxe_nbt_tag* tag = compound->tags + compound->ntags++;

    tag->tag = NULL;
    tag->type = type;

    if (pxe_nbt_read_length_string(reader, &tag->name, &tag->name_length,
                                   arena) == 0) {
      return 0;
    }

    if (pxe_nbt_parse_tag(reader, tag, arena) == 0) {
      return 0;
    }
  }

  return 1;
}

bool32 pxe_nbt_parse_tag(pxe_buffer_chain_reader* reader, pxe_nbt_tag* tag,
                         pxe_memory_arena* arena) {
  switch (tag->type) {
    case PXE_NBT_TAG_TYPE_END: {
    } break;
    case PXE_NBT_TAG_TYPE_BYTE: {
      pxe_nbt_tag_byte* byte_tag = pxe_arena_push_type(arena, pxe_nbt_tag_byte);

      if (pxe_buffer_chain_read_u8(reader, &byte_tag->data) == 0) {
        return 0;
      }

      tag->tag = byte_tag;
    } break;
    case PXE_NBT_TAG_TYPE_SHORT: {
      pxe_nbt_tag_short* short_tag =
          pxe_arena_push_type(arena, pxe_nbt_tag_short);

      if (pxe_buffer_chain_read_u16(reader, &short_tag->data) == 0) {
        return 0;
      }

      tag->tag = short_tag;
    } break;
    case PXE_NBT_TAG_TYPE_INT: {
      pxe_nbt_tag_int* int_tag = pxe_arena_push_type(arena, pxe_nbt_tag_int);

      if (pxe_buffer_chain_read_u32(reader, &int_tag->data) == 0) {
        return 0;
      }

      tag->tag = int_tag;
    } break;
    case PXE_NBT_TAG_TYPE_LONG: {
      pxe_nbt_tag_long* long_tag = pxe_arena_push_type(arena, pxe_nbt_tag_long);

      if (pxe_buffer_chain_read_u64(reader, &long_tag->data) == 0) {
        return 0;
      }

      tag->tag = long_tag;
    } break;
    case PXE_NBT_TAG_TYPE_FLOAT: {
      pxe_nbt_tag_float* float_tag =
          pxe_arena_push_type(arena, pxe_nbt_tag_float);

      if (pxe_buffer_chain_read_float(reader, &float_tag->data) == 0) {
        return 0;
      }

      tag->tag = float_tag;
    } break;
    case PXE_NBT_TAG_TYPE_DOUBLE: {
      pxe_nbt_tag_double* double_tag =
          pxe_arena_push_type(arena, pxe_nbt_tag_double);

      if (pxe_buffer_chain_read_double(reader, &double_tag->data) == 0) {
        return 0;
      }

      tag->tag = double_tag;
    } break;
    case PXE_NBT_TAG_TYPE_BYTE_ARRAY: {
      pxe_nbt_tag_byte_array* byte_array_tag =
          pxe_arena_push_type(arena, pxe_nbt_tag_byte_array);

      byte_array_tag->length = 0;
      byte_array_tag->data = NULL;

      if (pxe_buffer_chain_read_u32(reader, (u32*)&byte_array_tag->length) ==
          0) {
        return 0;
      }

      byte_array_tag->data =
          pxe_arena_alloc(arena, byte_array_tag->length * sizeof(u8));

      // Read all of the contained bytes in one read.
      if (pxe_buffer_chain_read_raw_string(reader, (char*)&byte_array_tag->data,
                                           byte_array_tag->length) == 0) {
        return 0;
      }

      tag->tag = byte_array_tag;
    } break;
    case PXE_NBT_TAG_TYPE_STRING: {
      pxe_nbt_tag_string* string_tag =
          pxe_arena_push_type(arena, pxe_nbt_tag_string);

      if (pxe_nbt_read_length_string(reader, &string_tag->data,
                                     &string_tag->length, arena) == 0) {
        return 0;
      }

      tag->tag = string_tag;
    } break;
    case PXE_NBT_TAG_TYPE_LIST: {
      pxe_nbt_tag_list* list_tag = pxe_arena_push_type(arena, pxe_nbt_tag_list);

      list_tag->length = 0;
      list_tag->tags = NULL;

      if (pxe_buffer_chain_read_u8(reader, (u8*)&list_tag->type) == 0) {
        return 0;
      }

      if (pxe_buffer_chain_read_u32(reader, (u32*)&list_tag->length) == 0) {
        return 0;
      }

      if (list_tag->length > 0) {
        // Allocate space for all of the tags.
        list_tag->tags =
            pxe_arena_push_type_count(arena, pxe_nbt_tag, list_tag->length);
      }

      for (size_t i = 0; i < list_tag->length; ++i) {
        pxe_nbt_tag* data_tag = list_tag->tags + i;

        data_tag->name = NULL;
        data_tag->name_length = 0;
        data_tag->type = list_tag->type;

        // TODO: This probably shouldn't be called recursively otherwise bad
        // actors could blow out the stack with nested lists.
        if (pxe_nbt_parse_tag(reader, data_tag, arena) == 0) {
          return 0;
        }
      }

      tag->tag = list_tag;
    } break;
    case PXE_NBT_TAG_TYPE_COMPOUND: {
      pxe_nbt_tag_compound* compound_tag =
          pxe_arena_push_type(arena, pxe_nbt_tag_compound);

      compound_tag->name = NULL;
      compound_tag->name_length = 0;
      compound_tag->ntags = 0;

      // TODO: This probably shouldn't be called recursively otherwise bad
      // actors could blow out the stack with nested lists.
      if (pxe_nbt_parse_compound(reader, compound_tag, arena) == 0) {
        return 0;
      }

      tag->tag = compound_tag;
    } break;
    case PXE_NBT_TAG_TYPE_INT_ARRAY: {
      pxe_nbt_tag_int_array* int_array_tag =
          pxe_arena_push_type(arena, pxe_nbt_tag_int_array);

      int_array_tag->length = 0;
      int_array_tag->data = NULL;

      if (pxe_buffer_chain_read_u32(reader, (u32*)&int_array_tag->length) ==
          0) {
        return 0;
      }

      int_array_tag->data =
          pxe_arena_alloc(arena, int_array_tag->length * sizeof(i32));

      for (size_t i = 0; i < int_array_tag->length; ++i) {
        i32* int_data = int_array_tag->data + i;

        if (pxe_buffer_chain_read_u32(reader, (u32*)int_data) == 0) {
          return 0;
        }
      }

      tag->tag = int_array_tag;
    } break;
    case PXE_NBT_TAG_TYPE_LONG_ARRAY: {
      pxe_nbt_tag_long_array* long_array_tag =
          pxe_arena_push_type(arena, pxe_nbt_tag_long_array);

      long_array_tag->length = 0;
      long_array_tag->data = NULL;

      if (pxe_buffer_chain_read_u32(reader, (u32*)&long_array_tag->length) ==
          0) {
        return 0;
      }

      long_array_tag->data =
          pxe_arena_alloc(arena, long_array_tag->length * sizeof(i64));

      for (size_t i = 0; i < long_array_tag->length; ++i) {
        i64* long_data = long_array_tag->data + i;

        if (pxe_buffer_chain_read_u64(reader, (u64*)long_data) == 0) {
          return 0;
        }
      }

      tag->tag = long_array_tag;
    } break;
    default: {
      fprintf(stderr, "Unknown NBT type: %d\n", tag->type);
      return 0;
    }
  }

  return 1;
}

bool32 pxe_nbt_parse(char* data, size_t size, pxe_memory_arena* arena,
                     pxe_nbt_tag_compound* result) {
  pxe_buffer buffer;
  buffer.data = (u8*)data;
  buffer.size = size;

  pxe_buffer_chain chain;
  chain.buffer = &buffer;
  chain.next = NULL;

  pxe_buffer_chain_reader reader;
  reader.chain = &chain;
  reader.read_pos = 0;

  pxe_nbt_tag_type type = 0;
  if (pxe_buffer_chain_read_u8(&reader, (u8*)&type) == 0) {
    return 0;
  }

  if (type != PXE_NBT_TAG_TYPE_COMPOUND) {
    return 0;
  }

  if (pxe_nbt_read_length_string(&reader, &result->name, &result->name_length,
                                 arena) == 0) {
    return 0;
  }

  if (pxe_nbt_parse_compound(&reader, result, arena) == 0) {
    return 0;
  }

  return 1;
}

/////////////////////////// NBT writing

bool32 pxe_nbt_write_length_string(pxe_buffer_writer* writer, char* data,
                                   size_t size, pxe_memory_arena* arena) {
  pxe_arena_alloc_unaligned(arena, size + sizeof(u16));

  writer->buffer->size += sizeof(u16) + size;

  if (pxe_buffer_write_u16(writer, (u16)size) == 0) return 0;

  return pxe_buffer_write_raw_string(writer, data, size);
}

bool32 pxe_nbt_write_tag_data(pxe_buffer_writer* writer, pxe_nbt_tag* tag,
                              pxe_memory_arena* arena) {
  switch (tag->type) {
    case PXE_NBT_TAG_TYPE_BYTE: {
      pxe_nbt_tag_byte* byte_tag = tag->tag;

      writer->buffer->size += sizeof(u8);
      pxe_arena_alloc_unaligned(arena, sizeof(u8));

      if (pxe_buffer_write_u8(writer, byte_tag->data) == 0) {
        return 0;
      }
    } break;
    case PXE_NBT_TAG_TYPE_SHORT: {
      pxe_nbt_tag_short* short_tag = tag->tag;

      writer->buffer->size += sizeof(u16);
      pxe_arena_alloc_unaligned(arena, sizeof(u16));

      if (pxe_buffer_write_u16(writer, short_tag->data) == 0) {
        return 0;
      }
    } break;
    case PXE_NBT_TAG_TYPE_INT: {
      pxe_nbt_tag_int* int_tag = tag->tag;

      writer->buffer->size += sizeof(u32);
      pxe_arena_alloc_unaligned(arena, sizeof(u32));

      if (pxe_buffer_write_u32(writer, int_tag->data) == 0) {
        return 0;
      }
    } break;
    case PXE_NBT_TAG_TYPE_LONG: {
      pxe_nbt_tag_long* long_tag = tag->tag;

      writer->buffer->size += sizeof(u64);
      pxe_arena_alloc_unaligned(arena, sizeof(u64));

      if (pxe_buffer_write_u64(writer, long_tag->data) == 0) {
        return 0;
      }
    } break;
    case PXE_NBT_TAG_TYPE_FLOAT: {
      pxe_nbt_tag_float* float_tag = tag->tag;

      writer->buffer->size += sizeof(float);
      pxe_arena_alloc_unaligned(arena, sizeof(float));

      if (pxe_buffer_write_float(writer, float_tag->data) == 0) {
        return 0;
      }
    } break;
    case PXE_NBT_TAG_TYPE_DOUBLE: {
      pxe_nbt_tag_double* double_tag = tag->tag;

      writer->buffer->size += sizeof(double);
      pxe_arena_alloc_unaligned(arena, sizeof(double));

      if (pxe_buffer_write_double(writer, double_tag->data) == 0) {
        return 0;
      }
    } break;
    case PXE_NBT_TAG_TYPE_BYTE_ARRAY: {
      pxe_nbt_tag_byte_array* byte_array_tag = tag->tag;

      size_t data_size = sizeof(u32) + byte_array_tag->length;

      writer->buffer->size += data_size;
      pxe_arena_alloc_unaligned(arena, data_size);

      if (pxe_buffer_write_u32(writer, (u32)byte_array_tag->length) == 0) {
        return 0;
      }

      if (pxe_buffer_write_raw_string(writer, (char*)byte_array_tag->data,
                                      byte_array_tag->length) == 0) {
        return 0;
      }
    } break;
    case PXE_NBT_TAG_TYPE_STRING: {
      pxe_nbt_tag_string* string_tag = tag->tag;

      if (pxe_nbt_write_length_string(writer, string_tag->data,
                                      string_tag->length, arena) == 0) {
        return 0;
      }
    } break;
    case PXE_NBT_TAG_TYPE_LIST: {
      pxe_nbt_tag_list* list_tag = tag->tag;

      writer->buffer->size += 1;
      pxe_arena_alloc_unaligned(arena, 1);

      if (list_tag->length == 0) {
        list_tag->type = PXE_NBT_TAG_TYPE_END;
      }

      if (pxe_buffer_write_u8(writer, (u8)list_tag->type) == 0) {
        return 0;
      }

      writer->buffer->size += sizeof(u32);
      pxe_arena_alloc_unaligned(arena, sizeof(u32));

      if (pxe_buffer_write_u32(writer, (u32)list_tag->length) == 0) {
        return 0;
      }

      for (size_t i = 0; i < list_tag->length; ++i) {
        pxe_nbt_tag* tag_entry = list_tag->tags + i;

        // TODO: This probably shouldn't be called recursively otherwise bad
        // actors could blow out the stack with nested lists.
        if (pxe_nbt_write_tag_data(writer, tag_entry, arena) == 0) {
          return 0;
        }
      }
    } break;
    case PXE_NBT_TAG_TYPE_COMPOUND: {
      pxe_nbt_tag_compound* compound_tag = tag->tag;

      for (size_t i = 0; i < compound_tag->ntags; ++i) {
        pxe_nbt_tag* tag_entry = compound_tag->tags + i;

        // TODO: This probably shouldn't be called recursively otherwise bad
        // actors could blow out the stack with nested lists.
        if (pxe_nbt_write_named_tag(writer, tag_entry, arena) == 0) {
          return 0;
        }
      }
    } break;
    case PXE_NBT_TAG_TYPE_INT_ARRAY: {
      pxe_nbt_tag_int_array* int_array_tag = tag->tag;

      size_t data_size = sizeof(u32) + int_array_tag->length * sizeof(u32);

      writer->buffer->size += data_size;
      pxe_arena_alloc_unaligned(arena, data_size);

      if (pxe_buffer_write_u32(writer, (u32)int_array_tag->length) == 0) {
        return 0;
      }

      for (size_t i = 0; i < int_array_tag->length; ++i) {
        u32* int_data = (u32*)int_array_tag->data + i;

        if (pxe_buffer_write_u32(writer, *int_data) == 0) {
          return 0;
        }
      }
    } break;
    case PXE_NBT_TAG_TYPE_LONG_ARRAY: {
      pxe_nbt_tag_long_array* long_array_tag = tag->tag;

      size_t data_size = sizeof(u32) + long_array_tag->length * sizeof(u64);

      writer->buffer->size += data_size;
      pxe_arena_alloc_unaligned(arena, data_size);

      if (pxe_buffer_write_u32(writer, (u32)long_array_tag->length) == 0) {
        return 0;
      }

      for (size_t i = 0; i < long_array_tag->length; ++i) {
        u64* long_data = (u64*)long_array_tag->data + i;

        if (pxe_buffer_write_u64(writer, *long_data) == 0) {
          return 0;
        }
      }
    } break;
    default: {
      return 0;
    }
  }

  return 1;
}

bool32 pxe_nbt_write_named_tag(pxe_buffer_writer* writer, pxe_nbt_tag* tag,
                               pxe_memory_arena* arena) {
  pxe_arena_alloc_unaligned(arena, 1);
  ++writer->buffer->size;

  if (pxe_buffer_write_u8(writer, (u8)tag->type) == 0) {
    return 0;
  }

  if (pxe_nbt_write_length_string(writer, tag->name, tag->name_length, arena) ==
      0) {
    return 0;
  }

  if (pxe_nbt_write_tag_data(writer, tag, arena) == 0) {
    return 0;
  }

  return 1;
}

// This function will break if memory arenas are ever updated to be
// multithreaded because it relies on the arena allocating contiguously.
bool32 pxe_nbt_write(pxe_nbt_tag_compound* compound, pxe_memory_arena* arena,
                     char** out, size_t* size) {
  // Beginning allocation should be aligned so the resulting pointer is aligned.
  *out = pxe_arena_alloc(arena, 1);

  pxe_buffer_writer writer;
  pxe_buffer buffer;

  buffer.data = (u8*)*out;
  buffer.size = 1;
  writer.buffer = &buffer;
  writer.write_pos = 0;

  if (pxe_buffer_write_u8(&writer, (u8)PXE_NBT_TAG_TYPE_COMPOUND) == 0) {
    return 0;
  }

  if (pxe_nbt_write_length_string(&writer, compound->name,
                                  compound->name_length, arena) == 0) {
    return 0;
  }

  for (size_t i = 0; i < compound->ntags; ++i) {
    pxe_nbt_tag* tag = compound->tags + i;

    if (pxe_nbt_write_named_tag(&writer, tag, arena) == 0) {
      return 0;
    }
  }

  pxe_arena_alloc_unaligned(arena, 1);
  ++buffer.size;
  if (pxe_buffer_write_u8(&writer, (u8)PXE_NBT_TAG_TYPE_END) == 0) {
    return 0;
  }

  *size = buffer.size;

  return 1;
}
