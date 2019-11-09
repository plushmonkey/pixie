#include "pxe_uuid.h"

#include <stdio.h>
#include <stdlib.h>
#include "pxe_buffer.h"

pxe_uuid pxe_uuid_create(u64 most, u64 least) {
  pxe_uuid uuid;

  uuid.most_significant = most;
  uuid.least_significant = least;

  return uuid;
}

pxe_uuid pxe_uuid_create_from_string(const char* str, bool32 dashes) {
  pxe_uuid uuid = {0};

  char upper[18];
  char lower[18];

  lower[0] = upper[0] = '0';
  lower[1] = upper[1] = 'x';

  if (dashes) {
    for (u32 i = 0; i < 8; ++i) {
      upper[i + 2] = str[i];
    }

    for (u32 i = 0; i < 4; ++i) {
      upper[i + 10] = str[i + 9];
    }

    for (u32 i = 0; i < 4; ++i) {
      upper[i + 14] = str[i + 14];
    }

    for (u32 i = 0; i < 4; ++i) {
      lower[i + 2] = str[i + 19];
    }

    for (u32 i = 0; i < 12; ++i) {
      lower[i + 6] = str[i + 24];
    }
  } else {
    for (u32 i = 0; i < 16; ++i) {
      upper[i] = str[i];
    }

    for (u32 i = 0; i < 16; ++i) {
      lower[i] = str[i + 16];
    }
  }

  uuid.most_significant = strtoll(upper, NULL, 16);
  uuid.least_significant = strtoll(lower, NULL, 16);

  return uuid;
}

void pxe_uuid_to_string(pxe_uuid* uuid, char* str, bool32 dashes) {
  char temp[16];

  pxe_buffer_writer writer;
  pxe_buffer buffer;
  buffer.data = (u8*)temp;
  buffer.size = 0;
  writer.buffer = &buffer;
  writer.write_pos = 0;

  pxe_buffer_write_u64(&writer, uuid->most_significant);
  pxe_buffer_write_u64(&writer, uuid->least_significant);

  size_t read_index = 0;
  size_t write_index = 0;

  for (size_t i = 0; i < 4; ++i) {
    sprintf_s(str + write_index, 2, "%02x", (int)temp[read_index++] & 0xFF);
    write_index += 2;
  }

  if (dashes) {
    str[write_index++] = '-';
  }

  for (u32 j = 0; j < 3; ++j) {
    for (u32 i = 0; i < 2; ++i) {
      sprintf_s(str + write_index, 2, "%02x", (int)temp[read_index++] & 0xFF);
      write_index += 2;
    }

    if (dashes) {
      str[write_index++] = '-';
    }
  }

  for (size_t i = 0; i < 6; ++i) {
    sprintf_s(str + write_index, 2, "%02x", (int)temp[read_index++] & 0xFF);
    write_index += 2;
  }
}

bool32 pxe_buffer_write_uuid(struct pxe_buffer_writer* writer, pxe_uuid* uuid) {
  if (pxe_buffer_write_u64(writer, uuid->most_significant) == 0) {
    return 0;
  }

  if (pxe_buffer_write_u64(writer, uuid->least_significant) == 0) {
    return 0;
  }

  return 1;
}
