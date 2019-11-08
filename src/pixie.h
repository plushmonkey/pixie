#ifndef PIXIE_H_
#define PIXIE_H_

#include <stddef.h>
#include <stdint.h>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef i32 bool32;

#define pxe_array_size(arr) (sizeof((arr)) / (sizeof(*(arr))))
#define pxe_array_string_size(arr) (pxe_array_size(arr) - 1)

inline size_t pxe_bitset_count(u32 value) {
  size_t count = 0;

  for (size_t i = 0; i < sizeof(u32) * 8; ++i) {
    count += (value & 1);
    value >>= 1;
  }

  return count;
}

#ifndef _MSC_VER
int sprintf_s(char* str, size_t str_size, const char* format, ...);
#endif

#endif
