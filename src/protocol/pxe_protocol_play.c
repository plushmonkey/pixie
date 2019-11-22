#include "pxe_protocol_play.h"

#include "../pxe_alloc.h"
#include "../pxe_buffer.h"
#include "../pxe_varint.h"

#include <stdio.h>
#include <string.h>

struct pxe_buffer_chain* pxe_serialize_play_chat(struct pxe_pool* pool,
                                                 char* message,
                                                 size_t message_size,
                                                 char* color) {
  char data[512];
  u8 position = 0;

  size_t data_len =
      sprintf_s(data, pxe_array_size(data),
                "{\"text\":\"%s\", \"color\": \"%s\"}", message, color);
  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

  if (pxe_buffer_write_length_string(&writer, data, data_len) == 0) {
    return NULL;
  }

  if (pxe_buffer_write_u8(&writer, position) == 0) {
    return NULL;
  }

  return writer.head;
}

struct pxe_buffer_chain* pxe_serialize_play_animation(
    struct pxe_pool* pool, i32 eid, pxe_animation_type animation) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

  if (pxe_buffer_write_varint(&writer, eid) == 0) {
    return NULL;
  }

  if (pxe_buffer_write_u8(&writer, (u8)animation) == 0) {
    return NULL;
  }

  return writer.head;
}

struct pxe_buffer_chain* pxe_serialize_play_spawn_player(
    struct pxe_pool* pool, i32 eid, pxe_uuid* uuid, double x, double y,
    double z, float yaw, float pitch) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

  if (pxe_buffer_write_varint(&writer, eid) == 0) {
    return NULL;
  }

  if (pxe_buffer_write_uuid(&writer, uuid) == 0) {
    return NULL;
  }

  if (pxe_buffer_write_double(&writer, x) == 0) {
    return NULL;
  }

  if (pxe_buffer_write_double(&writer, y) == 0) {
    return NULL;
  }

  if (pxe_buffer_write_double(&writer, z) == 0) {
    return NULL;
  }

  u8 yaw_data = (u8)((yaw / 360.0f) * 256);
  u8 pitch_data = (u8)((pitch / 360.0f) * 256);

  if (pxe_buffer_write_u8(&writer, yaw_data) == 0) {
    return NULL;
  }

  if (pxe_buffer_write_u8(&writer, pitch_data) == 0) {
    return NULL;
  }

  // TODO: Serialize metadata here when implemented
  if (pxe_buffer_write_u8(&writer, 0xFF) == 0) {
    return NULL;
  }

  return writer.head;
}

struct pxe_buffer_chain* pxe_serialize_play_plugin_message(
    struct pxe_pool* pool, const char* channel, const u8* data, size_t size) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

  pxe_buffer_write_length_string(&writer, channel, strlen(channel));
  pxe_buffer_write_length_string(&writer, (char*)data, size);

  return writer.head;
}

struct pxe_buffer_chain* pxe_serialize_play_entity_status(struct pxe_pool* pool,
                                                          pxe_entity_id eid,
                                                          u8 status) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

  pxe_buffer_write_u32(&writer, eid);
  pxe_buffer_write_u8(&writer, status);

  return writer.head;
}

struct pxe_buffer_chain* pxe_serialize_play_change_game_state(
    struct pxe_pool* pool, pxe_change_game_state_reason reason, float value) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

  pxe_buffer_write_u8(&writer, (u8)reason);
  pxe_buffer_write_float(&writer, value);

  return writer.head;
}

struct pxe_buffer_chain* pxe_serialize_play_keep_alive(struct pxe_pool* pool,
                                                       i64 id) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

  if (pxe_buffer_write_u64(&writer, (u64)id) == 0) {
    return NULL;
  }

  return writer.head;
}

struct pxe_buffer_chain* pxe_serialize_play_join_game(
    struct pxe_pool* pool, i32 eid, u8 gamemode, i32 dimension,
    char* level_type, i32 view_distance, bool32 reduced_debug) {
  u8 max_players = 0xFF;
  size_t level_len = (i32)strlen(level_type);

  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

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

  return writer.head;
}

