#ifndef PIXIE_NBT_H_
#define PIXIE_NBT_H_

#include "pixie.h"

typedef enum {
  PXE_NBT_TAG_TYPE_END = 0,
  PXE_NBT_TAG_TYPE_BYTE,
  PXE_NBT_TAG_TYPE_SHORT,
  PXE_NBT_TAG_TYPE_INT,
  PXE_NBT_TAG_TYPE_LONG,
  PXE_NBT_TAG_TYPE_FLOAT,
  PXE_NBT_TAG_TYPE_DOUBLE,
  PXE_NBT_TAG_TYPE_BYTE_ARRAY,
  PXE_NBT_TAG_TYPE_STRING,
  PXE_NBT_TAG_TYPE_LIST,
  PXE_NBT_TAG_TYPE_COMPOUND,
  PXE_NBT_TAG_TYPE_INT_ARRAY,
  PXE_NBT_TAG_TYPE_LONG_ARRAY,

  PXE_NBT_TAG_TYPE_UNKNOWN = 0xFF,
} pxe_nbt_tag_type;

typedef struct pxe_nbt_tag {
  void* tag;
  char* name;
  size_t name_length;
  pxe_nbt_tag_type type;
} pxe_nbt_tag;

#ifndef PXE_NBT_MAX_TAGS
#define PXE_NBT_MAX_TAGS 1024
#endif

typedef struct pxe_nbt_tag_compound {
  pxe_nbt_tag tags[PXE_NBT_MAX_TAGS];
  size_t ntags;

  char* name;
  size_t name_length;
} pxe_nbt_tag_compound;

typedef struct pxe_nbt_tag_string {
  char* data;
  size_t length;
} pxe_nbt_tag_string;

typedef struct pxe_nbt_tag_byte {
  u8 data;
} pxe_nbt_tag_byte;

typedef struct pxe_nbt_tag_short {
  u16 data;
} pxe_nbt_tag_short;

typedef struct pxe_nbt_tag_int {
  u32 data;
} pxe_nbt_tag_int;

typedef struct pxe_nbt_tag_long {
  u64 data;
} pxe_nbt_tag_long;

typedef struct pxe_nbt_tag_float {
  float data;
} pxe_nbt_tag_float;

typedef struct pxe_nbt_tag_double {
  double data;
} pxe_nbt_tag_double;

struct pxe_memory_arena;

bool32 pxe_nbt_parse(char* data, size_t size, struct pxe_memory_arena* arena, pxe_nbt_tag_compound* result);

#endif
