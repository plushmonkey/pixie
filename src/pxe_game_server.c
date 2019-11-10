#include "pxe_game_server.h"

#include "protocol/pxe_protocol_play.h"
#include "pxe_alloc.h"
#include "pxe_buffer.h"
#include "pxe_nbt.h"
#include "pxe_varint.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <time.h>
#endif

#define PXE_READ_BUFFER_SIZE 64
#define PXE_OUTPUT_CONNECTIONS 0

typedef enum {
  PXE_PROCESS_RESULT_CONTINUE,
  PXE_PROCESS_RESULT_CONSUMED,
  PXE_PROCESS_RESULT_DESTROY,
} pxe_process_result;

static const char pxe_ping_response[] =
    "{\"version\": { \"name\": \"1.14.4\", \"protocol\": 498 }, \"players\": "
    "{\"max\": 420, \"online\": 69, \"sample\": [{\"name\": \"plushmonkey\", "
    "\"id\": \"e812180e-a8aa-4c9f-a8b3-07f591b8de20\"}]}, \"description\": "
    "{\"text\": \"pixie server\"}}";
static const char pxe_login_response[] =
    "{\"text\": \"pixie server has no implemented game server.\"}";

static u32 chunk_data[16][16][16];
static const u32 pxe_chunk_palette[] = {0, 33, 9, 10, 1, 14, 15};

void pxe_send_packet(pxe_socket* socket, pxe_memory_arena* arena, int packet_id,
                     pxe_buffer* buffer) {
  if (buffer == NULL) {
    fprintf(stderr, "Packet %d was null when sending.\n", packet_id);
    return;
  }

  size_t size = buffer->size;
  size_t id_size = pxe_varint_size(packet_id);
  size_t length_size = pxe_varint_size((i32)(size + id_size));
  char* pkt = pxe_arena_alloc(arena, length_size + id_size + size);

  size_t index = 0;

  index += pxe_varint_write((i32)(size + id_size), pkt + index);
  index += pxe_varint_write(packet_id, pkt + index);

  char* payload = pkt + index;

  for (size_t i = 0; i < size; ++i) {
    payload[i] = buffer->data[i];
  }

  index += size;

  pxe_socket_send(socket, pkt, index);
}

void pxe_game_server_wsa_poll(pxe_game_server* game_server,
                              pxe_socket* listen_socket,
                              pxe_memory_arena* perm_arena,
                              pxe_memory_arena* trans_arena);

void pxe_game_server_epoll(pxe_game_server* game_server,
                           pxe_socket* listen_socket,
                           pxe_memory_arena* perm_arena,
                           pxe_memory_arena* trans_arena);
i64 pxe_get_time_ms() {
#ifdef _WIN32
  return GetTickCount64();
#else
  struct timespec time;
  clock_gettime(CLOCK_MONOTONIC, &time);

  return time.tv_sec * 1000 + (time.tv_nsec / 1.0e6);
#endif
}

void pxe_strcpy(char* dest, char* src) {
  while (*src) {
    *dest++ = *src++;
  }
  *dest = *src;
}

pxe_buffer_chain* pxe_game_get_read_buffer_chain(pxe_game_server* server,
                                                 pxe_memory_arena* perm_arena) {
  if (server->free_buffers == NULL) {
    pxe_buffer_chain* chain = pxe_arena_push_type(perm_arena, pxe_buffer_chain);
    pxe_buffer* buffer = pxe_arena_push_type(perm_arena, pxe_buffer);
    u8* data = pxe_arena_alloc(perm_arena, PXE_READ_BUFFER_SIZE);

    buffer->data = data;
    buffer->size = 0;

    chain->buffer = buffer;
    chain->next = NULL;

    return chain;
  }

  pxe_buffer_chain* head = server->free_buffers;

  server->free_buffers = head->next;
  head->next = NULL;

  return head;
}

bool32 pxe_game_create_heightmap_nbt(pxe_nbt_tag_compound** root,
                                     pxe_memory_arena* trans_arena) {
  static const char motion_blocking[] = "MOTION_BLOCKING";

  pxe_nbt_tag_compound* compound =
      pxe_arena_push_type(trans_arena, pxe_nbt_tag_compound);

  compound->name = NULL;
  compound->name_length = 0;
  compound->ntags = 0;

  pxe_nbt_tag heightmap;

  heightmap.name = (char*)motion_blocking;
  heightmap.name_length = pxe_array_string_size(motion_blocking);
  heightmap.type = PXE_NBT_TAG_TYPE_LONG_ARRAY;

  pxe_nbt_tag_long_array* heightmap_array_tag =
      pxe_arena_push_type(trans_arena, pxe_nbt_tag_long_array);

  heightmap_array_tag->length = 36;
  heightmap_array_tag->data =
      pxe_arena_push_type_count(trans_arena, i64, heightmap_array_tag->length);

  for (size_t i = 0; i < heightmap_array_tag->length; ++i) {
    u64* long_data = (u64*)heightmap_array_tag->data + i;

    *long_data = 0;
  }

  heightmap.tag = heightmap_array_tag;

  pxe_nbt_tag_compound_add(compound, heightmap);

  *root = compound;

  return 1;
}

bool32 pxe_game_encode_chunk_data(pxe_memory_arena* trans_arena, u64** data,
                                  size_t* chunk_data_size) {
  // TODO: variable bits_per_block
  size_t bits_per_block = 8;
  size_t encoded_size = ((16 * 16 * 16) / 8) * bits_per_block;
  size_t required_segments = encoded_size / sizeof(u64);

  u64* encoded = pxe_arena_alloc(trans_arena, required_segments * sizeof(u64));

  // Clear the segments before writing chunk data into them.
  for (size_t i = 0; i < required_segments; ++i) {
    u64* segment = encoded + i;
    *segment = 0;
  }

  size_t bit_index = 0;

  for (size_t y = 0; y < 16; ++y) {
    for (size_t z = 0; z < 16; ++z) {
      for (size_t x = 0; x < 16; ++x) {
        u32 full_block_data = chunk_data[y][z][x];
        size_t index = bit_index / (sizeof(u64) * 8);
        size_t offset = bit_index % (sizeof(u64) * 8);

        u64* encoded_segment = encoded + index;

        *encoded_segment |= ((u64)full_block_data << (u64)offset);

        bit_index += bits_per_block;
      }
    }
  }

  *data = encoded;
  *chunk_data_size = required_segments * sizeof(u64);

  return 1;
}