struct pxe_buffer_chain* pxe_serialize_play_entity_look_and_relative_move(
    struct pxe_pool* pool, pxe_entity_id eid, double delta_x, double delta_y,
    double delta_z, float yaw, float pitch, bool32 on_ground) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

  if (!pxe_buffer_write_varint(&writer, eid)) {
    return NULL;
  }

  i16 encoded_x = (i16)((delta_x * 32) * 128);
  i16 encoded_y = (i16)((delta_y * 32) * 128);
  i16 encoded_z = (i16)((delta_z * 32) * 128);

  if (!pxe_buffer_write_u16(&writer, (u16)encoded_x)) {
    return NULL;
  }

  if (!pxe_buffer_write_u16(&writer, (u16)encoded_y)) {
    return NULL;
  }

  if (!pxe_buffer_write_u16(&writer, (u16)encoded_z)) {
    return NULL;
  }

  u8 yaw_data = (u8)((yaw / 360.0f) * 256);
  u8 pitch_data = (u8)((pitch / 360.0f) * 256);

  if (pxe_buffer_write_u8(&writer, yaw_data) == 0) {
    return NULL;
  }

  if (pxe_buffer_write_u8(&writer, pitch_data) == 0) {
    return NULL;
  }

  if (pxe_buffer_write_u8(&writer, (u8)on_ground) == 0) {
    return NULL;
  }

  return writer.head;
}

struct pxe_buffer_chain* pxe_serialize_play_player_abilities(
    struct pxe_pool* pool, u8 flags, float fly_speed, float fov) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

  if (!pxe_buffer_write_u8(&writer, flags)) {
    return NULL;
  }

  if (!pxe_buffer_write_float(&writer, fly_speed)) {
    return NULL;
  }

  if (!pxe_buffer_write_float(&writer, fov)) {
    return NULL;
  }

  return writer.head;
}

