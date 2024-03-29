#ifndef PIXIE_PROTOCOL_PLAY_H_
#define PIXIE_PROTOCOL_PLAY_H_

#include "pxe_protocol.h"

#include "../pxe_session.h"
#include "../pxe_uuid.h"

struct pxe_buffer_chain;
struct pxe_pool;

typedef enum {
  PXE_ANIMATION_TYPE_SWING_MAIN = 0x00,
  PXE_ANIMATION_TYPE_DAMAGE,
  PXE_ANIMATION_TYPE_LEAVE_BED,
  PXE_ANIMATION_TYPE_SWING_OFFHAND,
  PXE_ANIMATION_TYPE_CRITICAL_EFFECT,
  PXE_ANIMATION_TYPE_MAGIC_CRITICAL_EFFECT,

  PXE_ANIMATION_TYPE_COUNT
} pxe_animation_type;

typedef enum {
  PXE_PLAYER_INFO_ADD,
  PXE_PLAYER_INFO_UPDATE_GAMEMODE,
  PXE_PLAYER_INFO_UPDATE_LATENCY,
  PXE_PLAYER_INFO_UPDATE_DISPLAY_NAME,
  PXE_PLAYER_INFO_REMOVE,
} pxe_player_info_action;

typedef struct pxe_player_info_add_property {
  char* name;
  size_t name_len;
  char* value;
  size_t value_len;
  bool32 is_signed;
  char* signature;
  size_t signature_len;
} pxe_player_info_add_property;

typedef struct pxe_player_info {
  struct {
    union {
      struct {
        char name[16];
        size_t property_count;
        pxe_player_info_add_property* properties;
        u8 gamemode;
        u32 ping;
        char* display_name;
        size_t display_name_size;
      } add;
    };
    pxe_uuid uuid;
  };
} pxe_player_info;

typedef enum {
  PXE_CHANGE_GAME_STATE_REASON_GAMEMODE = 0x03,
} pxe_change_game_state_reason;

// 0x03
struct pxe_buffer_chain* pxe_serialize_play_chat(struct pxe_pool* pool,
                                                 char* message,
                                                 size_t message_size,
                                                 char* color);

// 0x05
struct pxe_buffer_chain* pxe_serialize_play_spawn_player(
    struct pxe_pool* pool, i32 eid, pxe_uuid* uuid, double x, double y,
    double z, float yaw, float pitch);

// 0x06
struct pxe_buffer_chain* pxe_serialize_play_animation(
    struct pxe_pool* pool, i32 eid, pxe_animation_type animation);

// 0x18
struct pxe_buffer_chain* pxe_serialize_play_plugin_message(
    struct pxe_pool* pool, const char* channel, const u8* data, size_t size);

// 0x1B
struct pxe_buffer_chain* pxe_serialize_play_entity_status(struct pxe_pool* pool,
                                                          pxe_entity_id eid,
                                                          u8 status);

// 0x1E
struct pxe_buffer_chain* pxe_serialize_play_change_game_state(
    struct pxe_pool* pool, pxe_change_game_state_reason reason, float value);

// 0x20
struct pxe_buffer_chain* pxe_serialize_play_keep_alive(struct pxe_pool* pool,
                                                       i64 id);

// 0x25
struct pxe_buffer_chain* pxe_serialize_play_join_game(
    struct pxe_pool* pool, i32 eid, u8 gamemode, i32 dimension,
    char* level_type, i32 view_distance, bool32 reduced_debug);

// 0x29
struct pxe_buffer_chain* pxe_serialize_play_entity_look_and_relative_move(
    struct pxe_pool* pool, pxe_entity_id eid, double delta_x, double delta_y,
    double delta_z, float yaw, float pitch, bool32 on_ground);

// 0x31
struct pxe_buffer_chain* pxe_serialize_play_player_abilities(
    struct pxe_pool* pool, u8 flags, float fly_speed, float fov);

// 0x33
struct pxe_buffer_chain* pxe_serialize_play_player_info(
    struct pxe_pool* pool, pxe_player_info_action action, pxe_player_info* info,
    size_t info_count);

// 0x35
struct pxe_buffer_chain* pxe_serialize_play_position_and_look(
    struct pxe_pool* pool, double x, double y, double z, float yaw, float pitch,
    u8 flags, i32 teleport_id);

// 0x37
struct pxe_buffer_chain* pxe_serialize_play_destroy_entities(
    struct pxe_pool* pool, pxe_entity_id* entities, size_t count);

// 0x3A
struct pxe_buffer_chain* pxe_serialize_play_respawn(struct pxe_pool* pool,
                                                    i32 dimension,
                                                    pxe_gamemode gamemode,
                                                    char* level_type);

// 0x3B
struct pxe_buffer_chain* pxe_serialize_play_entity_head_look(
    struct pxe_pool* pool, pxe_entity_id eid, float yaw);

// 0x48
struct pxe_buffer_chain* pxe_serialize_play_update_health(struct pxe_pool* pool,
                                                          float health,
                                                          i32 food,
                                                          float saturation);

// 0x4E
struct pxe_buffer_chain* pxe_serialize_play_time_update(struct pxe_pool* pool,
                                                        u64 world_age,
                                                        u64 time);

// 0x56
struct pxe_buffer_chain* pxe_serialize_play_entity_teleport(
    struct pxe_pool* pool, pxe_entity_id eid, double x, double y, double z,
    float yaw, float pitch, bool32 on_ground);

#endif