bool32 pxe_game_create_palette(pxe_memory_arena* trans_arena, u8** palette_data,
                               size_t* palette_size) {
  size_t palette_length = pxe_array_size(pxe_chunk_palette);

  size_t size = pxe_varint_size((i32)palette_length);

  for (size_t i = 0; i < palette_length; ++i) {
    size += pxe_varint_size((i32)pxe_chunk_palette[i]);
  }

  pxe_buffer* buffer = pxe_arena_push_type(trans_arena, pxe_buffer);
  u8* payload = pxe_arena_alloc(trans_arena, size);

  pxe_buffer_writer writer;
  writer.buffer = buffer;
  writer.buffer->data = payload;
  writer.buffer->size = size;
  writer.write_pos = 0;

  if (pxe_buffer_write_varint(&writer, (i32)palette_length) == 0) {
    return 0;
  }

  for (size_t i = 0; i < palette_length; ++i) {
    if (pxe_buffer_write_varint(&writer, (i32)pxe_chunk_palette[i]) == 0) {
      return 0;
    }
  }

  *palette_data = payload;
  *palette_size = writer.buffer->size;

  return 1;
}

bool32 pxe_game_create_chunk_section(pxe_memory_arena* trans_arena,
                                     u8** chunk_section,
                                     size_t* chunk_section_size) {
  u8* palette_data;
  size_t palette_size;

  if (pxe_game_create_palette(trans_arena, &palette_data, &palette_size) == 0) {
    return 0;
  }

  u64* encoded_data;
  size_t encoded_data_size;

  if (pxe_game_encode_chunk_data(trans_arena, &encoded_data,
                                 &encoded_data_size) == 0) {
    return 0;
  }

  u8 bits_per_block = 8;

  size_t encoded_count = encoded_data_size / sizeof(u64);

  size_t size = sizeof(u16) + sizeof(u8) + palette_size +
                pxe_varint_size((i32)encoded_count) + encoded_data_size;
  pxe_buffer* buffer = pxe_arena_push_type(trans_arena, pxe_buffer);
  u8* payload = pxe_arena_alloc(trans_arena, size);

  pxe_buffer_writer writer;
  writer.buffer = buffer;
  writer.buffer->data = payload;
  writer.buffer->size = size;
  writer.write_pos = 0;

  if (pxe_buffer_write_u16(&writer, 16 * 16) == 0) {
    return 0;
  }

  if (pxe_buffer_write_u8(&writer, bits_per_block) == 0) {
    return 0;
  }

  if (pxe_buffer_write_raw_string(&writer, (char*)palette_data, palette_size) ==
      0) {
    return 0;
  }

  if (pxe_buffer_write_varint(&writer, (i32)encoded_count) == 0) {
    return 0;
  }

  if (pxe_buffer_write_raw_string(&writer, (char*)encoded_data,
                                  encoded_data_size) == 0) {
    return 0;
  }

  *chunk_section = payload;
  *chunk_section_size = buffer->size;

  return 1;
}

// TODO: This should probably be generated with buffer_chains to minimize
// copying
bool32 pxe_game_send_chunk_data(pxe_session* session,
                                pxe_memory_arena* trans_arena, i32 chunk_x,
                                i32 chunk_z, bool32 blank) {
  char* heightmap_data = NULL;
  size_t heightmap_size = 0;

  bool32 full_chunk = 1;
  i32 bitmask = 0x1F;

  if (blank) {
    bitmask = 0;
  }

  pxe_nbt_tag_compound* heightmap;

  if (pxe_game_create_heightmap_nbt(&heightmap, trans_arena) == 0) {
    return 0;
  }

  if (pxe_nbt_write(heightmap, trans_arena, &heightmap_data, &heightmap_size) ==
      0) {
    return 0;
  }

  u8* chunk_section;
  size_t chunk_section_size;
  if (pxe_game_create_chunk_section(trans_arena, &chunk_section,
                                    &chunk_section_size) == 0) {
    return 0;
  }

  i32 block_entity_count = 0;

  size_t chunk_section_count = pxe_bitset_count(bitmask);
  size_t total_chunk_data_size =
      chunk_section_size * chunk_section_count + sizeof(u32) * 256;
  size_t size =
      sizeof(u32) + sizeof(u32) + sizeof(u8) + pxe_varint_size(bitmask);
  size += heightmap_size;
  size += pxe_varint_size((i32)total_chunk_data_size);
  size += total_chunk_data_size;
  size += pxe_varint_size(block_entity_count);

  pxe_buffer* buffer = pxe_arena_push_type(trans_arena, pxe_buffer);
  u8* payload = pxe_arena_alloc(trans_arena, size);

  pxe_buffer_writer writer;
  writer.buffer = buffer;
  writer.buffer->data = payload;
  writer.buffer->size = size;
  writer.write_pos = 0;

  if (pxe_buffer_write_u32(&writer, chunk_x) == 0) {
    return 0;
  }

  if (pxe_buffer_write_u32(&writer, chunk_z) == 0) {
    return 0;
  }

  if (pxe_buffer_write_u8(&writer, (u8)full_chunk) == 0) {
    return 0;
  }

  if (pxe_buffer_write_varint(&writer, bitmask) == 0) {
    return 0;
  }

  if (pxe_buffer_write_raw_string(&writer, heightmap_data, heightmap_size) ==
      0) {
    return 0;
  }

  if (pxe_buffer_write_varint(&writer, (i32)total_chunk_data_size) == 0) {
    return 0;
  }

  for (size_t i = 0; i < chunk_section_count; ++i) {
    if (pxe_buffer_write_raw_string(&writer, (char*)chunk_section,
                                    chunk_section_size) == 0) {
      return 0;
    }
  }

  for (size_t i = 0; i < 256; ++i) {
    if (pxe_buffer_write_u32(&writer, 0) == 0) {
      return 0;
    }
  }

  if (pxe_buffer_write_varint(&writer, block_entity_count) == 0) {
    return 0;
  }

  pxe_send_packet(&session->socket, trans_arena,
                  PXE_PROTOCOL_OUTBOUND_PLAY_CHUNK_DATA, buffer);

  return 1;
}