struct pxe_buffer_chain* pxe_serialize_play_player_info(
    struct pxe_pool* pool, pxe_player_info_action action,
    pxe_player_info* infos, size_t info_count) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

  if (pxe_buffer_write_varint(&writer, (i32)action) == 0) {
    return NULL;
  }

  if (pxe_buffer_write_varint(&writer, (i32)info_count) == 0) {
    return NULL;
  }

  for (size_t i = 0; i < info_count; ++i) {
    pxe_player_info* info = infos + i;

    if (pxe_buffer_write_uuid(&writer, &info->uuid) == 0) {
      return NULL;
    }

    switch (action) {
      case PXE_PLAYER_INFO_ADD: {
        size_t name_len = strlen(info->add.name);

        if (pxe_buffer_write_length_string(&writer, info->add.name, name_len) ==
            0) {
          return NULL;
        }

        if (pxe_buffer_write_varint(&writer, (i32)info->add.property_count) ==
            0) {
          return NULL;
        }

        for (size_t property_index = 0;
             property_index < info->add.property_count; ++property_index) {
          pxe_player_info_add_property* property =
              info->add.properties + property_index;

          if (pxe_buffer_write_length_string(&writer, property->name,
                                             property->name_len) == 0) {
            return NULL;
          }

          if (pxe_buffer_write_length_string(&writer, property->value,
                                             property->value_len) == 0) {
            return NULL;
          }

          if (pxe_buffer_write_u8(&writer, (u8)property->is_signed) == 0) {
            return NULL;
          }

          if (property->is_signed) {
            if (pxe_buffer_write_length_string(&writer, property->signature,
                                               property->signature_len) == 0) {
              return NULL;
            }
          }
        }

        if (pxe_buffer_write_varint(&writer, (i32)info->add.gamemode) == 0) {
          return NULL;
        }

        if (pxe_buffer_write_varint(&writer, (i32)info->add.ping) == 0) {
          return NULL;
        }

        u8 has_display_name = info->add.display_name_size > 0;

        if (pxe_buffer_write_u8(&writer, (u8)has_display_name) == 0) {
          return NULL;
        }

        if (has_display_name) {
          if (pxe_buffer_write_length_string(&writer, info->add.display_name,
                                             info->add.display_name_size) ==
              0) {
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

  return writer.head;
}

struct pxe_buffer_chain* pxe_serialize_play_position_and_look(
    struct pxe_pool* pool, double x, double y, double z, float yaw, float pitch,
    u8 flags, i32 teleport_id) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

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

  return writer.head;
}

struct pxe_buffer_chain* pxe_serialize_play_destroy_entities(
    struct pxe_pool* pool, pxe_entity_id* entities, size_t count) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

  if (pxe_buffer_write_varint(&writer, (i32)count) == 0) {
    return NULL;
  }

  for (size_t i = 0; i < count; ++i) {
    pxe_entity_id eid = entities[i];

    if (pxe_buffer_write_varint(&writer, eid) == 0) {
      return NULL;
    }
  }

  return writer.head;
}

struct pxe_buffer_chain* pxe_serialize_play_respawn(struct pxe_pool* pool,
                                                    i32 dimension,
                                                    pxe_gamemode gamemode,
                                                    char* level_type) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

  pxe_buffer_write_u32(&writer, dimension);
  pxe_buffer_write_u8(&writer, (u8)gamemode);
  pxe_buffer_write_length_string(&writer, level_type, strlen(level_type));

  return writer.head;
}

struct pxe_buffer_chain* pxe_serialize_play_entity_head_look(
    struct pxe_pool* pool, pxe_entity_id eid, float yaw) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

  if (pxe_buffer_write_varint(&writer, eid) == 0) {
    return NULL;
  }

  u8 yaw_data = (u8)((yaw / 360.0f) * 256);

  if (pxe_buffer_write_u8(&writer, yaw_data) == 0) {
    return NULL;
  }

  return writer.head;
}

struct pxe_buffer_chain* pxe_serialize_play_update_health(struct pxe_pool* pool,
                                                          float health,
                                                          i32 food,
                                                          float saturation) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

  if (pxe_buffer_write_float(&writer, health) == 0) {
    return NULL;
  }

  if (pxe_buffer_write_varint(&writer, food) == 0) {
    return NULL;
  }

  if (pxe_buffer_write_float(&writer, saturation) == 0) {
    return NULL;
  }

  return writer.head;
}

struct pxe_buffer_chain* pxe_serialize_play_time_update(struct pxe_pool* pool,
                                                        u64 world_age,
                                                        u64 time) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

  pxe_buffer_write_u64(&writer, world_age);
  pxe_buffer_write_u64(&writer, time);

  return writer.head;
}

struct pxe_buffer_chain* pxe_serialize_play_entity_teleport(
    struct pxe_pool* pool, pxe_entity_id eid, double x, double y, double z,
    float yaw, float pitch, bool32 on_ground) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

  if (!pxe_buffer_write_varint(&writer, eid)) {
    return NULL;
  }

  if (!pxe_buffer_write_double(&writer, x)) {
    return NULL;
  }

  if (!pxe_buffer_write_double(&writer, y)) {
    return NULL;
  }

  if (!pxe_buffer_write_double(&writer, z)) {
    return NULL;
  }

  u8 yaw_data = (u8)((yaw / 360.0f) * 256);
  u8 pitch_data = (u8)((pitch / 360.0f) * 256);

  if (pxe_buffer_write_u8(&writer, yaw_data) == 0) {
    return NULL;
  }

  if (pxe_buffer_write_u8(&writer, pitch_data) == 0) {
    return NULL;
  }

  if (pxe_buffer_write_u8(&writer, (u8)on_ground) == 0) {
    return NULL;
  }

  return writer.head;
}
