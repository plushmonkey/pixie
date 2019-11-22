#include "pxe_game_server.h"

#include "protocol/pxe_protocol_play.h"
#include "pxe_alloc.h"
#include "pxe_buffer.h"
#include "pxe_nbt.h"
#include "pxe_varint.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <time.h>
#endif

// Must be at least 8
#define PXE_READ_BUFFER_SIZE 64
#define PXE_WRITE_BUFFER_SIZE 512
#define PXE_OUTPUT_CONNECTIONS 0
#define PXE_BUFFER_CHAIN_PACKET 0

//#define PXE_TEST_CHUNK_PALETTE

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
static const char pxe_server_brand[] = "pixie";

static u32 chunk_data[16][16][16];
static pxe_pool* chunk_pool;

#ifndef PXE_TEST_CHUNK_PALETTE
// static const u32 pxe_chunk_palette[] = {0, 33, 9, 10, 1, 14, 15};
static const u32 pxe_chunk_palette[] = {0, 33, 132, 126, 141};
#else
static const u32 pxe_chunk_palette[4096];
#endif

void pxe_send_packet_chain(pxe_socket* socket, pxe_memory_arena* arena, pxe_pool* pool,
                           i32 packet_id, pxe_buffer_chain* chain, bool32 free) {
  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

  size_t payload_size = pxe_buffer_size(chain);

  // Write total length of payload plus the varint that encodes it.
  i32 length = (i32)(pxe_varint_size(packet_id) + payload_size);

  pxe_buffer_write_varint(&writer, length);
  pxe_buffer_write_varint(&writer, packet_id);

  pxe_buffer_chain* last = writer.last;
  writer.last->next = chain;

  pxe_socket_send_chain(socket, arena, writer.head);

  if (free) {
    // Free the entire chain including header/payload.
    pxe_pool_free(pool, writer.head, 1);
  } else {
    last->next = NULL;
    // Free just the header.
    pxe_pool_free(pool, writer.head, 1);
  }
}