bool32 pxe_game_send_position_and_look(pxe_session* session,
                                       pxe_memory_arena* trans_arena, double x,
                                       double y, double z) {
  static i32 next_teleport_id = 1;

  pxe_buffer* buffer = pxe_serialize_play_position_and_look(
      trans_arena, x, y, z, 0.0f, 0.0f, 0, next_teleport_id++);

  pxe_send_packet(&session->socket, trans_arena,
                  PXE_PROTOCOL_OUTBOUND_PLAY_PLAYER_POSITION_AND_LOOK, buffer);

  return 1;
}

bool32 pxe_game_send_keep_alive_packet(pxe_session* session,
                                       pxe_memory_arena* trans_arena, i64 id) {
  pxe_buffer* buffer = pxe_serialize_play_keep_alive(trans_arena, id);

  pxe_send_packet(&session->socket, trans_arena,
                  PXE_PROTOCOL_OUTBOUND_PLAY_KEEP_ALIVE, buffer);

  return 1;
}

bool32 pxe_game_send_player_abilities(pxe_session* session,
                                      pxe_memory_arena* trans_arena, u8 flags,
                                      float fly_speed, float fov) {
  pxe_buffer* buffer =
      pxe_serialize_play_player_abilities(trans_arena, flags, fly_speed, fov);

  pxe_send_packet(&session->socket, trans_arena,
                  PXE_PROTOCOL_OUTBOUND_PLAY_PLAYER_ABILITIES, buffer);

  return 1;
}

bool32 pxe_game_send_join_packet(pxe_session* session,
                                 pxe_memory_arena* trans_arena) {
  pxe_buffer* buffer = pxe_serialize_play_join_game(
      trans_arena, session->entity_id, 0, 0, "default", 16, 0);

  pxe_send_packet(&session->socket, trans_arena,
                  PXE_PROTOCOL_OUTBOUND_PLAY_JOIN_GAME, buffer);

  return 1;
}

bool32 pxe_game_broadcast_spawn_player(pxe_game_server* server,
                                       pxe_session* session,
                                       pxe_memory_arena* trans_arena) {
  pxe_buffer* buffer = pxe_serialize_play_spawn_player(
      trans_arena, session->entity_id, &session->uuid, session->x, session->y,
      session->z, 0.0f, 0.0f);

  for (size_t i = 0; i < server->session_count; ++i) {
    pxe_session* target_session = server->sessions + i;

    if (target_session == session) continue;
    if (target_session->protocol_state != PXE_PROTOCOL_STATE_PLAY) continue;

    pxe_send_packet(&target_session->socket, trans_arena,
                    PXE_PROTOCOL_OUTBOUND_PLAY_SPAWN_PLAYER, buffer);
  }

  return 1;
}

bool32 pxe_game_broadcast_player_move(pxe_game_server* server,
                                      pxe_session* session,
                                      pxe_memory_arena* trans_arena) {
  double delta_x = session->x - session->previous_x;
  double delta_y = session->y - session->previous_y;
  double delta_z = session->z - session->previous_z;

  pxe_buffer* buffer = pxe_serialize_play_entity_look_and_relative_move(
      trans_arena, session->entity_id, delta_x, delta_y, delta_z, session->yaw,
      session->pitch, session->on_ground);

  pxe_buffer* look_buffer = pxe_serialize_play_entity_head_look(trans_arena, session->entity_id, session->yaw);

  for (size_t i = 0; i < server->session_count; ++i) {
    pxe_session* target_session = server->sessions + i;

    if (target_session == session) continue;
    if (target_session->protocol_state != PXE_PROTOCOL_STATE_PLAY) continue;

    pxe_send_packet(&target_session->socket, trans_arena,
                    PXE_PROTOCOL_OUTBOUND_PLAY_ENTITY_LOOK_AND_RELATIVE_MOVE, buffer);

    pxe_send_packet(&target_session->socket, trans_arena,
      PXE_PROTOCOL_OUTBOUND_PLAY_ENTITY_HEAD_LOOK, look_buffer);
  }

  return 1;
}

bool32 pxe_game_broadcast_existing_players(pxe_game_server* server,
                                           pxe_session* session,
                                           pxe_memory_arena* trans_arena) {
  for (size_t i = 0; i < server->session_count; ++i) {
    pxe_session* target_session = server->sessions + i;

    if (target_session == session) continue;
    if (target_session->protocol_state != PXE_PROTOCOL_STATE_PLAY) continue;

    pxe_buffer* buffer = pxe_serialize_play_spawn_player(
        trans_arena, target_session->entity_id, &target_session->uuid,
        target_session->x, target_session->y, target_session->z, 0.0f, 0.0f);

    pxe_send_packet(&session->socket, trans_arena,
                    PXE_PROTOCOL_OUTBOUND_PLAY_SPAWN_PLAYER, buffer);
  }

  return 1;
}

