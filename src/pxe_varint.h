#ifndef PIXIE_VARINT_H_
#define PIXIE_VARINT_H_

#include "pixie.h"

// buf must be at least 5 bytes for int
size_t pxe_varint_write(i32 value, char* buf);
size_t pxe_varint_size(i32 value);
// Returns the number of bytes that were read from buf.
size_t pxe_varint_read(char* buf, size_t buf_size, i32* value);

// buf must be at least 10 bytes for long
size_t pxe_varlong_write(i64 value, char* buf);
size_t pxe_varlong_size(i64 value);
// Returns the number of bytes that were read from buf.
size_t pxe_varlong_read(char* buf, size_t buf_size, i64* value);

#endif
