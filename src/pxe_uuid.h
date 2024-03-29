#ifndef PIXIE_UUID_H_
#define PIXIE_UUID_H_

#include "pixie.h"

#include "pxe_buffer.h"

typedef struct pxe_uuid {
  u64 most_significant;
  u64 least_significant;
} pxe_uuid;

pxe_uuid pxe_uuid_create(u64 most, u64 least);
pxe_uuid pxe_uuid_create_from_string(const char* str, bool32 dashes);

// This creates completely random numbers. They aren't valid uuids.
pxe_uuid pxe_uuid_random();

// TODO: profile this because it's probably really slow and could be improved
// significantly if it's a problem.
void pxe_uuid_to_string(pxe_uuid* uuid, char* str, bool32 dashes);

struct pxe_buffer_writer;
bool32 pxe_buffer_write_uuid(struct pxe_buffer_writer* writer, pxe_uuid* uuid);

#endif