bool32 pxe_game_send_existing_player_info(pxe_game_server* server,
                                          pxe_session* session,
                                          pxe_memory_arena* trans_arena) {
  pxe_player_info* infos = pxe_arena_alloc(trans_arena, 0);
  size_t info_count = 0;

  for (size_t i = 0; i < server->session_count; ++i) {
    pxe_session* existing_session = server->sessions + i;

    if (existing_session->protocol_state != PXE_PROTOCOL_STATE_PLAY) {
      continue;
    }

    pxe_player_info* info = pxe_arena_push_type(trans_arena, pxe_player_info);

    memset(info, 0, sizeof(pxe_player_info));
    info->uuid = existing_session->uuid;

    pxe_strcpy(info->add.name, existing_session->username);
    ++info_count;
  }

  pxe_buffer* buffer = pxe_serialize_play_player_info(
      trans_arena, PXE_PLAYER_INFO_ADD, infos, info_count);

  for (size_t i = 0; i < server->session_count; ++i) {
    pxe_session* target_session = server->sessions + i;

    if (target_session->protocol_state != PXE_PROTOCOL_STATE_PLAY) continue;

    pxe_send_packet(&target_session->socket, trans_arena,
                    PXE_PROTOCOL_OUTBOUND_PLAY_PLAYER_INFO, buffer);
  }

  return 1;
}

bool32 pxe_game_broadcast_destroy_entity(pxe_game_server* server,
                                         pxe_entity_id eid,
                                         pxe_memory_arena* trans_arena) {
  pxe_buffer* buffer =
      pxe_serialize_play_destroy_entities(trans_arena, &eid, 1);

  for (size_t i = 0; i < server->session_count; ++i) {
    pxe_session* target_session = server->sessions + i;

    if (target_session->protocol_state != PXE_PROTOCOL_STATE_PLAY) continue;

    pxe_send_packet(&target_session->socket, trans_arena,
                    PXE_PROTOCOL_OUTBOUND_PLAY_DESTROY_ENTITIES, buffer);
  }

  return 1;
}

bool32 pxe_game_broadcast_player_info(pxe_game_server* server,
                                      pxe_session* session,
                                      pxe_player_info_action action,
                                      pxe_memory_arena* trans_arena) {
  pxe_player_info info = {0};

  info.uuid = session->uuid;

  if (action == PXE_PLAYER_INFO_ADD) {
    pxe_strcpy(info.add.name, session->username);
  }

  pxe_buffer* buffer =
      pxe_serialize_play_player_info(trans_arena, action, &info, 1);

  for (size_t i = 0; i < server->session_count; ++i) {
    pxe_session* target_session = server->sessions + i;

    if (target_session == session) continue;
    if (target_session->protocol_state != PXE_PROTOCOL_STATE_PLAY) continue;

    pxe_send_packet(&target_session->socket, trans_arena,
                    PXE_PROTOCOL_OUTBOUND_PLAY_PLAYER_INFO, buffer);
  }

  return 1;
}

bool32 pxe_game_server_send_chat(pxe_game_server* server,
                                 pxe_memory_arena* trans_arena, char* message,
                                 size_t message_len, char* color) {
  pxe_buffer* buffer =
      pxe_serialize_play_chat(trans_arena, message, message_len, color);

  for (size_t i = 0; i < server->session_count; ++i) {
    pxe_session* session = server->sessions + i;

    if (session->protocol_state != PXE_PROTOCOL_STATE_PLAY) continue;

    pxe_send_packet(&session->socket, trans_arena,
                    PXE_PROTOCOL_OUTBOUND_PLAY_CHAT, buffer);
  }

  return 1;
}

