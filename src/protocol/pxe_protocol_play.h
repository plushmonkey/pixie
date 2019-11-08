#ifndef PIXIE_PROTOCOL_PLAY_H_
#define PIXIE_PROTOCOL_PLAY_H_

#include "pxe_protocol.h"

struct pxe_buffer;

// 0x03
struct pxe_buffer* pxe_serialize_play_chat(struct pxe_memory_arena* arena,
                                           char* message, size_t message_size,
                                           char* color);

// 0x20
struct pxe_buffer* pxe_serialize_play_keep_alive(
  struct pxe_memory_arena* arena, i64 id);

// 0x25
struct pxe_buffer* pxe_serialize_play_join_game(struct pxe_memory_arena* arena,
                                                i32 eid, u8 gamemode,
                                                i32 dimension, char* level_type,
                                                i32 view_distance,
                                                bool32 reduced_debug);
// 0x31
struct pxe_buffer* pxe_serialize_play_player_abilities(
    struct pxe_memory_arena* arena, u8 flags, float fly_speed, float fov);

// 0x35
struct pxe_buffer* pxe_serialize_play_position_and_look(
    struct pxe_memory_arena* arena, double x, double y, double z, float yaw,
    float pitch, u8 flags, i32 teleport_id);

#endif
