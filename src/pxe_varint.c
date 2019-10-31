#include "pxe_varint.h"

size_t pxe_varint_write(i64 value, char* buf) {
  size_t index = 0;

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

size_t pxe_varint_size(i64 value) {
  size_t index = 0;

  do {
    value >>= 7;

    ++index;
  } while (value);

  return index;
}

size_t pxe_varint_read(char* buf, size_t buf_size, i64* value) {
  int shift = 0;
  size_t i = 0;

  *value = 0;

  do {
    if (i >= buf_size) {
      // The buffer doesn't have enough data to fully read this VarInt.
      *value = 0;
      return 0;
    }

    *value |= (i64)(buf[i] & 0x7F) << shift;
    shift += 7;
  } while ((buf[i++] & 0x80) != 0);

  return i;
}
