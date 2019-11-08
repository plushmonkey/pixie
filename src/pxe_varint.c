#include "pxe_varint.h"

size_t pxe_varint_write(i32 signed_value, char* buf) {
  size_t index = 0;

  u32 value = (u32)signed_value;

  do {
    u8 byte = (u8)(value & 0x7F);

    value >>= 7;

    if (value) {
      byte |= 0x80;
    }

    buf[index++] = byte;
  } while (value);

  return index;
}

size_t pxe_varint_size(i32 signed_value) {
  size_t index = 0;

  u32 value = (u32)signed_value;

  do {
    value >>= 7;

    ++index;
  } while (value);

  return index;
}

size_t pxe_varint_read(char* buf, size_t buf_size, i32* value) {
  int shift = 0;
  size_t i = 0;

  *value = 0;

  do {
    if (i >= buf_size) {
      // The buffer doesn't have enough data to fully read this VarInt.
      *value = 0;
      return 0;
    }

    *value |= (i32)(buf[i] & 0x7F) << shift;
    shift += 7;
  } while ((buf[i++] & 0x80) != 0);

  return i;
}

size_t pxe_varlong_write(i64 signed_value, char* buf) {
  size_t index = 0;

  u64 value = (u64)signed_value;

  do {
    uint8_t byte = value & 0x7F;

    value >>= 7;

    if (value) {
      byte |= 0x80;
    }

    buf[index++] = byte;
  } while (value);

  return index;
}

size_t pxe_varlong_size(i64 signed_value) {
  size_t index = 0;

  u64 value = (u64)signed_value;

  do {
    value >>= 7;

    ++index;
  } while (value);

  return index;
}

size_t pxe_varlong_read(char* buf, size_t buf_size, i64* value) {
  int shift = 0;
  size_t i = 0;

  *value = 0;

  do {
    if (i >= buf_size) {
      // The buffer doesn't have enough data to fully read this VarLong.
      *value = 0;
      return 0;
    }

    *value |= (i64)(buf[i] & 0x7F) << shift;
    shift += 7;
  } while ((buf[i++] & 0x80) != 0);

  return i;
}
