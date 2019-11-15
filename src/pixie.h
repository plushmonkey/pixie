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

typedef i32 pxe_entity_id;

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

#ifdef _MSC_VER
#define bswap_16(x) _byteswap_ushort(x)
#define bswap_32(x) _byteswap_ulong(x)
#define bswap_64(x) _byteswap_uint64(x)
#else
#define bswap_16(x) __builtin_bswap16(x)
#define bswap_32(x) __builtin_bswap32(x)
#define bswap_64(x) __builtin_bswap64(x)
#endif

#endif