void pxe_send_packet(pxe_socket* socket, pxe_memory_arena* arena, i32 packet_id,
                     pxe_buffer* buffer) {
  if (buffer == NULL) {
    fprintf(stderr, "Packet %d was null when sending.\n", packet_id);
    return;
  }

#if PXE_BUFFER_CHAIN_PACKET
  pxe_buffer_writer writer = pxe_buffer_writer_create(arena, 0);

  // Write total length of payload plus the varint that encodes it.
  i32 length = (i32)(pxe_varint_size(packet_id) + buffer->size);

  pxe_buffer_push_varint(&writer, length, arena);
  pxe_buffer_push_varint(&writer, packet_id, arena);

  pxe_buffer_chain header;
  pxe_buffer_chain payload;

  header.buffer = writer.buffer;
  header.next = &payload;

  payload.next = NULL;
  payload.buffer = buffer;

  pxe_socket_send_chain(socket, arena, &header);

#else
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
#endif
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

bool32 pxe_game_broadcast(pxe_game_server* server, int pkt_id,
                          pxe_buffer_chain* buffer, pxe_memory_arena* trans_arena) {
  for (size_t i = 0; i < server->session_count; ++i) {
    pxe_session* session = server->sessions + i;

    if (session->protocol_state != PXE_PROTOCOL_STATE_PLAY) continue;

    pxe_send_packet_chain(&session->socket, trans_arena, server->write_pool, pkt_id, buffer, 0);
  }

  pxe_pool_free(server->write_pool, buffer, 1);

  return 1;
}

bool32 pxe_game_broadcast_except(pxe_game_server* server, pxe_session* except,
                                 int pkt_id, pxe_buffer_chain* buffer,
                                 pxe_memory_arena* trans_arena) {
  for (size_t i = 0; i < server->session_count; ++i) {
    pxe_session* session = server->sessions + i;

    if (session == except) continue;
    if (session->protocol_state != PXE_PROTOCOL_STATE_PLAY) continue;

    pxe_send_packet_chain(&session->socket, trans_arena, server->write_pool, pkt_id, buffer, 0);
  }

  pxe_pool_free(server->write_pool, buffer, 1);

  return 1;
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

pxe_buffer_chain* pxe_game_encode_chunk_data(pxe_memory_arena* perm_arena,
                                             size_t* chunk_data_size,
                                             u8* bits_per_block) {
  u8 bpb = (u8)ceil(log2(pxe_array_size(pxe_chunk_palette)));

  if (bpb < 4) {
    bpb = 4;
  }

  if (bpb > 8) {
    bpb = 14;
  }

  *bits_per_block = bpb;

  size_t encoded_size = ((16 * 16 * 16) / 8) * (u64)bpb;
  size_t required_segments = encoded_size / sizeof(u64);

  if (chunk_pool == NULL) {
    chunk_pool = pxe_pool_create(perm_arena, required_segments * sizeof(u64));
  }

  pxe_buffer_chain* chain = pxe_pool_alloc(chunk_pool);
  u64* encoded = (u64*)chain->buffer->data;

  // Clear the segments before writing chunk data into them.
  for (size_t i = 0; i < required_segments; ++i) {
    u64* segment = encoded + i;
    *segment = 0;
  }

  size_t bit_index = 0;

  for (size_t y = 0; y < 16; ++y) {
    for (size_t z = 0; z < 16; ++z) {
      for (size_t x = 0; x < 16; ++x) {
        size_t offset = bit_index % (sizeof(u64) * 8);
        size_t index0 = (bit_index / (sizeof(u64) * 8));
        size_t index1 = ((bit_index + bpb - 1) / (sizeof(u64) * 8));

        u64* first_segment = encoded + index0;
        u64* second_segment = encoded + index1;

        u64 block_data = chunk_data[y][z][x];
        u64 first_data = block_data << offset;

        u64 second_mask = ((u64)~0 << (64 - offset)) * (index1 - index0);
        u64 second_data = (block_data & second_mask) >> (64 - offset);

        *first_segment |= first_data;
        *second_segment |= second_data;

        bit_index += bpb;
      }
    }
  }

  for (size_t i = 0; i < required_segments; ++i) {
    u64* segment = encoded + i;
    *segment = bswap_64(*segment);
  }

  *chunk_data_size = required_segments * sizeof(u64);

  return chain;
}

pxe_buffer_chain* pxe_game_create_palette(pxe_pool* pool, size_t* size) {
  size_t palette_length = pxe_array_size(pxe_chunk_palette);

  *size = pxe_varint_size((i32)palette_length);

  for (size_t i = 0; i < palette_length; ++i) {
    *size += pxe_varint_size((i32)pxe_chunk_palette[i]);
  }

  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

  if (pxe_buffer_write_varint(&writer, (i32)palette_length) == 0) {
    return 0;
  }

  for (size_t i = 0; i < palette_length; ++i) {
    if (pxe_buffer_write_varint(&writer, (i32)pxe_chunk_palette[i]) == 0) {
      return 0;
    }
  }

  return writer.head;
}

pxe_buffer_chain* pxe_game_create_chunk_section(pxe_pool* pool,
                                                pxe_memory_arena* perm_arena,
                                                size_t* size) {
  u8 bits_per_block = 4;
  size_t encoded_data_size;

  pxe_buffer_chain* data_chain = pxe_game_encode_chunk_data(
      perm_arena, &encoded_data_size, &bits_per_block);

  size_t palette_size = 0;
  pxe_buffer_chain* palette_chain = NULL;

  if (bits_per_block < 9) {
    palette_chain = pxe_game_create_palette(pool, &palette_size);
  }

  size_t encoded_count = encoded_data_size / sizeof(u64);
  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

  pxe_buffer_write_u16(&writer, 16 * 16);
  pxe_buffer_write_u8(&writer, bits_per_block);

  if (bits_per_block < 9) {
    writer.current->next = palette_chain;
    writer.current = palette_chain;
    writer.last = palette_chain;
    writer.relative_write_pos = palette_chain->buffer->size;
  }

  pxe_buffer_write_varint(&writer, (i32)encoded_count);

  // TODO: find a way to do this without copying
  pxe_buffer_write_raw_string(&writer, (char*)data_chain->buffer->data,
                              encoded_data_size);

  *size = encoded_data_size + palette_size + sizeof(u16) + sizeof(u8) + pxe_varint_size((i32)encoded_count);
  size_t test = pxe_buffer_size(writer.head);
  
  pxe_pool_free(chunk_pool, data_chain, 1);

  return writer.head;
}

// TODO: This should probably be generated with buffer_chains to minimize
// copying
bool32 pxe_game_send_chunk_data(pxe_session* session, pxe_pool* pool,
                                pxe_memory_arena* perm_arena,
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

  size_t chunk_section_size;
  pxe_buffer_chain* section_chain =
      pxe_game_create_chunk_section(pool, perm_arena, &chunk_section_size);

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

  pxe_buffer_writer writer = pxe_buffer_writer_create(pool);

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
    pxe_buffer_chain* current_section = section_chain;
    
    while (current_section) {
      pxe_buffer_chain* current_chain = pxe_pool_alloc(pool);

      // TODO: avoid copy
      memcpy(current_chain->buffer->data, current_section->buffer->data,
        current_section->buffer->size);

      current_chain->buffer->size = current_section->buffer->size;

      writer.current->next = current_chain;
      writer.current = current_chain;
      writer.last = current_chain;
      writer.relative_write_pos = current_chain->buffer->size;

      current_section = current_section->next;
    }
  }

  pxe_pool_free(pool, section_chain, 1);

  for (size_t i = 0; i < 256; ++i) {
    if (pxe_buffer_write_u32(&writer, 0) == 0) {
      return 0;
    }
  }

  if (pxe_buffer_write_varint(&writer, block_entity_count) == 0) {
    return 0;
  }

  pxe_send_packet_chain(&session->socket, trans_arena, pool,
                        PXE_PROTOCOL_OUTBOUND_PLAY_CHUNK_DATA, writer.head, 1);

  return 1;
}

bool32 pxe_game_send_brand(pxe_session* session, pxe_memory_arena* trans_arena,
                           pxe_pool* pool) {
  pxe_buffer_chain* buffer = pxe_serialize_play_plugin_message(
      pool, "minecraft:brand", (const u8*)pxe_server_brand,
      pxe_array_string_size(pxe_server_brand));

  pxe_send_packet_chain(&session->socket, trans_arena, pool,
                        PXE_PROTOCOL_OUTBOUND_PLAY_PLUGIN_MESSAGE, buffer, 1);

  return 1;
}

bool32 pxe_game_change_game_state(pxe_session* session,
                                  pxe_memory_arena* trans_arena, pxe_pool* pool,
                                  pxe_gamemode gamemode) {
  session->gamemode = gamemode;

  pxe_buffer_chain* buffer = pxe_serialize_play_change_game_state(
      pool, PXE_CHANGE_GAME_STATE_REASON_GAMEMODE, (float)gamemode);

  pxe_send_packet_chain(&session->socket, trans_arena, pool,
                        PXE_PROTOCOL_OUTBOUND_PLAY_CHANGE_GAME_STATE, buffer, 1);

  return 1;
}

bool32 pxe_game_send_time(pxe_game_server* server, pxe_session* session,
                          pxe_memory_arena* trans_arena, pxe_pool* pool) {
  pxe_buffer_chain* buffer = pxe_serialize_play_time_update(
      pool, server->world_age, server->world_time);

  pxe_send_packet_chain(&session->socket, trans_arena, pool,
                        PXE_PROTOCOL_OUTBOUND_PLAY_TIME_UPDATE, buffer, 1);

  return 1;
}

bool32 pxe_game_send_position_and_look(pxe_session* session,
                                       pxe_memory_arena* trans_arena,
                                       pxe_pool* pool, double x, double y,
                                       double z, float yaw, float pitch) {
  static i32 next_teleport_id = 1;

  pxe_buffer_chain* buffer = pxe_serialize_play_position_and_look(
      pool, x, y, z, yaw, pitch, 0, next_teleport_id++);

  pxe_send_packet_chain(&session->socket, trans_arena, pool,
                        PXE_PROTOCOL_OUTBOUND_PLAY_PLAYER_POSITION_AND_LOOK,
                        buffer, 1);

  return 1;
}

bool32 pxe_game_send_keep_alive_packet(pxe_session* session,
                                       pxe_memory_arena* trans_arena,
                                       pxe_pool* pool, i64 id) {
  pxe_buffer_chain* buffer = pxe_serialize_play_keep_alive(pool, id);

  pxe_send_packet_chain(&session->socket, trans_arena, pool,
                        PXE_PROTOCOL_OUTBOUND_PLAY_KEEP_ALIVE, buffer, 1);

  return 1;
}

bool32 pxe_game_send_player_abilities(pxe_session* session,
                                      pxe_memory_arena* trans_arena,
                                      pxe_pool* pool, u8 flags, float fly_speed,
                                      float fov) {
  pxe_buffer_chain* buffer =
      pxe_serialize_play_player_abilities(pool, flags, fly_speed, fov);

  pxe_send_packet_chain(&session->socket, trans_arena, pool,
                        PXE_PROTOCOL_OUTBOUND_PLAY_PLAYER_ABILITIES, buffer, 1);

  return 1;
}

bool32 pxe_game_send_join_packet(pxe_session* session,
                                 pxe_memory_arena* trans_arena,
                                 pxe_pool* pool) {
  pxe_buffer_chain* buffer = pxe_serialize_play_join_game(
      pool, session->entity_id, 0, 0, "default", 16, 0);

  pxe_send_packet_chain(&session->socket, trans_arena, pool,
                        PXE_PROTOCOL_OUTBOUND_PLAY_JOIN_GAME, buffer, 1);

  return 1;
}

bool32 pxe_game_broadcast_spawn_player(pxe_game_server* server,
                                       pxe_session* session,
                                       pxe_memory_arena* trans_arena,
                                       pxe_pool* pool) {
  pxe_buffer_chain* buffer = pxe_serialize_play_spawn_player(
      pool, session->entity_id, &session->uuid, session->x, session->y,
      session->z, 0.0f, 0.0f);

  for (size_t i = 0; i < server->session_count; ++i) {
    pxe_session* target_session = server->sessions + i;

    if (target_session == session) continue;
    if (target_session->protocol_state != PXE_PROTOCOL_STATE_PLAY) continue;

    pxe_send_packet_chain(&target_session->socket, trans_arena, pool,
                          PXE_PROTOCOL_OUTBOUND_PLAY_SPAWN_PLAYER, buffer, 0);
  }

  pxe_pool_free(pool, buffer, 1);

  return 1;
}

bool32 pxe_game_broadcast_player_move(pxe_game_server* server,
                                      pxe_session* session,
                                      pxe_memory_arena* trans_arena,
                                      pxe_pool* pool) {
  double delta_x = session->x - session->previous_x;
  double delta_y = session->y - session->previous_y;
  double delta_z = session->z - session->previous_z;

  pxe_buffer_chain* buffer;
  pxe_protocol_outbound_play_id pkt_id;

  if (delta_x < 8 && delta_y < 8 && delta_z < 8) {
    buffer = pxe_serialize_play_entity_look_and_relative_move(
        pool, session->entity_id, delta_x, delta_y, delta_z, session->yaw,
        session->pitch, session->on_ground);

    pkt_id = PXE_PROTOCOL_OUTBOUND_PLAY_ENTITY_LOOK_AND_RELATIVE_MOVE;
  } else {
    buffer = pxe_serialize_play_entity_teleport(
        pool, session->entity_id, session->x, session->y, session->z,
        session->yaw, session->pitch, session->on_ground);

    pkt_id = PXE_PROTOCOL_OUTBOUND_PLAY_ENTITY_TELEPORT;
  }

  pxe_buffer_chain* look_buffer = pxe_serialize_play_entity_head_look(
      pool, session->entity_id, session->yaw);

  for (size_t i = 0; i < server->session_count; ++i) {
    pxe_session* target_session = server->sessions + i;

    if (target_session == session) continue;
    if (target_session->protocol_state != PXE_PROTOCOL_STATE_PLAY) continue;

    pxe_send_packet_chain(&target_session->socket, trans_arena, pool, pkt_id, buffer, 0);

    pxe_send_packet_chain(&target_session->socket, trans_arena, pool,
                          PXE_PROTOCOL_OUTBOUND_PLAY_ENTITY_HEAD_LOOK,
                          look_buffer, 0);
  }

  pxe_pool_free(pool, buffer, 1);
  pxe_pool_free(pool, look_buffer, 1);

  return 1;
}

bool32 pxe_game_broadcast_existing_players(pxe_game_server* server,
                                           pxe_session* session,
                                           pxe_memory_arena* trans_arena,
                                           pxe_pool* pool) {
  for (size_t i = 0; i < server->session_count; ++i) {
    pxe_session* target_session = server->sessions + i;

    if (target_session == session) continue;
    if (target_session->protocol_state != PXE_PROTOCOL_STATE_PLAY) continue;

    pxe_buffer_chain* buffer = pxe_serialize_play_spawn_player(
        pool, target_session->entity_id, &target_session->uuid,
        target_session->x, target_session->y, target_session->z, 0.0f, 0.0f);

    pxe_send_packet_chain(&session->socket, trans_arena, pool,
                          PXE_PROTOCOL_OUTBOUND_PLAY_SPAWN_PLAYER, buffer, 1);
  }

  return 1;
}

bool32 pxe_game_send_existing_player_info(pxe_game_server* server,
                                          pxe_session* session,
                                          pxe_memory_arena* trans_arena,
                                          pxe_pool* pool) {
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

  pxe_buffer_chain* buffer = pxe_serialize_play_player_info(
      pool, PXE_PLAYER_INFO_ADD, infos, info_count);

  for (size_t i = 0; i < server->session_count; ++i) {
    pxe_session* target_session = server->sessions + i;

    if (target_session->protocol_state != PXE_PROTOCOL_STATE_PLAY) continue;

    pxe_send_packet_chain(&target_session->socket, trans_arena, pool,
                          PXE_PROTOCOL_OUTBOUND_PLAY_PLAYER_INFO, buffer, 0);
  }

  pxe_pool_free(pool, buffer, 1);

  return 1;
}

bool32 pxe_game_broadcast_destroy_entity(pxe_game_server* server,
                                         pxe_entity_id eid,
                                         pxe_memory_arena* trans_arena,
                                         pxe_pool* pool) {
  pxe_buffer_chain* buffer = pxe_serialize_play_destroy_entities(pool, &eid, 1);

  for (size_t i = 0; i < server->session_count; ++i) {
    pxe_session* target_session = server->sessions + i;

    if (target_session->entity_id == eid) continue;
    if (target_session->protocol_state != PXE_PROTOCOL_STATE_PLAY) continue;

    pxe_send_packet_chain(&target_session->socket, trans_arena, pool,
                          PXE_PROTOCOL_OUTBOUND_PLAY_DESTROY_ENTITIES, buffer, 0);
  }

  pxe_pool_free(pool, buffer, 1);

  return 1;
}

bool32 pxe_game_broadcast_player_info(pxe_game_server* server,
                                      pxe_session* session,
                                      pxe_player_info_action action,
                                      pxe_memory_arena* trans_arena,
                                      pxe_pool* pool) {
  pxe_player_info info = {0};

  info.uuid = session->uuid;

  if (action == PXE_PLAYER_INFO_ADD) {
    pxe_strcpy(info.add.name, session->username);
  }

  pxe_buffer_chain* buffer =
      pxe_serialize_play_player_info(pool, action, &info, 1);

  for (size_t i = 0; i < server->session_count; ++i) {
    pxe_session* target_session = server->sessions + i;

    if (target_session == session) continue;
    if (target_session->protocol_state != PXE_PROTOCOL_STATE_PLAY) continue;

    pxe_send_packet_chain(&target_session->socket, trans_arena, pool,
                          PXE_PROTOCOL_OUTBOUND_PLAY_PLAYER_INFO, buffer, 0);
  }

  pxe_pool_free(pool, buffer, 1);

  return 1;
}

bool32 pxe_game_server_send_chat(pxe_game_server* server,
                                 pxe_memory_arena* trans_arena, pxe_pool* pool,
                                 char* message, size_t message_len,
                                 char* color) {
  pxe_buffer_chain* buffer =
      pxe_serialize_play_chat(pool, message, message_len, color);

  for (size_t i = 0; i < server->session_count; ++i) {
    pxe_session* session = server->sessions + i;

    if (session->protocol_state != PXE_PROTOCOL_STATE_PLAY) continue;

    pxe_send_packet_chain(&session->socket, trans_arena, pool,
                          PXE_PROTOCOL_OUTBOUND_PLAY_CHAT, buffer, 0);
  }

  pxe_pool_free(pool, buffer, 1);

  return 1;
}

bool32 pxe_game_send_health(pxe_session* session, pxe_memory_arena* trans_arena,
                            pxe_pool* pool) {
  pxe_buffer_chain* buffer =
      pxe_serialize_play_update_health(pool, session->health, 20, 5.0f);

  pxe_send_packet_chain(&session->socket, trans_arena, pool,
                        PXE_PROTOCOL_OUTBOUND_PLAY_UPDATE_HEALTH, buffer, 1);

  return 1;
}

pxe_session* pxe_game_server_get_session_by_eid(pxe_game_server* game_server,
                                                i32 eid) {
  for (size_t i = 0; i < game_server->session_count; ++i) {
    pxe_session* session = game_server->sessions + i;

    if (session->protocol_state != PXE_PROTOCOL_STATE_PLAY) continue;

    if (session->entity_id == eid) {
      return session;
    }
  }

  return NULL;
}

pxe_process_result pxe_game_process_session(pxe_game_server* game_server,
                                            pxe_session* session,
                                            pxe_memory_arena* trans_arena,
                                            pxe_memory_arena* perm_arena) {
  pxe_buffer_reader* reader = &session->buffer_reader;

  session->buffer_reader.chain = session->read_buffer_chain;

  i32 pkt_len, pkt_id;
  size_t pkt_len_size = 0;

  if (pxe_buffer_read_varint(reader, &pkt_len) == 0) {
    return PXE_PROCESS_RESULT_CONSUMED;
  }

  pkt_len_size = pxe_varint_size(pkt_len);

  if (pxe_buffer_read_varint(reader, &pkt_id) == 0) {
    return PXE_PROCESS_RESULT_CONSUMED;
  }

  if (session->protocol_state == PXE_PROTOCOL_STATE_HANDSHAKING) {
    switch (pkt_id) {
      case PXE_PROTOCOL_INBOUND_HANDSHAKING_HANDSHAKE: {
        i32 version;

        if (pxe_buffer_read_varint(reader, &version) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        size_t hostname_len;

        if (pxe_buffer_read_length_string(reader, NULL, &hostname_len) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        char* hostname = pxe_arena_alloc(trans_arena, hostname_len);

        if (pxe_buffer_read_length_string(reader, hostname, &hostname_len) ==
            0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        u16 port;
        if (pxe_buffer_read_u16(reader, &port) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        i32 next_state;
        if (pxe_buffer_read_varint(reader, &next_state) == 0) {
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
        pxe_buffer_writer writer =
            pxe_buffer_writer_create(game_server->write_pool);

        pxe_buffer_write_length_string(&writer, pxe_ping_response, data_size);

        pxe_send_packet_chain(&session->socket, trans_arena, game_server->write_pool,
                              PXE_PROTOCOL_OUTBOUND_STATUS_RESPONSE,
                              writer.head, 1);
      } break;
      case PXE_PROTOCOL_INBOUND_STATUS_PING: {
        // Respond to the game with the same payload.
        u64 payload;
        if (pxe_buffer_read_u64(reader, &payload) == 0) {
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
        if (pxe_buffer_read_length_string(reader, NULL, &username_len) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        char* username = pxe_arena_alloc(trans_arena, username_len + 1);
        if (pxe_buffer_read_length_string(reader, username, &username_len) ==
            0) {
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

#if 0
        printf("%s uuid: %s\n", session->username, uuid);
#endif

        size_t response_size = pxe_varint_size((i32)username_len) +
                               username_len + pxe_varint_size(36) + 36;

        pxe_buffer_writer writer =
            pxe_buffer_writer_create(game_server->write_pool);

        pxe_buffer_write_length_string(&writer, uuid, 36);
        pxe_buffer_write_length_string(&writer, session->username,
                                       username_len);

        pxe_send_packet_chain(&session->socket, trans_arena, game_server->write_pool,
                              PXE_PROTOCOL_OUTBOUND_LOGIN_SUCCESS, writer.head, 1);

        session->entity_id = game_server->next_entity_id++;

        i32 spawn_radius = 30;
        session->previous_x = session->x =
            (rand() % (spawn_radius * 2)) - (double)spawn_radius;
        session->previous_y = session->y = 68;
        session->previous_z = session->z =
            (rand() % (spawn_radius * 2)) - (double)spawn_radius;

        session->yaw = (float)(rand() % 360);
        session->pitch = (float)((rand() % 30) - 30);
        if (!pxe_game_send_join_packet(session, trans_arena,
                                       game_server->write_pool)) {
          fprintf(stderr, "Error writing join packet.\n");
        }

        session->protocol_state = PXE_PROTOCOL_STATE_PLAY;

        pxe_game_send_brand(session, trans_arena, game_server->write_pool);

        char join_message[512];
        size_t join_message_len =
            sprintf_s(join_message, pxe_array_size(join_message),
                      "%s joined the server.", session->username);
        pxe_game_server_send_chat(game_server, trans_arena,
                                  game_server->write_pool, join_message,
                                  join_message_len, "dark_aqua");

        if (pxe_game_send_player_abilities(session, trans_arena,
                                           game_server->write_pool, 0x04, 0.05f,
                                           0.1f) == 0) {
          printf("Failed to send player abilities packet.\n");
        }

        if (pxe_game_send_position_and_look(
                session, trans_arena, game_server->write_pool, session->x,
                session->y, session->z, session->yaw, session->pitch) == 0) {
          fprintf(stderr, "Failed to send position\n");
        }

        if (pxe_game_send_existing_player_info(game_server, session,
                                               trans_arena,
                                               game_server->write_pool) == 0) {
          fprintf(stderr, "Failed to send existing player info.\n");
        }

        if (pxe_game_broadcast_player_info(game_server, session,
                                           PXE_PLAYER_INFO_ADD, trans_arena,
                                           game_server->write_pool) == 0) {
          fprintf(stderr, "Failed to broadcast player info.\n");
        }

        if (!pxe_game_broadcast_spawn_player(game_server, session, trans_arena,
                                             game_server->write_pool)) {
          fprintf(stderr, "Error broadcasting spawn player packet.\n");
        }

        if (!pxe_game_broadcast_existing_players(
                game_server, session, trans_arena, game_server->write_pool)) {
          fprintf(stderr, "Error broadcasting existing players packet.\n");
        }

        // Send terrain
        i32 terrain_radius = 5;
        for (i32 z = -terrain_radius; z < terrain_radius + 1; ++z) {
          for (i32 x = -terrain_radius; x < terrain_radius + 1; ++x) {
            bool32 blank = 0;

            float r = (float)(x * x + z * z);
            if (r > 3.5f * 3.5f) {
              blank = 1;
            }

            if (pxe_game_send_chunk_data(session, game_server->write_pool, perm_arena, trans_arena, x,
                                         z, blank) == 0) {
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

        if (pxe_buffer_read_varint(reader, &teleport_id) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }
      } break;
      case PXE_PROTOCOL_INBOUND_PLAY_CHAT: {
        size_t message_len;
        if (pxe_buffer_read_length_string(reader, NULL, &message_len) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        char* message = pxe_arena_alloc(trans_arena, message_len + 1);
        if (pxe_buffer_read_length_string(reader, message, &message_len) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        message[message_len] = 0;

        if (message_len > 0 && message[0] == '/') {
          if (strcmp(message, "/spawn") == 0) {
            if (pxe_game_send_position_and_look(session, trans_arena,
                                                game_server->write_pool, 5.0f,
                                                68.0f, 5.0f, 0.0f, 0.0f) == 0) {
              fprintf(stderr, "Failed to send position\n");
            }
          } else if (strncmp(message, "/time ", 6) == 0) {
            char* target_string = message + 6;

            game_server->world_time = strtol(target_string, NULL, 10);

            for (size_t session_index = 0;
                 session_index < game_server->session_count; ++session_index) {
              game_server->sessions[session_index].next_keep_alive = 0;
            }
          } else if (strncmp(message, "/gm ", 4) == 0) {
            long gamemode = strtol(message + 4, NULL, 10);

            if (gamemode >= PXE_GAMEMODE_SURVIVAL &&
                gamemode < PXE_GAMEMODE_COUNT) {
              pxe_game_change_game_state(session, trans_arena,
                                         game_server->write_pool,
                                         (pxe_gamemode)gamemode);
            }
          }

        } else {
          char output_message[512];

          size_t output_message_len =
              sprintf_s(output_message, pxe_array_size(output_message),
                        "%s> %s", session->username, message);

          pxe_game_server_send_chat(game_server, trans_arena,
                                    game_server->write_pool, output_message,
                                    output_message_len, "white");
        }
      } break;
      case PXE_PROTOCOL_INBOUND_PLAY_CLIENT_STATUS: {
        i32 action;

        if (pxe_buffer_read_varint(reader, &action) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (action == 0x00) {
          if (session->health <= 0) {
            session->health = 20;

            pxe_buffer_chain* buffer = pxe_serialize_play_respawn(
              game_server->write_pool, 0, PXE_GAMEMODE_SURVIVAL, "default");
            pxe_send_packet_chain(&session->socket, trans_arena, game_server->write_pool,
                            PXE_PROTOCOL_OUTBOUND_PLAY_RESPAWN, buffer, 1);
          }

          session->x = 0;
          session->y = 66;
          session->z = 0;

          pxe_game_send_position_and_look(
              session, trans_arena, game_server->write_pool, session->x,
              session->y, session->z, session->yaw, session->pitch);

          pxe_buffer_chain* buffer = pxe_serialize_play_spawn_player(
              game_server->write_pool, session->entity_id, &session->uuid,
              session->x, session->y, session->z, session->yaw, session->pitch);

          pxe_game_broadcast_except(game_server, session,
                                    PXE_PROTOCOL_OUTBOUND_PLAY_SPAWN_PLAYER,
                                    buffer, trans_arena);
        }
      } break;
      case PXE_PROTOCOL_INBOUND_PLAY_CLIENT_SETTINGS: {
        size_t locale_len;
        if (pxe_buffer_read_length_string(reader, NULL, &locale_len) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        char* locale = pxe_arena_alloc(trans_arena, locale_len + 1);
        if (pxe_buffer_read_length_string(reader, locale, &locale_len) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        locale[locale_len] = 0;

        u8 view_distance;
        if (pxe_buffer_read_u8(reader, &view_distance) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        i32 chat_mode;
        if (pxe_buffer_read_varint(reader, &chat_mode) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        bool32 chat_colors = 0;
        if (pxe_buffer_read_u8(reader, (u8*)&chat_colors) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        u8 skin_parts;
        if (pxe_buffer_read_u8(reader, &skin_parts) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        i32 main_hand;
        if (pxe_buffer_read_varint(reader, &main_hand) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }
      } break;
      case PXE_PROTOCOL_INBOUND_PLAY_PLUGIN_MESSAGE: {
        size_t channel_len;
        if (pxe_buffer_read_length_string(reader, NULL, &channel_len) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        char* channel = pxe_arena_alloc(trans_arena, channel_len + 1);

        if (pxe_buffer_read_length_string(reader, channel, &channel_len) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        channel[channel_len] = 0;

        size_t message_size;

        if (pxe_buffer_read_length_string(reader, NULL, &message_size) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        char* plugin_message = pxe_arena_alloc(trans_arena, message_size + 1);
        if (pxe_buffer_read_length_string(reader, plugin_message,
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
      case PXE_PROTOCOL_INBOUND_PLAY_KEEP_ALIVE: {
        u64 id;

        if (pxe_buffer_read_u64(reader, &id) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        // TODO: Verify keep alive id
      } break;
      case PXE_PROTOCOL_INBOUND_PLAY_PLAYER_POSITION: {
        double x;
        double y;
        double z;
        bool32 on_ground;

        if (pxe_buffer_read_double(reader, &x) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (pxe_buffer_read_double(reader, &y) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (pxe_buffer_read_double(reader, &z) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (pxe_buffer_read_u8(reader, (u8*)&on_ground) == 0) {
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

        if (pxe_buffer_read_double(reader, &x) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (pxe_buffer_read_double(reader, &y) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (pxe_buffer_read_double(reader, &z) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (pxe_buffer_read_float(reader, &yaw) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (pxe_buffer_read_float(reader, &pitch) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (pxe_buffer_read_u8(reader, (u8*)&on_ground) == 0) {
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

        if (pxe_buffer_read_float(reader, &yaw) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (pxe_buffer_read_float(reader, &pitch) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (pxe_buffer_read_u8(reader, (u8*)&on_ground) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        session->yaw = yaw;
        session->pitch = pitch;
        session->on_ground = on_ground;

      } break;
      case PXE_PROTOCOL_INBOUND_PLAY_ANIMATION: {
        i32 hand;

        if (pxe_buffer_read_varint(reader, &hand) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        pxe_animation_type type = PXE_ANIMATION_TYPE_SWING_MAIN;
        if (hand != 0) {
          type = PXE_ANIMATION_TYPE_SWING_OFFHAND;
        }

        // Broadcast swing
        pxe_buffer_chain* buffer =
            pxe_serialize_play_animation(game_server->write_pool, session->entity_id, type);
        pxe_game_broadcast_except(game_server, session,
                                  PXE_PROTOCOL_OUTBOUND_PLAY_ANIMATION, buffer,
                                  trans_arena);
      } break;
      case PXE_PROTOCOL_INBOUND_PLAY_USE_ENTITY: {
        i32 target, type;

        if (pxe_buffer_read_varint(reader, &target) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (pxe_buffer_read_varint(reader, &type) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        if (type == 2) {
          float target_x, target_y, target_z;

          if (pxe_buffer_read_float(reader, &target_x) == 0) {
            return PXE_PROCESS_RESULT_CONSUMED;
          }

          if (pxe_buffer_read_float(reader, &target_y) == 0) {
            return PXE_PROCESS_RESULT_CONSUMED;
          }

          if (pxe_buffer_read_float(reader, &target_z) == 0) {
            return PXE_PROCESS_RESULT_CONSUMED;
          }
        }

        if (type != 1) {
          i32 hand;

          if (pxe_buffer_read_varint(reader, &hand) == 0) {
            return PXE_PROCESS_RESULT_CONSUMED;
          }
        }

        if (type == 1) {
          pxe_session* target_session =
              pxe_game_server_get_session_by_eid(game_server, target);
          if (target_session != NULL) {
            i64 time = pxe_get_time_ms();

            if (target_session->health > 0 &&
                time > target_session->last_damage_time + 500) {
              pxe_buffer_chain* buffer = pxe_serialize_play_animation(
                game_server->write_pool, target_session->entity_id,
                  PXE_ANIMATION_TYPE_DAMAGE);

              pxe_game_broadcast(game_server,
                                 PXE_PROTOCOL_OUTBOUND_PLAY_ANIMATION, buffer,
                                 trans_arena);

              target_session->health -= 6.0f;
              pxe_game_send_health(target_session, trans_arena,
                                   game_server->write_pool);

              target_session->last_damage_time = time;

              if (target_session->health < 0) {
                pxe_buffer_chain* buffer = pxe_serialize_play_entity_status(
                  game_server->write_pool, target_session->entity_id, 3);

                pxe_game_broadcast(game_server,
                                   PXE_PROTOCOL_OUTBOUND_PLAY_ENTITY_STATUS,
                                   buffer, trans_arena);
              }
            }
          }
        }
      } break;
      default: {
#if 1
        fprintf(stderr, "Received unhandled packet %d in state %d\n", pkt_id,
                session->protocol_state);
#endif

#if 1
        size_t payload_size = pkt_len - pxe_varint_size(pkt_id);
        size_t buffer_size = pxe_buffer_size(reader->chain);
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

  pxe_buffer_chain* current = session->read_buffer_chain;

  while (current && reader->read_pos >= current->buffer->size) {
    size_t current_size = current->buffer->size;

    pxe_buffer_chain* next = current->next;

    pxe_pool_free(game_server->read_pool, current, 0);

    current = next;

    reader->read_pos -= current_size;
  }

  session->read_buffer_chain = current;

  if (current == NULL) {
    session->last_read_chain = NULL;
    return PXE_PROCESS_RESULT_CONSUMED;
  }

  return PXE_PROCESS_RESULT_CONTINUE;
}

pxe_game_server* pxe_game_server_create(pxe_memory_arena* perm_arena) {
#ifndef PXE_TEST_CHUNK_PALETTE
  u32 palette_size = (u32)pxe_array_size(pxe_chunk_palette);

  for (size_t z = 0; z < 16; ++z) {
    for (size_t x = 0; x < 16; ++x) {
      chunk_data[0][z][x] = 1;
    }
  }

  for (size_t z = 0; z < 16; ++z) {
    for (size_t x = 0; x < 16; ++x) {
      chunk_data[1][z][x] = (rand() % (palette_size - 2)) + 2;
    }
  }

  for (size_t i = 0; i < 16; ++i) {
    chunk_data[1][0][i] = palette_size - 1;
    chunk_data[1][i][0] = palette_size - 1;
    chunk_data[1][i][15] = palette_size - 1;
    chunk_data[1][15][i] = palette_size - 1;
  }
#else
  for (u32 i = 0; i < 16 * 16 * 16; ++i) {
    *((u32*)chunk_data + i) = i;
  }
#endif

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
  game_server->next_entity_id = 0;
  game_server->world_age = 0;
  game_server->world_time = 0;
  game_server->read_pool = pxe_pool_create(perm_arena, PXE_READ_BUFFER_SIZE);
  game_server->write_pool = pxe_pool_create(perm_arena, PXE_WRITE_BUFFER_SIZE);

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
  pxe_buffer_chain* buffer_chain = pxe_pool_alloc(game_server->read_pool);
  pxe_buffer* buffer = buffer_chain->buffer;

  buffer->size =
      pxe_socket_receive(socket, (char*)buffer->data, PXE_READ_BUFFER_SIZE);

  if (session->read_buffer_chain == NULL) {
    session->read_buffer_chain = buffer_chain;
    session->last_read_chain = buffer_chain;
  } else {
    session->last_read_chain->next = buffer_chain;
    session->last_read_chain = buffer_chain;
  }

  if (socket->state != PXE_SOCKET_STATE_CONNECTED) {
    return 0;
  }

  pxe_process_result process_result = PXE_PROCESS_RESULT_CONTINUE;

  while (process_result == PXE_PROCESS_RESULT_CONTINUE) {
    size_t reader_pos_snapshot = session->buffer_reader.read_pos;

    process_result = pxe_game_process_session(game_server, session, trans_arena,
                                              perm_arena);

    if (process_result == PXE_PROCESS_RESULT_CONSUMED) {
      if (session->read_buffer_chain == NULL) {
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
  if (session->protocol_state == PXE_PROTOCOL_STATE_PLAY) {
    if (pxe_game_broadcast_player_info(server, session, PXE_PLAYER_INFO_REMOVE,
                                       arena, server->write_pool) == 0) {
      fprintf(stderr, "Failed to broadcast player info leave\n");
    }

    if (pxe_game_broadcast_destroy_entity(server, session->entity_id, arena,
                                          server->write_pool) == 0) {
      fprintf(stderr, "Failed to broadcast entity destroy.\n");
    }
  }
}

void pxe_game_server_tick(pxe_game_server* server, pxe_memory_arena* perm_arena,
                          pxe_memory_arena* trans_arena) {
  i64 current_time = pxe_get_time_ms();

  ++server->world_age;
  server->world_time = (server->world_time + 1) % 24000;

  float dt = 50.0f / 1000.0f;
  for (size_t i = 0; i < server->session_count; ++i) {
    pxe_session* session = server->sessions + i;

    if (session->protocol_state != PXE_PROTOCOL_STATE_PLAY) continue;

    if (session->health > 0) {
      i32 prev_discrete_health = (i32)session->health;

      session->health += session->health_regen * dt;

      if (session->health > 20.0f) {
        session->health = 20.0f;
      }

      if ((i32)session->health > prev_discrete_health) {
        pxe_game_send_health(session, trans_arena, server->write_pool);
      }
    }

    if (current_time >= session->next_keep_alive) {
      pxe_game_send_keep_alive_packet(session, trans_arena, server->write_pool,
                                      current_time);
      pxe_game_send_time(server, session, trans_arena, server->write_pool);

      session->next_keep_alive = current_time + 10000;
    }

    if (current_time >= session->next_position_broadcast) {
      pxe_game_broadcast_player_move(server, session, trans_arena,
                                     server->write_pool);

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

    if (current_time >= last_tick_time + 50) {
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
      pxe_arena_reset(trans_arena);
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
