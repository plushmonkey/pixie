#include "pxe_nbt.h"

#include "pxe_alloc.h"
#include "pxe_buffer.h"

#include <stdio.h>

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
  // Begin by reading the compound tag name
  compound->name = NULL;
  compound->name_length = 0;
  compound->ntags = 0;

  if (pxe_nbt_read_length_string(reader, &compound->name, &compound->name_length,
                                 arena) == 0) {
    return 0;
  }

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

    if (pxe_nbt_read_length_string(reader, &tag->name, &tag->name_length, arena) == 0) {
      return 0;
    }

    switch (type) {
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
        pxe_nbt_tag_short* short_tag = pxe_arena_push_type(arena, pxe_nbt_tag_short);

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
        pxe_nbt_tag_float* float_tag = pxe_arena_push_type(arena, pxe_nbt_tag_float);

        if (pxe_buffer_chain_read_float(reader, &float_tag->data) == 0) {
          return 0;
        }

        tag->tag = float_tag;
      } break;
      case PXE_NBT_TAG_TYPE_DOUBLE: {
        pxe_nbt_tag_double* double_tag = pxe_arena_push_type(arena, pxe_nbt_tag_double);

        if (pxe_buffer_chain_read_double(reader, &double_tag->data) == 0) {
          return 0;
        }

        tag->tag = double_tag;
      } break;
      case PXE_NBT_TAG_TYPE_STRING: {
        pxe_nbt_tag_string* string_tag = pxe_arena_push_type(arena, pxe_nbt_tag_string);
        
        if (pxe_nbt_read_length_string(reader, &string_tag->data, &string_tag->length, arena) == 0) {
          return 0;
        }

        tag->tag = string_tag;
      } break;
      default: {
        fprintf(stderr, "Unknown NBT type: %d\n", type);
        return 0;
      }
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

  if (pxe_nbt_parse_compound(&reader, result, arena) == 0) {
    return 0;
  }

  return 1;
}
