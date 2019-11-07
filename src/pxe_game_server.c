#include "pxe_game_server.h"

#include "pxe_alloc.h"
#include "pxe_buffer.h"
#include "pxe_nbt.h"
#include "pxe_varint.h"

#include <stdio.h>
#include <stdlib.h>

#ifndef _WIN32
#include <time.h>
#endif

#define READ_BUFFER_SIZE 512

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

size_t write_string(char* dest, const char* src, size_t len);
void send_packet(pxe_socket* socket, pxe_memory_arena* arena, int packet_id,
                 const char* src, size_t size);

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

#ifndef _MSVC_VER
int sprintf_s(char* str, size_t str_size, const char* format, ...) {
  va_list args;

  va_start(args, format);

  int result = vsprintf(str, format, args);

  va_end(args);

  return result;
}
#endif

void pxe_strcpy(char* dest, char* src) {
  while (*src) {
    *dest++ = *src++;
  }
  *dest = *src;
}

void pxe_generate_uuid(char* result) {
  // e812180e-a8aa-4c9f-a8b3-07f591b8de20
  for (size_t i = 0; i < 36; ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      *result++ = '-';
    } else {
      *result++ = (rand() % 9) + '0';
    }
  }
}

static size_t allocated_buffers = 0;

pxe_buffer_chain* pxe_game_get_read_buffer_chain(pxe_game_server* server,
                                                 pxe_memory_arena* perm_arena) {
  if (server->free_buffers == NULL) {
    pxe_buffer_chain* chain = pxe_arena_push_type(perm_arena, pxe_buffer_chain);
    pxe_buffer* buffer = pxe_arena_push_type(perm_arena, pxe_buffer);
    u8* data = pxe_arena_alloc(perm_arena, READ_BUFFER_SIZE);

    buffer->data = data;
    buffer->size = 0;

    chain->buffer = buffer;
    chain->next = NULL;

    ++allocated_buffers;

    return chain;
  }

  pxe_buffer_chain* head = server->free_buffers;

  server->free_buffers = head->next;
  head->next = NULL;

  return head;
}

inline void pxe_game_free_buffer_chain(pxe_game_server* server,
                                       pxe_buffer_chain* chain) {
  chain->next = server->free_buffers;
  server->free_buffers = chain;
}