pxe_process_result pxe_game_process_session(pxe_game_server* game_server,
                                            pxe_session* session,
                                            pxe_memory_arena* trans_arena) {
  pxe_buffer_chain_reader* reader = &session->buffer_reader;

  session->buffer_reader.chain = session->process_buffer_chain;

  i32 pkt_len, pkt_id;
  size_t pkt_len_size = 0;

  if (pxe_buffer_chain_read_varint(reader, &pkt_len) == 0) {
    return PXE_PROCESS_RESULT_CONSUMED;
  }

  pkt_len_size = pxe_varint_size(pkt_len);

  if (pxe_buffer_chain_read_varint(reader, &pkt_id) == 0) {
    return PXE_PROCESS_RESULT_CONSUMED;
  }

  if (session->protocol_state == PXE_PROTOCOL_STATE_HANDSHAKING) {
    switch (pkt_id) {
      case PXE_PROTOCOL_INBOUND_HANDSHAKING_HANDSHAKE: {
        i32 version;

        if (pxe_buffer_chain_read_varint(reader, &version) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        size_t hostname_len;

        if (pxe_buffer_chain_read_length_string(reader, NULL, &hostname_len) ==
            0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        char* hostname = pxe_arena_alloc(trans_arena, hostname_len);

        if (pxe_buffer_chain_read_length_string(reader, hostname,
                                                &hostname_len) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        u16 port;
        if (pxe_buffer_chain_read_u16(reader, &port) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        i32 next_state;
        if (pxe_buffer_chain_read_varint(reader, &next_state) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (next_state >= PXE_PROTOCOL_STATE_COUNT) {
          printf("Illegal state: %d. Terminating connection.\n", next_state);
          return PXE_PROCESS_RESULT_DESTROY;
        }

        session->protocol_state = next_state;
      } break;
      default: {
        printf("Illegal packet %d received in handshaking state.\n", pkt_id);
        return PXE_PROCESS_RESULT_DESTROY;
      }
    }
  } else if (session->protocol_state == PXE_PROTOCOL_STATE_STATUS) {
    switch (pkt_id) {
      case PXE_PROTOCOL_INBOUND_STATUS_REQUEST: {
        size_t data_size = pxe_array_size(pxe_ping_response);
        size_t response_size = pxe_varint_size((i32)data_size) + data_size;
        char* response_str = pxe_arena_alloc(trans_arena, response_size);

        pxe_buffer_writer writer;
        pxe_buffer buffer;
        buffer.data = (u8*)response_str;
        buffer.size = response_size;
        writer.buffer = &buffer;
        writer.write_pos = 0;

        pxe_buffer_write_length_string(&writer, pxe_ping_response, data_size);

        pxe_send_packet(&session->socket, trans_arena,
                        PXE_PROTOCOL_OUTBOUND_STATUS_RESPONSE, &buffer);
      } break;
      case PXE_PROTOCOL_INBOUND_STATUS_PING: {
        // Respond to the game with the same payload.
        u64 payload;
        if (pxe_buffer_chain_read_u64(reader, &payload) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        pxe_buffer buffer;
        buffer.data = (u8*)&payload;
        buffer.size = sizeof(u64);

        pxe_send_packet(&session->socket, trans_arena,
                        PXE_PROTOCOL_OUTBOUND_STATUS_PONG, &buffer);
      } break;
      default: {
        printf("Illegal packet %d received in status state.\n", pkt_id);
        return PXE_PROCESS_RESULT_DESTROY;
      }
    }
  } else if (session->protocol_state == PXE_PROTOCOL_STATE_LOGIN) {
    switch (pkt_id) {
      case PXE_PROTOCOL_INBOUND_LOGIN_START: {
        size_t username_len;
        if (pxe_buffer_chain_read_length_string(reader, NULL, &username_len) ==
            0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        char* username = pxe_arena_alloc(trans_arena, username_len + 1);
        if (pxe_buffer_chain_read_length_string(reader, username,
                                                &username_len) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        username[username_len] = 0;

        if (username_len > 16) {
          fprintf(stderr, "Illegal username: %.20s\n", username);
          return PXE_PROCESS_RESULT_DESTROY;
        }

        pxe_strcpy(session->username, username);

        char uuid[37];
        session->uuid = pxe_uuid_random();

        pxe_uuid_to_string(&session->uuid, uuid, 1);

        printf("%s uuid: %s\n", session->username, uuid);

        size_t response_size = pxe_varint_size((i32)username_len) +
                               username_len + pxe_varint_size(36) + 36;
        char* response = pxe_arena_alloc(trans_arena, response_size);

        pxe_buffer_writer writer;
        pxe_buffer buffer;
        buffer.data = (u8*)response;
        buffer.size = response_size;
        writer.buffer = &buffer;
        writer.write_pos = 0;

        pxe_buffer_write_length_string(&writer, uuid, 36);
        pxe_buffer_write_length_string(&writer, session->username,
                                       username_len);

        pxe_send_packet(&session->socket, trans_arena,
                        PXE_PROTOCOL_OUTBOUND_LOGIN_SUCCESS, &buffer);

        session->entity_id = game_server->next_entity_id++;
        session->previous_x = session->x = 0;
        session->previous_y = session->y = 68;
        session->previous_z = session->z = 0;

        if (!pxe_game_send_join_packet(session, trans_arena)) {
          fprintf(stderr, "Error writing join packet.\n");
        } else {
          printf("Sent join packet to %s\n", username);
        }

        session->protocol_state = PXE_PROTOCOL_STATE_PLAY;

        char join_message[512];
        size_t join_message_len =
            sprintf_s(join_message, pxe_array_size(join_message),
                      "%s joined the server.", session->username);
        pxe_game_server_send_chat(game_server, trans_arena, join_message,
                                  join_message_len, "dark_aqua");

        if (pxe_game_send_player_abilities(session, trans_arena, 0x04, 0.05f,
                                           0.1f) == 0) {
          printf("Failed to send player abilities packet.\n");
        }

        if (pxe_game_send_position_and_look(session, trans_arena, session->x,
                                            session->y, session->z) == 0) {
          fprintf(stderr, "Failed to send position\n");
        }

        if (pxe_game_send_existing_player_info(game_server, session,
                                               trans_arena) == 0) {
          fprintf(stderr, "Failed to send existing player info.\n");
        }

        if (pxe_game_broadcast_player_info(
                game_server, session, PXE_PLAYER_INFO_ADD, trans_arena) == 0) {
          fprintf(stderr, "Failed to broadcast player info.\n");
        }

        if (!pxe_game_broadcast_spawn_player(game_server, session,
                                             trans_arena)) {
          fprintf(stderr, "Error broadcasting spawn player packet.\n");
        }

        if (!pxe_game_broadcast_existing_players(game_server, session,
                                                 trans_arena)) {
          fprintf(stderr, "Error broadcasting existing players packet.\n");
        }

        // Send terrain
        for (i32 z = -5; z < 6; ++z) {
          for (i32 x = -5; x < 6; ++x) {
            bool32 blank = 0;

            float r = (float)(x * x + z * z);
            if (r > 3.5f * 3.5f) {
              blank = 1;
            }

            if (pxe_game_send_chunk_data(session, trans_arena, x, z, blank) ==
                0) {
              fprintf(stderr, "Failed to send chunk data\n");
            }
          }
        }
      } break;
      default: {
        fprintf(stderr, "Received unhandled packet %d in state %d\n", pkt_id,
                session->protocol_state);
        return PXE_PROCESS_RESULT_DESTROY;
      }
    }
  } else if (session->protocol_state == PXE_PROTOCOL_STATE_PLAY) {
    switch (pkt_id) {
      case PXE_PROTOCOL_INBOUND_PLAY_TELEPORT_CONFIRM: {
        i32 teleport_id;

        if (pxe_buffer_chain_read_varint(reader, &teleport_id) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }
      } break;
      case PXE_PROTOCOL_INBOUND_PLAY_CHAT: {
        size_t message_len;
        if (pxe_buffer_chain_read_length_string(reader, NULL, &message_len) ==
            0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        char* message = pxe_arena_alloc(trans_arena, message_len + 1);
        if (pxe_buffer_chain_read_length_string(reader, message,
                                                &message_len) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        message[message_len] = 0;

        if (message_len > 0 && message[0] == '/') {
          if (strcmp(message, "/spawn") == 0) {
            if (pxe_game_send_position_and_look(session, trans_arena, 5.0f,
                                                68.0f, 5.0f) == 0) {
              fprintf(stderr, "Failed to send position\n");
            }
          }
        } else {
          char output_message[512];

          size_t output_message_len =
              sprintf_s(output_message, pxe_array_size(output_message),
                        "%s> %s", session->username, message);

          pxe_game_server_send_chat(game_server, trans_arena, output_message,
                                    output_message_len, "white");
        }
      } break;
      case PXE_PROTOCOL_INBOUND_PLAY_CLIENT_SETTINGS: {
        size_t locale_len;
        if (pxe_buffer_chain_read_length_string(reader, NULL, &locale_len) ==
            0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        char* locale = pxe_arena_alloc(trans_arena, locale_len + 1);
        if (pxe_buffer_chain_read_length_string(reader, locale, &locale_len) ==
            0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        locale[locale_len] = 0;

        u8 view_distance;
        if (pxe_buffer_chain_read_u8(reader, &view_distance) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        i32 chat_mode;
        if (pxe_buffer_chain_read_varint(reader, &chat_mode) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        bool32 chat_colors = 0;
        if (pxe_buffer_chain_read_u8(reader, (u8*)&chat_colors) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        u8 skin_parts;
        if (pxe_buffer_chain_read_u8(reader, &skin_parts) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        i32 main_hand;
        if (pxe_buffer_chain_read_varint(reader, &main_hand) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        printf("Received client settings from %s.\n", session->username);
      } break;
      case PXE_PROTOCOL_INBOUND_PLAY_PLUGIN_MESSAGE: {
        size_t channel_len;
        if (pxe_buffer_chain_read_length_string(reader, NULL, &channel_len) ==
            0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        char* channel = pxe_arena_alloc(trans_arena, channel_len + 1);

        if (pxe_buffer_chain_read_length_string(reader, channel,
                                                &channel_len) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        channel[channel_len] = 0;

        size_t message_size;

        if (pxe_buffer_chain_read_length_string(reader, NULL, &message_size) ==
            0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        char* plugin_message = pxe_arena_alloc(trans_arena, message_size + 1);
        if (pxe_buffer_chain_read_length_string(reader, plugin_message,
                                                &message_size) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }
        plugin_message[message_size] = 0;

        printf("plugin message from %s: (%s, %s)\n", session->username, channel,
               plugin_message);

#if 0
        return PXE_PROCESS_RESULT_DESTROY;
#endif
      } break;
      case PXE_PROTOCOL_INBOUND_PLAY_PLAYER_POSITION: {
        double x;
        double y;
        double z;
        bool32 on_ground;

        if (pxe_buffer_chain_read_double(reader, &x) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (pxe_buffer_chain_read_double(reader, &y) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (pxe_buffer_chain_read_double(reader, &z) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (pxe_buffer_chain_read_u8(reader, (u8*)&on_ground) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        // TODO: validate inputs
        session->x = x;
        session->y = y;
        session->z = z;
        session->on_ground = on_ground;

      } break;
      case PXE_PROTOCOL_INBOUND_PLAY_PLAYER_POSITION_AND_LOOK: {
        double x;
        double y;
        double z;
        float yaw;
        float pitch;
        bool32 on_ground;

        if (pxe_buffer_chain_read_double(reader, &x) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (pxe_buffer_chain_read_double(reader, &y) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (pxe_buffer_chain_read_double(reader, &z) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (pxe_buffer_chain_read_float(reader, &yaw) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (pxe_buffer_chain_read_float(reader, &pitch) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (pxe_buffer_chain_read_u8(reader, (u8*)&on_ground) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        // TODO: validate inputs
        session->x = x;
        session->y = y;
        session->z = z;
        session->yaw = yaw;
        session->pitch = pitch;
        session->on_ground = on_ground;

      } break;
      case PXE_PROTOCOL_INBOUND_PLAY_PLAYER_LOOK: {
        float yaw;
        float pitch;
        bool32 on_ground;

        if (pxe_buffer_chain_read_float(reader, &yaw) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (pxe_buffer_chain_read_float(reader, &pitch) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (pxe_buffer_chain_read_u8(reader, (u8*)&on_ground) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        session->yaw = yaw;
        session->pitch = pitch;
        session->on_ground = on_ground;

      } break;
      default: {
#if 1
        fprintf(stderr, "Received unhandled packet %d in state %d\n", pkt_id,
                session->protocol_state);
#endif

#if 1
        size_t payload_size = pkt_len - pxe_varint_size(pkt_id);
        size_t buffer_size = pxe_buffer_chain_size(reader->chain);
        if (buffer_size >= reader->read_pos + payload_size) {
          // Skip over this packet
          reader->read_pos += payload_size;
        } else {
          return PXE_PROCESS_RESULT_CONSUMED;
        }
#else
        return PXE_PROCESS_RESULT_DESTROY;
#endif
      }
    }

  } else {
    printf("Unhandled protocol state\n");
    return PXE_PROCESS_RESULT_DESTROY;
  }

  pxe_buffer_chain* current = session->process_buffer_chain;

  while (current && reader->read_pos >= current->buffer->size) {
    size_t current_size = current->buffer->size;

    pxe_buffer_chain* next = current->next;

    pxe_game_free_buffer_chain(game_server, current);

    current = next;

    reader->read_pos -= current_size;
  }

  session->process_buffer_chain = current;

  if (current == NULL) {
    session->last_buffer_chain = NULL;
    return PXE_PROCESS_RESULT_CONSUMED;
  }

  return PXE_PROCESS_RESULT_CONTINUE;
}

pxe_game_server* pxe_game_server_create(pxe_memory_arena* perm_arena) {
  for (size_t z = 0; z < 16; ++z) {
    for (size_t x = 0; x < 16; ++x) {
      chunk_data[0][z][x] = 1;
    }
  }
  for (size_t z = 0; z < 16; ++z) {
    for (size_t x = 0; x < 16; ++x) {
      size_t palette_size = pxe_array_size(pxe_chunk_palette);

      chunk_data[1][z][x] = (rand() % (palette_size - 2)) + 2;
    }
  }

  pxe_socket listen_socket = {0};

  if (pxe_socket_listen(&listen_socket, "127.0.0.1", 25565) == 0) {
    fprintf(stderr, "Failed to listen with socket.\n");
    return NULL;
  }

  pxe_socket_set_block(&listen_socket, 0);

  pxe_game_server* game_server =
      pxe_arena_push_type(perm_arena, pxe_game_server);

  game_server->session_count = 0;
  game_server->listen_socket = listen_socket;
  game_server->free_buffers = NULL;
  game_server->next_entity_id = 0;

#ifndef _WIN32
  game_server->epollfd = epoll_create1(0);
  if (game_server->epollfd == -1) {
    fprintf(stderr, "Failed to create epoll fd.\n");
    return NULL;
  }
#else
  game_server->nevents = 0;
#endif

  for (size_t i = 0; i < PXE_GAME_SERVER_MAX_SESSIONS; ++i) {
    game_server->sessions[i].buffer_reader.read_pos = 0;
  }

  return game_server;
}

bool32 pxe_game_server_read_session(pxe_game_server* game_server,
                                    pxe_memory_arena* perm_arena,
                                    pxe_memory_arena* trans_arena,
                                    pxe_session* session) {
  pxe_socket* socket = &session->socket;
  pxe_buffer_chain* buffer_chain =
      pxe_game_get_read_buffer_chain(game_server, perm_arena);
  pxe_buffer* buffer = buffer_chain->buffer;

  buffer->size =
      pxe_socket_receive(socket, (char*)buffer->data, PXE_READ_BUFFER_SIZE);

  if (session->process_buffer_chain == NULL) {
    session->process_buffer_chain = buffer_chain;
    session->last_buffer_chain = buffer_chain;
  } else {
    session->last_buffer_chain->next = buffer_chain;
    session->last_buffer_chain = buffer_chain;
  }

  if (socket->state != PXE_SOCKET_STATE_CONNECTED) {
    return 0;
  }

  pxe_process_result process_result = PXE_PROCESS_RESULT_CONTINUE;

  while (process_result == PXE_PROCESS_RESULT_CONTINUE) {
    size_t reader_pos_snapshot = session->buffer_reader.read_pos;

    process_result =
        pxe_game_process_session(game_server, session, trans_arena);

    if (process_result == PXE_PROCESS_RESULT_CONSUMED) {
      if (session->process_buffer_chain == NULL) {
        // Set the read position back to the beginning because the entire buffer
        // was processed.
        session->buffer_reader.read_pos = 0;
      } else {
        // Revert the read position because the last process didn't fully
        // read a packet.
        session->buffer_reader.read_pos = reader_pos_snapshot;
      }
    }
  }

  fflush(stdout);

  return process_result != PXE_PROCESS_RESULT_DESTROY;
}

void pxe_game_server_on_disconnect(pxe_game_server* server,
                                   pxe_session* session,
                                   pxe_memory_arena* arena) {
  if (pxe_game_broadcast_player_info(server, session, PXE_PLAYER_INFO_REMOVE,
                                     arena) == 0) {
    fprintf(stderr, "Failed to broadcast player info leave\n");
  }

  if (pxe_game_broadcast_destroy_entity(server, session->entity_id, arena) ==
      0) {
    fprintf(stderr, "Failed to broadcast entity destroy.\n");
  }
}

void pxe_game_server_tick(pxe_game_server* server, pxe_memory_arena* perm_arena,
                          pxe_memory_arena* trans_arena) {
  i64 current_time = pxe_get_time_ms();

  for (size_t i = 0; i < server->session_count; ++i) {
    pxe_session* session = server->sessions + i;

    if (session->protocol_state != PXE_PROTOCOL_STATE_PLAY) continue;

    if (current_time >= session->next_keep_alive) {
      pxe_game_send_keep_alive_packet(session, trans_arena, current_time);

      session->next_keep_alive = current_time + 10000;
    }

    if (current_time >= session->next_position_broadcast) {
      pxe_game_broadcast_player_move(server, session, trans_arena);

      session->previous_x = session->x;
      session->previous_y = session->y;
      session->previous_z = session->z;
      session->next_position_broadcast = current_time + 100;
    }
  }
}

void pxe_game_server_run(pxe_memory_arena* perm_arena,
                         pxe_memory_arena* trans_arena) {
  pxe_game_server* game_server = pxe_game_server_create(perm_arena);

  if (game_server == NULL) {
    fprintf(stderr, "Failed to create game server.\n");
    return;
  }

  struct timeval timeout = {0};

  printf("Listening for connections...\n");
  fflush(stdout);

  pxe_socket* listen_socket = &game_server->listen_socket;

#ifdef _WIN32
  game_server->events[0].fd = listen_socket->fd;
  game_server->events[0].events = POLLIN;
  game_server->events[0].revents = 0;
  game_server->nevents = 1;
#else
  game_server->events[0].events = EPOLLIN;
  game_server->events[0].data.u64 = PXE_GAME_SERVER_MAX_SESSIONS + 1;

  if (epoll_ctl(game_server->epollfd, EPOLL_CTL_ADD, listen_socket->fd,
                game_server->events)) {
    fprintf(stderr, "Failed to add listen socket to epoll.\n");
    return;
  }
#endif

  i64 last_tick_time = 0;

  while (listen_socket->state == PXE_SOCKET_STATE_LISTENING) {
#ifdef _WIN32
    pxe_game_server_wsa_poll(game_server, listen_socket, perm_arena,
                             trans_arena);
#else
    pxe_game_server_epoll(game_server, listen_socket, perm_arena, trans_arena);
#endif

    i64 current_time = pxe_get_time_ms();

    if (current_time > last_tick_time + 50) {
      pxe_game_server_tick(game_server, perm_arena, trans_arena);
      last_tick_time = current_time;
    }

    pxe_arena_reset(trans_arena);
  }
}

void pxe_game_server_wsa_poll(pxe_game_server* game_server,
                              pxe_socket* listen_socket,
                              pxe_memory_arena* perm_arena,
                              pxe_memory_arena* trans_arena) {
#ifdef _WIN32
  int wsa_result = WSAPoll(game_server->events, (ULONG)game_server->nevents, 0);

  if (wsa_result > 0) {
    if (game_server->events[0].revents != 0) {
      pxe_socket new_socket = {0};

      if (pxe_socket_accept(listen_socket, &new_socket)) {
#if PXE_OUTPUT_CONNECTIONS
        u8 bytes[] = ENDPOINT_BYTES(new_socket.endpoint);

        printf("Accepted %hhu.%hhu.%hhu.%hhu:%hu\n", bytes[0], bytes[1],
               bytes[2], bytes[3], new_socket.endpoint.sin_port);
#endif

        pxe_socket_set_block(&new_socket, 0);

        size_t index = game_server->session_count++;
        pxe_session* session = game_server->sessions + index;

        pxe_session_initialize(session);

        session->socket = new_socket;

        WSAPOLLFD* new_event = game_server->events + game_server->nevents++;

        new_event->fd = new_socket.fd;
        new_event->events = POLLIN;
        new_event->revents = 0;
      } else {
        fprintf(stderr, "Failed to accept new socket\n");
      }
    }

    for (size_t event_index = 1; event_index < game_server->nevents;) {
      if (game_server->events[event_index].revents != 0) {
        size_t session_index = event_index - 1;
        pxe_session* session = &game_server->sessions[session_index];

        if (pxe_game_server_read_session(game_server, perm_arena, trans_arena,
                                         session) == 0) {
          pxe_game_server_on_disconnect(game_server, session, trans_arena);

          pxe_session_free(session, game_server);
          pxe_socket_disconnect(&session->socket);

          // Swap the last session to the current position then decrement
          // session count so this session is removed.
          game_server->sessions[session_index] =
              game_server->sessions[--game_server->session_count];

          game_server->events[event_index] =
              game_server->events[--game_server->nevents];

#if PXE_OUTPUT_CONNECTIONS
          u8 bytes[] = ENDPOINT_BYTES(session->socket.endpoint);

          printf("%hhu.%hhu.%hhu.%hhu:%hu disconnected.\n", bytes[0], bytes[1],
                 bytes[2], bytes[3], session->socket.endpoint.sin_port);
#endif
          continue;
        }
      }

      ++event_index;
    }

    fflush(stdout);
  } else if (wsa_result < 0) {
    fprintf(stderr, "WSA error: %d", wsa_result);
  }

  fflush(stdout);
#endif
}

void pxe_game_server_epoll(pxe_game_server* game_server,
                           pxe_socket* listen_socket,
                           pxe_memory_arena* perm_arena,
                           pxe_memory_arena* trans_arena) {
#ifndef _WIN32
  int nfds = epoll_wait(game_server->epollfd, game_server->events,
                        PXE_GAME_SERVER_MAX_SESSIONS, 0);

  for (int event_index = 0; event_index < nfds; ++event_index) {
    struct epoll_event* event = game_server->events + event_index;

    if (event->data.u64 == PXE_GAME_SERVER_MAX_SESSIONS + 1) {
      pxe_socket new_socket = {0};

      if (pxe_socket_accept(listen_socket, &new_socket)) {
#if PXE_OUTPUT_CONNECTIONS
        u8 bytes[] = ENDPOINT_BYTES(new_socket.endpoint);

        printf("Accepted %hhu.%hhu.%hhu.%hhu:%hu\n", bytes[0], bytes[1],
               bytes[2], bytes[3], new_socket.endpoint.sin_port);
#endif

        pxe_socket_set_block(&new_socket, 0);

        size_t index = game_server->session_count++;
        pxe_session* session = game_server->sessions + index;

        pxe_session_initialize(session);

        session->socket = new_socket;

        struct epoll_event new_event = {0};

        new_event.events = EPOLLIN | EPOLLHUP;
        new_event.data.u64 = index;

        if (epoll_ctl(game_server->epollfd, EPOLL_CTL_ADD, new_socket.fd,
                      &new_event)) {
          fprintf(stderr, "Failed to add new socket to epoll.\n");
        }
      } else {
        fprintf(stderr, "Failed to accept new socket\n");
      }
    } else {
      size_t session_index = (size_t)event->data.u64;
      pxe_session* session = game_server->sessions + session_index;

      if (pxe_game_server_read_session(game_server, perm_arena, trans_arena,
                                       session) == 0) {
        pxe_game_server_on_disconnect(game_server, session, trans_arena);

        pxe_session_free(session, game_server);
        pxe_socket_disconnect(&session->socket);

#if PXE_OUTPUT_CONNECTIONS
        u8 bytes[] = ENDPOINT_BYTES(session->socket.endpoint);
        printf("%hhu.%hhu.%hhu.%hhu:%hu disconnected.\n", bytes[0], bytes[1],
               bytes[2], bytes[3], session->socket.endpoint.sin_port);
#endif

        // Swap the last session to the current position then decrement
        // session count so this session is removed.
        game_server->sessions[session_index] =
            game_server->sessions[--game_server->session_count];

        if (session_index < game_server->session_count) {
          struct epoll_event mod_event;
          mod_event.events = EPOLLIN | EPOLLHUP;
          mod_event.data.u64 = session_index;

          // The session pointer now points to the swapped session, so the event
          // should be modified to point to the new session index.
          if (epoll_ctl(game_server->epollfd, EPOLL_CTL_MOD, session->socket.fd,
                        &mod_event)) {
            fprintf(stderr, "Failed to modify event session index for fd %d.\n",
                    session->socket.fd);
          }
        }

        continue;
      }
    }
    fflush(stdout);
  }
#endif
}
