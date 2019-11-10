#ifndef PIXIE_PROTOCOL_PLAY_H_
#define PIXIE_PROTOCOL_PLAY_H_

#include "pxe_protocol.h"

#include "../pxe_uuid.h"

struct pxe_buffer;

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

// 0x03
struct pxe_buffer* pxe_serialize_play_chat(struct pxe_memory_arena* arena,
                                           char* message, size_t message_size,
                                           char* color);

// 0x05
struct pxe_buffer* pxe_serialize_play_spawn_player(
    struct pxe_memory_arena* arena, i32 eid, pxe_uuid* uuid, double x, double y,
    double z, float yaw, float pitch);

// 0x20
struct pxe_buffer* pxe_serialize_play_keep_alive(struct pxe_memory_arena* arena,
                                                 i64 id);

// 0x25
struct pxe_buffer* pxe_serialize_play_join_game(struct pxe_memory_arena* arena,
                                                i32 eid, u8 gamemode,
                                                i32 dimension, char* level_type,
                                                i32 view_distance,
                                                bool32 reduced_debug);

// 0x29
struct pxe_buffer* pxe_serialize_play_entity_look_and_relative_move(
    struct pxe_memory_arena* arena, pxe_entity_id eid, double delta_x,
    double delta_y, double delta_z, float yaw, float pitch, bool32 on_ground);

// 0x31
struct pxe_buffer* pxe_serialize_play_player_abilities(
    struct pxe_memory_arena* arena, u8 flags, float fly_speed, float fov);

// 0x33
struct pxe_buffer* pxe_serialize_play_player_info(
    struct pxe_memory_arena* arena, pxe_player_info_action action,
    pxe_player_info* info, size_t info_count);

// 0x35
struct pxe_buffer* pxe_serialize_play_position_and_look(
    struct pxe_memory_arena* arena, double x, double y, double z, float yaw,
    float pitch, u8 flags, i32 teleport_id);

// 0x37
struct pxe_buffer* pxe_serialize_play_destroy_entities(
    struct pxe_memory_arena* arena, pxe_entity_id* entities, size_t count);

// 0x3B
struct pxe_buffer* pxe_serialize_play_entity_head_look(
  struct pxe_memory_arena* arena, pxe_entity_id eid, float yaw);

#endif