void pxe_game_free_session(pxe_game_server* server, pxe_session* session) {
  pxe_buffer_chain* current = session->process_buffer_chain;

  // Free all of the unprocessed data for this session.
  while (current) {
    pxe_buffer_chain* next = current->next;

    pxe_game_free_buffer_chain(server, current);

    current = next;
  }

  session->buffer_reader.chain = NULL;
  session->buffer_reader.read_pos = 0;
  session->process_buffer_chain = NULL;
  session->last_buffer_chain = NULL;
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
  heightmap.name_length = array_string_size(motion_blocking);
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

bool32 pxe_game_send_blank_chunk_data(pxe_session* session,
                                      pxe_memory_arena* trans_arena,
                                      i32 chunk_x, i32 chunk_z) {
  char* heightmap_data = NULL;
  size_t heightmap_size = 0;

  bool32 full_chunk = 1;
  i64 bitmask = 0;

  pxe_nbt_tag_compound* heightmap;

  if (pxe_game_create_heightmap_nbt(&heightmap, trans_arena) == 0) {
    return 0;
  }

  if (pxe_nbt_write(heightmap, trans_arena, &heightmap_data, &heightmap_size) ==
      0) {
    return 0;
  }

  i64 data_size = 0;
  i64 block_entity_count = 0;

  size_t size = sizeof(i32) + sizeof(i32) + 1 + pxe_varint_size(bitmask) +
                heightmap_size + pxe_varint_size(data_size) +
                pxe_varint_size(block_entity_count);

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

  if (pxe_buffer_write_varint(&writer, data_size) == 0) {
    return 0;
  }

  if (pxe_buffer_write_varint(&writer, block_entity_count) == 0) {
    return 0;
  }

  send_packet(&session->socket, trans_arena, 0x21, (char*)writer.buffer->data,
              writer.buffer->size);

  return 1;
}

bool32 pxe_game_send_position_and_look(pxe_session* session,
                                       pxe_memory_arena* trans_arena, float x,
                                       float y, float z) {
  static i64 next_teleport_id = 1;

  float yaw = 0.0f;
  float pitch = 0.0f;
  u8 flags = 0;
  i64 teleport_id = next_teleport_id++;

  size_t size = sizeof(double) + sizeof(double) + sizeof(double) +
                sizeof(float) + sizeof(float) + sizeof(u8) +
                pxe_varint_size(teleport_id);

  pxe_buffer* buffer = pxe_arena_push_type(trans_arena, pxe_buffer);
  u8* payload = pxe_arena_alloc(trans_arena, size);

  pxe_buffer_writer writer;

  writer.buffer = buffer;
  writer.buffer->data = payload;
  writer.buffer->size = size;
  writer.write_pos = 0;

  if (!pxe_buffer_write_double(&writer, x)) {
    return 0;
  }

  if (!pxe_buffer_write_double(&writer, y)) {
    return 0;
  }

  if (!pxe_buffer_write_double(&writer, z)) {
    return 0;
  }

  if (!pxe_buffer_write_float(&writer, yaw)) {
    return 0;
  }

  if (!pxe_buffer_write_float(&writer, pitch)) {
    return 0;
  }

  if (!pxe_buffer_write_u8(&writer, flags)) {
    return 0;
  }

  if (!pxe_buffer_write_varint(&writer, teleport_id)) {
    return 0;
  }

  send_packet(&session->socket, trans_arena, 0x35, (char*)writer.buffer->data,
              writer.buffer->size);

  return 1;
}

bool32 pxe_game_send_keep_alive_packet(pxe_session* session,
                                       pxe_memory_arena* trans_arena, i64 id) {
  pxe_buffer* buffer = pxe_arena_push_type(trans_arena, pxe_buffer);
  u8* payload = pxe_arena_alloc(trans_arena, sizeof(u64));

  pxe_buffer_writer writer;

  writer.buffer = buffer;
  writer.buffer->data = payload;
  writer.buffer->size = sizeof(u64);
  writer.write_pos = 0;

  if (pxe_buffer_write_u64(&writer, (u64)id) == 0) {
    return 0;
  }

  send_packet(&session->socket, trans_arena, 0x20, (char*)writer.buffer->data,
              writer.buffer->size);

  return 1;
}

bool32 pxe_game_send_join_packet(pxe_session* session,
                                 pxe_memory_arena* trans_arena) {
  static i32 next_entity_id = 1;

  i32 eid = next_entity_id++;
  u8 gamemode = 0;
  i32 dimension = 0;
  u8 max_players = 0xFF;
  char level_type[] = "default";
  i32 view_distance = 16;
  bool32 reduced_debug = 0;

  size_t level_len = array_size(level_type) - 1;

  size_t size = sizeof(eid) + sizeof(gamemode) + sizeof(dimension) +
                sizeof(max_players) + pxe_varint_size(level_len) + level_len +
                pxe_varint_size(view_distance) + 1;

  pxe_buffer* buffer = pxe_arena_push_type(trans_arena, pxe_buffer);
  u8* payload = pxe_arena_alloc(trans_arena, size);

  pxe_buffer_writer writer;

  writer.buffer = buffer;
  writer.buffer->data = payload;
  writer.buffer->size = size;
  writer.write_pos = 0;

  if (!pxe_buffer_write_u32(&writer, eid)) {
    return 0;
  }

  if (!pxe_buffer_write_u8(&writer, gamemode)) {
    return 0;
  }

  if (!pxe_buffer_write_u32(&writer, dimension)) {
    return 0;
  }

  if (!pxe_buffer_write_u8(&writer, max_players)) {
    return 0;
  }

  if (!pxe_buffer_write_length_string(&writer, level_type, level_len)) {
    return 0;
  }

  if (!pxe_buffer_write_varint(&writer, view_distance)) {
    return 0;
  }

  if (!pxe_buffer_write_u8(&writer, (u8)reduced_debug)) {
    return 0;
  }

  send_packet(&session->socket, trans_arena, 0x25, (char*)writer.buffer->data,
              writer.buffer->size);

  return 1;
}

bool32 pxe_game_server_send_chat(pxe_game_server* server,
                                 pxe_memory_arena* trans_arena, char* message,
                                 size_t message_len, char* color) {
  char data[512];
  u8 position = 0;

  size_t data_len =
      sprintf_s(data, array_size(data), "{\"text\":\"%s\", \"color\": \"%s\"}",
                message, color);

  size_t payload_len = data_len + pxe_varint_size(data_len) + sizeof(u8);
  pxe_buffer* buffer = pxe_arena_push_type(trans_arena, pxe_buffer);
  u8* payload = pxe_arena_alloc(trans_arena, payload_len);

  pxe_buffer_writer writer;

  writer.buffer = buffer;
  writer.buffer->data = payload;
  writer.buffer->size = payload_len;
  writer.write_pos = 0;

  if (pxe_buffer_write_length_string(&writer, data, data_len) == 0) {
    return 0;
  }

  if (pxe_buffer_write_u8(&writer, position) == 0) {
    return 0;
  }

  for (size_t i = 0; i < server->session_count; ++i) {
    pxe_session* session = server->sessions + i;

    if (session->protocol_state != PXE_PROTOCOL_STATE_PLAY) continue;

    send_packet(&session->socket, trans_arena, 0x0E, (char*)writer.buffer->data,
                writer.buffer->size);
  }

  return 1;
}

pxe_process_result pxe_game_process_session(pxe_game_server* game_server,
                                            pxe_session* session,
                                            pxe_memory_arena* trans_arena) {
  pxe_buffer_chain_reader* reader = &session->buffer_reader;

  session->buffer_reader.chain = session->process_buffer_chain;

  i64 pkt_len, pkt_id;
  size_t pkt_len_size = 0;

  if (pxe_buffer_chain_read_varint(reader, &pkt_len) == 0) {
    return PXE_PROCESS_RESULT_CONSUMED;
  }

  pkt_len_size = pxe_varint_size(pkt_len);

  if (pxe_buffer_chain_read_varint(reader, &pkt_id) == 0) {
    return PXE_PROCESS_RESULT_CONSUMED;
  }

  if (session->protocol_state == PXE_PROTOCOL_STATE_HANDSHAKE) {
    i64 version;

    if (pxe_buffer_chain_read_varint(reader, &version) == 0) {
      return PXE_PROCESS_RESULT_CONSUMED;
    }

    size_t hostname_len;

    if (pxe_buffer_chain_read_length_string(reader, NULL, &hostname_len) == 0) {
      return PXE_PROCESS_RESULT_CONSUMED;
    }

    char* hostname = pxe_arena_alloc(trans_arena, hostname_len);

    if (pxe_buffer_chain_read_length_string(reader, hostname, &hostname_len) ==
        0) {
      return PXE_PROCESS_RESULT_CONSUMED;
    }

    u16 port;
    if (pxe_buffer_chain_read_u16(reader, &port) == 0) {
      return PXE_PROCESS_RESULT_CONSUMED;
    }

    i64 next_state;
    if (pxe_buffer_chain_read_varint(reader, &next_state) == 0) {
      return PXE_PROCESS_RESULT_CONSUMED;
    }

    if (next_state >= PXE_PROTOCOL_STATE_COUNT) {
      printf("Illegal state: %ld. Terminating connection.\n", next_state);
      return PXE_PROCESS_RESULT_DESTROY;
    }

    session->protocol_state = next_state;
  } else if (session->protocol_state == PXE_PROTOCOL_STATE_STATUS) {
    switch (pkt_id) {
      case 0: {
        // printf("Sending game response.\n");
        size_t data_size = array_size(pxe_ping_response);
        size_t response_size = pxe_varint_size(data_size) + data_size;
        char* response_str = pxe_arena_alloc(trans_arena, response_size);

        write_string(response_str, pxe_ping_response, data_size);

        send_packet(&session->socket, trans_arena, 0, response_str,
                    response_size);
      } break;
      case 1: {
        // Respond to the game with the same payload.
        u64 payload;
        if (pxe_buffer_chain_read_u64(reader, &payload) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        send_packet(&session->socket, trans_arena, 1, (char*)&payload,
                    sizeof(u64));
      } break;
      default: {
        fprintf(stderr, "Received unhandled packet in state %d\n",
                session->protocol_state);
        return PXE_PROCESS_RESULT_DESTROY;
      }
    }
  } else if (session->protocol_state == PXE_PROTOCOL_STATE_LOGIN) {
    switch (pkt_id) {
      case 0: {
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

        // printf("Login request from %s.\n", username);

#if 1
        char uuid[36];
        pxe_generate_uuid(uuid);

        char* response = pxe_arena_alloc(
            trans_arena, pxe_varint_size(username_len) + username_len +
                             pxe_varint_size(36) + 36);

        size_t index = write_string(response, uuid, 36);
        index +=
            write_string(response + index, session->username, username_len);

        send_packet(&session->socket, trans_arena, 0x02, response, index);

        if (!pxe_game_send_join_packet(session, trans_arena)) {
          fprintf(stderr, "Error writing join packet.\n");
        } else {
          printf("Sent join packet to %s\n", username);
        }

        session->protocol_state = PXE_PROTOCOL_STATE_PLAY;

        char join_message[512];
        size_t join_message_len =
            sprintf_s(join_message, array_size(join_message),
                      "%s joined the server.", session->username);
        pxe_game_server_send_chat(game_server, trans_arena, join_message,
                                  join_message_len, "dark_aqua");

#if 0
        // Send terrain
        if (pxe_game_send_blank_chunk_data(session, trans_arena, 0, 0) == 0) {
          fprintf(stderr, "Failed to send chunk data\n");
        }
#endif

#if 1
        if (pxe_game_send_position_and_look(session, trans_arena, 0.0f, 100.0f,
                                            0.0f) == 0) {
          fprintf(stderr, "Failed to send position\n");
        }
#endif

#else
        size_t data_size = array_size(pxe_login_response);
        size_t response_size = pxe_varint_size(data_size) + data_size;
        char* response_str = pxe_arena_alloc(trans_arena, response_size);

        // Send disconnect message
        write_string(response_str, pxe_login_response, data_size);

        send_packet(&session->socket, trans_arena, 0x1A, response_str,
                    response_size);
        return PXE_PROCESS_RESULT_DESTROY;
#endif
      } break;
      default: {
        fprintf(stderr, "Received unhandled packet %ld in state %d\n", pkt_id,
                session->protocol_state);
        return PXE_PROCESS_RESULT_DESTROY;
      }
    }
  } else if (session->protocol_state == PXE_PROTOCOL_STATE_PLAY) {
    switch (pkt_id) {
      case 0x00: {  // Teleport confirm
        i64 teleport_id;

        if (pxe_buffer_chain_read_varint(reader, &teleport_id) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }
      } break;
      case 0x03: {  // Chat
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

        char output_message[512];

        size_t output_message_len =
            sprintf_s(output_message, array_size(output_message), "%s> %s",
                      session->username, message);

        pxe_game_server_send_chat(game_server, trans_arena, output_message,
                                  output_message_len, "white");
      } break;
      case 0x05: {
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

        i64 chat_mode;
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

        i64 main_hand;
        if (pxe_buffer_chain_read_varint(reader, &main_hand) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        printf("Received client settings from %s.\n", session->username);
      } break;
      case 0x0B: {  // Plugin message
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

        size_t message_size =
            pkt_len - pkt_len_size - channel_len - pxe_varint_size(channel_len);
        char* plugin_message = pxe_arena_alloc(trans_arena, message_size + 1);

        if (pxe_buffer_chain_read_raw_string(reader, plugin_message,
                                             message_size) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        plugin_message[message_size] = 0;

        printf("plugin message from %s: (%s, %s)\n", session->username, channel,
               plugin_message);

#if 0
        return PXE_PROCESS_RESULT_DESTROY;
#endif
      } break;
      case 0x12: {  // Player position and look
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

      } break;
      default: {
#if 1
        fprintf(stderr, "Received unhandled packet %ld in state %d\n", pkt_id,
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
      pxe_socket_receive(socket, (char*)buffer->data, READ_BUFFER_SIZE);

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
        // Set the read position back to the beginning because the entire buffer was processed.
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

  if (epoll_ctl(game_server->epollfd, EPOLL_CTL_ADD, listen_socket->fd, game_server->events)) {
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

        game_server->sessions[index].protocol_state =
            PXE_PROTOCOL_STATE_HANDSHAKE;
        game_server->sessions[index].socket = new_socket;
        game_server->sessions[index].buffer_reader.read_pos = 0;
        game_server->sessions[index].buffer_reader.chain = NULL;
        game_server->sessions[index].last_buffer_chain = NULL;
        game_server->sessions[index].process_buffer_chain = NULL;
        game_server->sessions[index].username[0] = 0;
        game_server->sessions[index].next_keep_alive = 0;

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
          pxe_game_free_session(game_server, session);
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
  int nfds = epoll_wait(game_server->epollfd, game_server->events, PXE_GAME_SERVER_MAX_SESSIONS, 0);

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

        session->protocol_state = PXE_PROTOCOL_STATE_HANDSHAKE;
        session->socket = new_socket;
        session->buffer_reader.read_pos = 0;
        session->buffer_reader.chain = NULL;
        session->last_buffer_chain = NULL;
        session->process_buffer_chain = NULL;
        session->username[0] = 0;
        session->next_keep_alive = 0;

        struct epoll_event new_event = {0};

        new_event.events = EPOLLIN | EPOLLHUP;
        new_event.data.u64 = index;

        if (epoll_ctl(game_server->epollfd, EPOLL_CTL_ADD, new_socket.fd, &new_event)) {
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
          pxe_game_free_session(game_server, session);
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

            // The session pointer now points to the swapped session, so the event should be modified to point to the new session index.
            if (epoll_ctl(game_server->epollfd, EPOLL_CTL_MOD, session->socket.fd, &mod_event)) {
              fprintf(stderr, "Failed to modify event session index for fd %d.\n", session->socket.fd);
            }
          }

          continue;
        }
    }
    fflush(stdout);
  }
#endif
}
