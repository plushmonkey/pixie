#include "pxe_ping_server.h"

#include "pxe_alloc.h"
#include "pxe_buffer.h"
#include "pxe_varint.h"

#include <stdio.h>

#define READ_BUFFER_SIZE 512

typedef enum {
  PXE_PROCESS_RESULT_CONTINUE,
  PXE_PROCESS_RESULT_CONSUMED,
  PXE_PROCESS_RESULT_DESTROY,
} pxe_process_result;

static const char pxe_ping_response[] =
    "{\"version\": { \"name\": \"1.14.4\", \"protocol\": 498 }, \"players\": "
    "{\"max\": 420, \"online\": 69, \"sample\": [{\"name\": \"plushmonkey\", "
    "\"id\": \"e812180e-a8aa-4c9f-a8b3-07f591b8de20\"}]}, \"description\": "
    "{\"text\": \"pixie ping server\"}}";
static const char pxe_login_response[] =
    "{\"text\": \"pixie ping server has no game server.\"}";

size_t write_string(char* dest, const char* src, size_t len);
void send_packet(pxe_socket* socket, pxe_memory_arena* arena, int packet_id,
                 const char* src, size_t size);

static size_t allocated_buffers = 0;

pxe_buffer_chain* pxe_ping_get_read_buffer_chain(pxe_ping_server* server,
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

inline void pxe_ping_free_buffer_chain(pxe_ping_server* server,
                                       pxe_buffer_chain* chain) {
  chain->next = server->free_buffers;
  server->free_buffers = chain;
}

void pxe_ping_free_session(pxe_ping_server* server, pxe_session* session) {
  pxe_buffer_chain* current = session->process_buffer_chain;

  // Free all of the unprocessed data for this session.
  while (current) {
    pxe_buffer_chain* next = current->next;

    pxe_ping_free_buffer_chain(server, current);

    current = next;
  }

  session->process_buffer_chain = NULL;
  session->last_buffer_chain = NULL;
}

pxe_process_result pxe_ping_process_session(pxe_ping_server* ping_server,
                                            pxe_session* session,
                                            pxe_memory_arena* trans_arena) {
  pxe_buffer_chain_reader* reader = &session->buffer_reader;
  session->buffer_reader.chain = session->process_buffer_chain;

  i64 pkt_len, pkt_id;

  if (pxe_buffer_chain_read_varint(reader, &pkt_len) == 0) {
    return PXE_PROCESS_RESULT_CONSUMED;
  }

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
        // printf("Sending ping response.\n");
        size_t data_size = array_size(pxe_ping_response);
        size_t response_size = pxe_varint_size(data_size) + data_size;
        char* response_str = pxe_arena_alloc(trans_arena, response_size);

        write_string(response_str, pxe_ping_response, data_size);

        send_packet(&session->socket, trans_arena, 0, response_str,
                    response_size);
      } break;
      case 1: {
        // Respond to the ping with the same payload.
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

        printf("Login request from %s.\n", username);

        size_t data_size = array_size(pxe_login_response);
        size_t response_size = pxe_varint_size(data_size) + data_size;
        char* response_str = pxe_arena_alloc(trans_arena, response_size);

        // Send disconnect message
        write_string(response_str, pxe_login_response, data_size);

        send_packet(&session->socket, trans_arena, 0, response_str,
                    response_size);
        return PXE_PROCESS_RESULT_DESTROY;
      } break;
      default: {
        fprintf(stderr, "Received unhandled packet in state %d\n",
                session->protocol_state);
        return PXE_PROCESS_RESULT_DESTROY;
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

    pxe_ping_free_buffer_chain(ping_server, current);

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

pxe_ping_server* pxe_ping_server_create(pxe_memory_arena* perm_arena) {
  pxe_socket listen_socket = {0};

  if (pxe_socket_listen(&listen_socket, "127.0.0.1", 25565) == 0) {
    fprintf(stderr, "Failed to listen with socket.\n");
    return NULL;
  }

  pxe_ping_server* ping_server =
      pxe_arena_push_type(perm_arena, pxe_ping_server);

  ping_server->session_count = 0;
  ping_server->listen_socket = listen_socket;
  ping_server->free_buffers = NULL;

  for (size_t i = 0; i < PXE_MAX_SESSIONS; ++i) {
    ping_server->sessions[i].buffer_reader.read_pos = 0;
  }

  return ping_server;
}

void pxe_ping_server_run(pxe_memory_arena* perm_arena,
                         pxe_memory_arena* trans_arena) {
  pxe_ping_server* ping_server = pxe_ping_server_create(perm_arena);

  if (ping_server == NULL) {
    fprintf(stderr, "Failed to create ping server.\n");
    return;
  }

  struct timeval timeout = {0};

  printf("Listening for connections...\n");
  fflush(stdout);

  size_t counter = 0;

  pxe_socket* listen_socket = &ping_server->listen_socket;

  while (listen_socket->state == PXE_SOCKET_STATE_LISTENING) {
    fd_set read_set = {0};

    FD_SET(listen_socket->fd, &read_set);

    for (size_t i = 0; i < ping_server->session_count; ++i) {
      pxe_session* session = ping_server->sessions + i;

      FD_SET(session->socket.fd, &read_set);
    }

    /*if (++counter == 1000000) {
      printf("Session count: %lld\n", ping_server->session_count);
      fflush(stdout);
      counter = 0;
    }*/

    if (select(0, &read_set, NULL, NULL, &timeout) > 0) {
      if (FD_ISSET(listen_socket->fd, &read_set)) {
        pxe_socket new_socket = {0};

        if (pxe_socket_accept(listen_socket, &new_socket) == 0) {
          fprintf(stderr, "Failed to accept new socket\n");
        } else {
          u8 bytes[] = ENDPOINT_BYTES(new_socket.endpoint);

          // printf("Accepted %hhu.%hhu.%hhu.%hhu:%hu\n", bytes[0], bytes[1],
          //     bytes[2], bytes[3], new_socket.endpoint.sin_port);

          pxe_socket_set_block(&new_socket, 0);

          size_t index = ping_server->session_count++;

          ping_server->sessions[index].protocol_state =
              PXE_PROTOCOL_STATE_HANDSHAKE;
          ping_server->sessions[index].socket = new_socket;
          ping_server->sessions[index].buffer_reader.read_pos = 0;
          ping_server->sessions[index].buffer_reader.chain = NULL;
          ping_server->sessions[index].last_buffer_chain =
              ping_server->sessions[index].process_buffer_chain = NULL;
        }

        fflush(stdout);
      }

      for (size_t i = 0; i < ping_server->session_count;) {
        pxe_session* session = ping_server->sessions + i;
        pxe_socket* socket = &session->socket;

        if (FD_ISSET(socket->fd, &read_set)) {
          pxe_buffer_chain* buffer_chain =
              pxe_ping_get_read_buffer_chain(ping_server, perm_arena);
          pxe_buffer* buffer = buffer_chain->buffer;

          buffer->size =
              pxe_socket_receive(socket, (char*)buffer->data, READ_BUFFER_SIZE);

          if (socket->state != PXE_SOCKET_STATE_CONNECTED) {
            // Swap the last session to the current position then decrement
            // session count so this session is removed.
            pxe_ping_free_session(ping_server, session);

            ping_server->sessions[i] =
                ping_server->sessions[ping_server->session_count - 1];

            --ping_server->session_count;

            u8 bytes[] = ENDPOINT_BYTES(socket->endpoint);

            // printf("%hhu.%hhu.%hhu.%hhu:%hu disconnected.\n", bytes[0],
            //     bytes[1], bytes[2], bytes[3], socket->endpoint.sin_port);

            continue;
          }

          if (session->process_buffer_chain == NULL) {
            session->process_buffer_chain = buffer_chain;
            session->last_buffer_chain = buffer_chain;
          } else {
            session->last_buffer_chain->next = buffer_chain;
            session->last_buffer_chain = buffer_chain;
          }

          pxe_process_result process_result = PXE_PROCESS_RESULT_CONTINUE;

          while (process_result == PXE_PROCESS_RESULT_CONTINUE) {
            process_result =
                pxe_ping_process_session(ping_server, session, trans_arena);

            if (process_result == PXE_PROCESS_RESULT_DESTROY) {
              pxe_ping_free_session(ping_server, session);

              u8 bytes[] = ENDPOINT_BYTES(socket->endpoint);

              printf("%hhu.%hhu.%hhu.%hhu:%hu disconnected.\n", bytes[0],
                     bytes[1], bytes[2], bytes[3], socket->endpoint.sin_port);

              pxe_socket_disconnect(socket);

              ping_server->sessions[i] =
                  ping_server->sessions[ping_server->session_count - 1];
              --ping_server->session_count;

              break;
            }
          }

          fflush(stdout);

          if (process_result == PXE_PROCESS_RESULT_DESTROY) {
            continue;
          }
        }

        ++i;
      }
    }

    pxe_arena_reset(trans_arena);
  }
}
