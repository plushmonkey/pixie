#ifndef PIXIE_H_
#define PIXIE_H_

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

#define array_size(arr) (sizeof((arr)) / (sizeof(*(arr))))
#define array_string_size(arr) ((sizeof((arr)) / (sizeof(*(arr)))) - 1)

#endif
