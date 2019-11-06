#include "pxe_game_server.h"

#include "pxe_alloc.h"
#include "pxe_buffer.h"
#include "pxe_varint.h"

#include <stdio.h>
#include <stdlib.h>

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

void pxe_strcpy(char* dest, char* src) {
  while (*src) {
    *dest++ = *src++;
  }
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

  session->process_buffer_chain = NULL;
  session->last_buffer_chain = NULL;
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
      printf("Illegal state: %lld. Terminating connection.\n", next_state);
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
        fprintf(stderr, "Received unhandled packet %lld in state %d\n", pkt_id,
                session->protocol_state);
        return PXE_PROCESS_RESULT_DESTROY;
      }
    }
  } else if (session->protocol_state == PXE_PROTOCOL_STATE_PLAY) {
    switch (pkt_id) {
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

        // Send terrain
      } break;
      case 0x0B: { // Plugin message
        size_t channel_len;
        if (pxe_buffer_chain_read_length_string(reader, NULL, &channel_len) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        char* channel = pxe_arena_alloc(trans_arena, channel_len + 1);

        if (pxe_buffer_chain_read_length_string(reader, channel, &channel_len) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        channel[channel_len] = 0;

        size_t message_size = pkt_len - reader->read_pos + pkt_len_size;
        char* plugin_message = pxe_arena_alloc(trans_arena, message_size + 1);

        if (pxe_buffer_chain_read_raw_string(reader, plugin_message, message_size) == 0) {
          return PXE_PROCESS_RESULT_CONSUMED;
        }

        plugin_message[message_size] = 0;

        printf("plugin message from %s: (%s, %s)\n", session->username, channel, plugin_message);

#if 1
        return PXE_PROCESS_RESULT_DESTROY;
#endif
      } break;
      default: {
        fprintf(stderr, "Received unhandled packet %lld in state %d\n", pkt_id,
                session->protocol_state);
#if 1
        // Skip over this packet
        reader->read_pos = pkt_len;
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

  pxe_game_server* game_server =
      pxe_arena_push_type(perm_arena, pxe_game_server);

  game_server->session_count = 0;
  game_server->listen_socket = listen_socket;
  game_server->free_buffers = NULL;

#ifdef _WIN32
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
      // Revert the read position because the last process didn't fully
      // read a packet.
      session->buffer_reader.read_pos = reader_pos_snapshot;
    }
  }

  fflush(stdout);

  return process_result != PXE_PROCESS_RESULT_DESTROY;
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
#endif

  while (listen_socket->state == PXE_SOCKET_STATE_LISTENING) {
#ifdef _WIN32
    pxe_game_server_wsa_poll(game_server, listen_socket, perm_arena,
                             trans_arena);
#endif
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

        WSAPOLLFD* new_event = game_server->events + game_server->nevents++;

        new_event->fd = new_socket.fd;
        new_event->events = POLLIN;
        new_event->revents = 0;
      } else {
        fprintf(stderr, "Failed to accept new socket\n");
      }
    }

    for (size_t i = 1; i < game_server->nevents;) {
      if (game_server->events[i].revents != 0) {
        pxe_session* session = &game_server->sessions[i - 1];

        if (pxe_game_server_read_session(game_server, perm_arena, trans_arena,
                                         session) == 0) {
          pxe_game_free_session(game_server, session);
          pxe_socket_disconnect(&session->socket);

          // Swap the last session to the current position then decrement
          // session count so this session is removed.
          game_server->sessions[i] =
              game_server->sessions[--game_server->session_count];

          game_server->events[i] = game_server->events[--game_server->nevents];

#if PXE_OUTPUT_CONNECTIONS
          u8 bytes[] = ENDPOINT_BYTES(session->socket.endpoint);

          printf("%hhu.%hhu.%hhu.%hhu:%hu disconnected.\n", bytes[0], bytes[1],
                 bytes[2], bytes[3], session->socket.endpoint.sin_port);
#endif
          continue;
        }
      }

      ++i;
    }

    fflush(stdout);
  } else if (wsa_result < 0) {
    fprintf(stderr, "WSA error: %d", wsa_result);
  }

  fflush(stdout);
#endif
}