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
