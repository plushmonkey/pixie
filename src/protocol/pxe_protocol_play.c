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

struct pxe_buffer* pxe_serialize_play_plugin_message(
    struct pxe_memory_arena* arena, const char* channel, const u8* data,
    size_t size) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(arena, 0);

  pxe_buffer_push_length_string(&writer, channel, strlen(channel), arena);
  pxe_buffer_push_length_string(&writer, (char*)data, size, arena);

  return writer.buffer;
}

struct pxe_buffer* pxe_serialize_play_change_game_state(
    struct pxe_memory_arena* arena, pxe_change_game_state_reason reason,
    float value) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(arena, 0);

  pxe_buffer_push_u8(&writer, (u8)reason, arena);
  pxe_buffer_push_float(&writer, value, arena);

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

struct pxe_buffer* pxe_serialize_play_spawn_player(
    struct pxe_memory_arena* arena, i32 eid, pxe_uuid* uuid, double x, double y,
    double z, float yaw, float pitch) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(arena, 0);

  if (pxe_buffer_push_varint(&writer, eid, arena) == 0) {
    return NULL;
  }

  if (pxe_buffer_push_uuid(&writer, uuid, arena) == 0) {
    return NULL;
  }

  if (pxe_buffer_push_double(&writer, x, arena) == 0) {
    return NULL;
  }

  if (pxe_buffer_push_double(&writer, y, arena) == 0) {
    return NULL;
  }

  if (pxe_buffer_push_double(&writer, z, arena) == 0) {
    return NULL;
  }

  u8 yaw_data = (u8)((yaw / 360.0f) * 256);
  u8 pitch_data = (u8)((pitch / 360.0f) * 256);

  if (pxe_buffer_push_u8(&writer, yaw_data, arena) == 0) {
    return NULL;
  }

  if (pxe_buffer_push_u8(&writer, pitch_data, arena) == 0) {
    return NULL;
  }

  // TODO: Serialize metadata here when implemented
  if (pxe_buffer_push_u8(&writer, 0xFF, arena) == 0) {
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

struct pxe_buffer* pxe_serialize_play_entity_look_and_relative_move(
    struct pxe_memory_arena* arena, pxe_entity_id eid, double delta_x,
    double delta_y, double delta_z, float yaw, float pitch, bool32 on_ground) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(arena, 0);

  if (!pxe_buffer_push_varint(&writer, eid, arena)) {
    return NULL;
  }

  i16 encoded_x = (i16)((delta_x * 32) * 128);
  i16 encoded_y = (i16)((delta_y * 32) * 128);
  i16 encoded_z = (i16)((delta_z * 32) * 128);

  if (!pxe_buffer_push_u16(&writer, (u16)encoded_x, arena)) {
    return NULL;
  }

  if (!pxe_buffer_push_u16(&writer, (u16)encoded_y, arena)) {
    return NULL;
  }

  if (!pxe_buffer_push_u16(&writer, (u16)encoded_z, arena)) {
    return NULL;
  }

  u8 yaw_data = (u8)((yaw / 360.0f) * 256);
  u8 pitch_data = (u8)((pitch / 360.0f) * 256);

  if (pxe_buffer_push_u8(&writer, yaw_data, arena) == 0) {
    return NULL;
  }

  if (pxe_buffer_push_u8(&writer, pitch_data, arena) == 0) {
    return NULL;
  }

  if (pxe_buffer_push_u8(&writer, (u8)on_ground, arena) == 0) {
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

struct pxe_buffer* pxe_serialize_play_player_info(
    struct pxe_memory_arena* arena, pxe_player_info_action action,
    pxe_player_info* infos, size_t info_count) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(arena, 0);

  if (pxe_buffer_push_varint(&writer, (i32)action, arena) == 0) {
    return NULL;
  }

  if (pxe_buffer_push_varint(&writer, (i32)info_count, arena) == 0) {
    return NULL;
  }

  for (size_t i = 0; i < info_count; ++i) {
    pxe_player_info* info = infos + i;

    if (pxe_buffer_push_uuid(&writer, &info->uuid, arena) == 0) {
      return NULL;
    }

    switch (action) {
      case PXE_PLAYER_INFO_ADD: {
        size_t name_len = strlen(info->add.name);

        if (pxe_buffer_push_length_string(&writer, info->add.name, name_len,
                                          arena) == 0) {
          return NULL;
        }

        if (pxe_buffer_push_varint(&writer, (i32)info->add.property_count,
                                   arena) == 0) {
          return NULL;
        }

        for (size_t property_index = 0;
             property_index < info->add.property_count; ++property_index) {
          pxe_player_info_add_property* property =
              info->add.properties + property_index;

          if (pxe_buffer_push_length_string(&writer, property->name,
                                            property->name_len, arena) == 0) {
            return NULL;
          }

          if (pxe_buffer_push_length_string(&writer, property->value,
                                            property->value_len, arena) == 0) {
            return NULL;
          }

          if (pxe_buffer_push_u8(&writer, (u8)property->is_signed, arena) ==
              0) {
            return NULL;
          }

          if (property->is_signed) {
            if (pxe_buffer_push_length_string(&writer, property->signature,
                                              property->signature_len,
                                              arena) == 0) {
              return NULL;
            }
          }
        }

        if (pxe_buffer_push_varint(&writer, (i32)info->add.gamemode, arena) ==
            0) {
          return NULL;
        }

        if (pxe_buffer_push_varint(&writer, (i32)info->add.ping, arena) == 0) {
          return NULL;
        }

        u8 has_display_name = info->add.display_name_size > 0;

        if (pxe_buffer_push_u8(&writer, (u8)has_display_name, arena) == 0) {
          return NULL;
        }

        if (has_display_name) {
          if (pxe_buffer_push_length_string(&writer, info->add.display_name,
                                            info->add.display_name_size,
                                            arena) == 0) {
            return NULL;
          }
        }
      } break;
      case PXE_PLAYER_INFO_REMOVE: {
        // Nothing needs to be done here.
      } break;
      default: {
        fprintf(stderr, "player_info type %d not yet implemented.\n", action);
        return NULL;
      }
    }
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

struct pxe_buffer* pxe_serialize_play_destroy_entities(
    struct pxe_memory_arena* arena, pxe_entity_id* entities, size_t count) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(arena, 0);

  if (pxe_buffer_push_varint(&writer, (i32)count, arena) == 0) {
    return NULL;
  }

  for (size_t i = 0; i < count; ++i) {
    pxe_entity_id eid = entities[i];

    if (pxe_buffer_push_varint(&writer, eid, arena) == 0) {
      return NULL;
    }
  }

  return writer.buffer;
}

struct pxe_buffer* pxe_serialize_play_entity_head_look(
    struct pxe_memory_arena* arena, pxe_entity_id eid, float yaw) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(arena, 0);

  if (pxe_buffer_push_varint(&writer, eid, arena) == 0) {
    return NULL;
  }

  u8 yaw_data = (u8)((yaw / 360.0f) * 256);

  if (pxe_buffer_push_u8(&writer, yaw_data, arena) == 0) {
    return NULL;
  }

  return writer.buffer;
}

struct pxe_buffer* pxe_serialize_play_time_update(
    struct pxe_memory_arena* arena, u64 world_age, u64 time) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(arena, 0);

  pxe_buffer_push_u64(&writer, world_age, arena);
  pxe_buffer_push_u64(&writer, time, arena);

  return writer.buffer;
}

struct pxe_buffer* pxe_serialize_play_entity_teleport(
    struct pxe_memory_arena* arena, pxe_entity_id eid, double x, double y,
    double z, float yaw, float pitch, bool32 on_ground) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(arena, 0);

  if (!pxe_buffer_push_varint(&writer, eid, arena)) {
    return NULL;
  }

  if (!pxe_buffer_push_double(&writer, x, arena)) {
    return NULL;
  }

  if (!pxe_buffer_push_double(&writer, y, arena)) {
    return NULL;
  }

  if (!pxe_buffer_push_double(&writer, z, arena)) {
    return NULL;
  }

  u8 yaw_data = (u8)((yaw / 360.0f) * 256);
  u8 pitch_data = (u8)((pitch / 360.0f) * 256);

  if (pxe_buffer_push_u8(&writer, yaw_data, arena) == 0) {
    return NULL;
  }

  if (pxe_buffer_push_u8(&writer, pitch_data, arena) == 0) {
    return NULL;
  }

  if (pxe_buffer_push_u8(&writer, (u8)on_ground, arena) == 0) {
    return NULL;
  }

  return writer.buffer;
}
