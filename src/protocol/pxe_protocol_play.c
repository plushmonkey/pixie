#include "pxe_protocol_play.h"

#include "../pxe_alloc.h"
#include "../pxe_buffer.h"
#include "../pxe_varint.h"

#include <stdio.h>
#include <string.h>

struct pxe_buffer* pxe_serialize_play_chat(struct pxe_memory_arena* arena,
                                           char* message, size_t message_size,
                                           char* color) {
  char data[512];
  u8 position = 0;

  size_t data_len =
      sprintf_s(data, pxe_array_size(data),
                "{\"text\":\"%s\", \"color\": \"%s\"}", message, color);

  size_t size = data_len + pxe_varint_size((i32)data_len) + sizeof(u8);
  pxe_buffer_writer writer = pxe_buffer_writer_create(arena, size);

  if (pxe_buffer_write_length_string(&writer, data, data_len) == 0) {
    return NULL;
  }

  if (pxe_buffer_write_u8(&writer, position) == 0) {
    return NULL;
  }

  return writer.buffer;
}

struct pxe_buffer* pxe_serialize_play_keep_alive(struct pxe_memory_arena* arena,
                                                 i64 id) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(arena, sizeof(u64));

  if (pxe_buffer_write_u64(&writer, (u64)id) == 0) {
    return NULL;
  }

  return writer.buffer;
}

struct pxe_buffer* pxe_serialize_play_join_game(struct pxe_memory_arena* arena,
                                                i32 eid, u8 gamemode,
                                                i32 dimension, char* level_type,
                                                i32 view_distance,
                                                bool32 reduced_debug) {
  u8 max_players = 0xFF;
  size_t level_len = (i32)strlen(level_type);

  size_t size = sizeof(eid) + sizeof(gamemode) + sizeof(dimension) +
                sizeof(max_players) + pxe_varint_size((i32)level_len) +
                level_len + pxe_varint_size(view_distance) + 1;

  pxe_buffer_writer writer = pxe_buffer_writer_create(arena, size);

  if (!pxe_buffer_write_u32(&writer, eid)) {
    return NULL;
  }

  if (!pxe_buffer_write_u8(&writer, gamemode)) {
    return NULL;
  }

  if (!pxe_buffer_write_u32(&writer, dimension)) {
    return NULL;
  }

  if (!pxe_buffer_write_u8(&writer, max_players)) {
    return NULL;
  }

  if (!pxe_buffer_write_length_string(&writer, level_type, level_len)) {
    return NULL;
  }

  if (!pxe_buffer_write_varint(&writer, view_distance)) {
    return NULL;
  }

  if (!pxe_buffer_write_u8(&writer, (u8)reduced_debug)) {
    return NULL;
  }

  return writer.buffer;
}

struct pxe_buffer* pxe_serialize_play_player_abilities(
    struct pxe_memory_arena* arena, u8 flags, float fly_speed, float fov) {
  size_t size = sizeof(u8) + sizeof(float) + sizeof(float);

  pxe_buffer_writer writer = pxe_buffer_writer_create(arena, size);

  if (!pxe_buffer_write_u8(&writer, flags)) {
    return NULL;
  }

  if (!pxe_buffer_write_float(&writer, fly_speed)) {
    return NULL;
  }

  if (!pxe_buffer_write_float(&writer, fov)) {
    return NULL;
  }

  return writer.buffer;
}

struct pxe_buffer* pxe_serialize_play_position_and_look(
    struct pxe_memory_arena* arena, double x, double y, double z, float yaw,
    float pitch, u8 flags, i32 teleport_id) {
  size_t size = sizeof(double) + sizeof(double) + sizeof(double) +
                sizeof(float) + sizeof(float) + sizeof(u8) +
                pxe_varint_size(teleport_id);

  pxe_buffer_writer writer = pxe_buffer_writer_create(arena, size);

  if (!pxe_buffer_write_double(&writer, x)) {
    return NULL;
  }

  if (!pxe_buffer_write_double(&writer, y)) {
    return NULL;
  }

  if (!pxe_buffer_write_double(&writer, z)) {
    return NULL;
  }

  if (!pxe_buffer_write_float(&writer, yaw)) {
    return NULL;
  }

  if (!pxe_buffer_write_float(&writer, pitch)) {
    return NULL;
  }

  if (!pxe_buffer_write_u8(&writer, flags)) {
    return NULL;
  }

  if (!pxe_buffer_write_varint(&writer, teleport_id)) {
    return NULL;
  }

  return writer.buffer;
}
