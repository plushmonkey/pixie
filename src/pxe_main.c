#include "pixie.h"

#include "pxe_alloc.h"
#include "pxe_socket.h"
#include "pxe_varint.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

inline pxe_buffer_chain* pxe_chain_insert(pxe_memory_arena* arena,
                                          pxe_buffer_chain* chain, u8* data,
                                          size_t size) {
  pxe_buffer* buffer = pxe_arena_push_type(arena, pxe_buffer);
  pxe_buffer_chain* new_chain = pxe_arena_push_type(arena, pxe_buffer_chain);

  buffer->data = data;
  buffer->size = size;

  new_chain->buffer = buffer;
  new_chain->next = chain;

  return new_chain;
}

inline size_t pxe_chain_size(pxe_buffer_chain* chain) {
  if (chain == NULL) return 0;

  size_t size = 0;

  do {
    size += chain->buffer->size;

    chain = chain->next;
  } while (chain);

  return size;
}

size_t write_string(char* dest, const char* src, size_t len) {
  size_t varint_size = pxe_varint_write(len, dest);

  dest += varint_size;

  for (size_t i = 0; i < len; ++i) {
    dest[i] = src[i];
  }

  return varint_size + len;
}

inline pxe_buffer_chain* generate_string_chain(pxe_memory_arena* arena,
                                               const char* str, size_t len) {
  size_t varint_size = pxe_varint_size(len);

  u8* len_data = pxe_arena_alloc(arena, varint_size);

  pxe_varint_write(len, (char*)len_data);

  pxe_buffer_chain* chain = pxe_chain_insert(arena, NULL, (u8*)str, len);
  chain = pxe_chain_insert(arena, chain, len_data, varint_size);

  return chain;
}

pxe_buffer* generate_empty_packet(pxe_memory_arena* arena, int packet_id) {
  size_t id_size = pxe_varint_size(packet_id);
  size_t length_size = pxe_varint_size(id_size);
  char* pkt = pxe_arena_alloc(arena, length_size + id_size);

  size_t index = 0;

  index += pxe_varint_write(id_size, pkt + index);
  index += pxe_varint_write(packet_id, pkt + index);

  pxe_buffer* buffer = pxe_arena_push_type(arena, pxe_buffer);

  buffer->data = (u8*)pkt;
  buffer->size = index;

  return buffer;
}

pxe_buffer* generate_packet(pxe_memory_arena* arena, int packet_id,
                            pxe_buffer* payload_buffer) {
  size_t id_size = pxe_varint_size(packet_id);
  size_t length_size = pxe_varint_size(payload_buffer->size + id_size);
  char* pkt =
      pxe_arena_alloc(arena, length_size + id_size + payload_buffer->size);

  size_t index = 0;

  index += pxe_varint_write(payload_buffer->size + id_size, pkt + index);
  index += pxe_varint_write(packet_id, pkt + index);

  char* payload = pkt + index;

  for (size_t i = 0; i < payload_buffer->size; ++i) {
    payload[i] = payload_buffer->data[i];
  }

  index += payload_buffer->size;

  pxe_buffer* buffer = pxe_arena_push_type(arena, pxe_buffer);

  buffer->data = (u8*)pkt;
  buffer->size = index;

  return buffer;
}

void send_packet(pxe_socket* socket, pxe_memory_arena* arena, int packet_id,
                 const char* src, size_t size) {
  size_t id_size = pxe_varint_size(packet_id);
  size_t length_size = pxe_varint_size(size + id_size);
  char* pkt = pxe_arena_alloc(arena, length_size + id_size + size);

  size_t index = 0;

  index += pxe_varint_write(size + id_size, pkt + index);
  index += pxe_varint_write(packet_id, pkt + index);

  char* payload = pkt + index;

  for (size_t i = 0; i < size; ++i) {
    payload[i] = src[i];
  }

  index += size;

  pxe_socket_send(socket, pkt, index);
}

pxe_buffer_chain* generate_packet_chain(pxe_memory_arena* arena, int packet_id,
                                        pxe_buffer_chain* payload_chain) {
  size_t payload_size = pxe_chain_size(payload_chain);
  size_t id_size = pxe_varint_size(packet_id);
  size_t length_size = pxe_varint_size(payload_size + id_size);

  u8* id = pxe_arena_alloc(arena, id_size);
  pxe_varint_write(packet_id, (char*)id);

  u8* length = pxe_arena_alloc(arena, length_size);
  pxe_varint_write(payload_size + id_size, (char*)length);

  pxe_buffer_chain* chain = pxe_chain_insert(arena, payload_chain, id, id_size);
  chain = pxe_chain_insert(arena, chain, length, length_size);

  return chain;
}

pxe_buffer_chain* generate_handshake_chain(pxe_memory_arena* arena,
                                           const char* hostname, u16 port,
                                           int protocol_version,
                                           int next_state) {
  size_t host_len = strlen(hostname);
  size_t version_size = pxe_varint_size(protocol_version);
  size_t state_size = pxe_varint_size(next_state);

  char* version_data = pxe_arena_alloc(arena, version_size);
  char* state_data = pxe_arena_alloc(arena, state_size);
  char* port_data = pxe_arena_alloc(arena, sizeof(u16));

  pxe_varint_write(protocol_version, version_data);
  pxe_varint_write(next_state, state_data);

  *(u16*)port_data = port;

  pxe_buffer_chain* hostname_chain =
      generate_string_chain(arena, hostname, host_len);

  pxe_buffer_chain* chain =
      pxe_chain_insert(arena, NULL, (u8*)state_data, state_size);
  chain = pxe_chain_insert(arena, chain, (u8*)port_data, sizeof(u16));
  hostname_chain->next->next = chain;
  chain = hostname_chain;

  chain = pxe_chain_insert(arena, chain, (u8*)version_data, version_size);

  return chain;
}

pxe_buffer* generate_handshake(pxe_memory_arena* arena, const char* hostname,
                               u16 port, int protocol_version, int next_state) {
  size_t host_len = strlen(hostname);

  // buffer_size = protocol version + host_len + host + port + state
  size_t payload_size = pxe_varint_size(protocol_version) +
                        pxe_varint_size(host_len) + host_len + 2 +
                        pxe_varint_size(next_state);

  char* payload = pxe_arena_alloc(arena, payload_size);

  size_t index = pxe_varint_write(protocol_version, payload);
  index += write_string(payload + index, hostname, host_len);

  // append port
  payload[index++] = port & 0xFF;
  payload[index++] = (port & 0xFF00) >> 8;

  index += pxe_varint_write(next_state, payload + index);

  pxe_buffer* buffer = pxe_arena_push_type(arena, pxe_buffer);

  buffer->data = (u8*)payload;
  buffer->size = index;

  return buffer;
}

void send_handshake(pxe_socket* socket, pxe_memory_arena* arena,
                    const char* hostname, int protocol_version,
                    int next_state) {
  size_t host_len = strlen(hostname);

  // buffer_size = protocol version + host_len + host + port + state
  size_t buffer_size = pxe_varint_size(protocol_version) +
                       pxe_varint_size(host_len) + host_len + 2 +
                       pxe_varint_size(next_state);

  char* buffer = pxe_arena_alloc(arena, buffer_size);

  size_t index = pxe_varint_write(protocol_version, buffer);
  index += write_string(buffer + index, hostname, host_len);

  // append port
  buffer[index++] = socket->port & 0xFF;
  buffer[index++] = (socket->port & 0xFF00) >> 8;

  index += pxe_varint_write(next_state, buffer + index);

  send_packet(socket, arena, 0, buffer, index);
}

void test_connection(pxe_memory_arena* arena) {
  pxe_socket socket = {0};

  if (!pxe_socket_connect(&socket, "127.0.0.1", 25565)) {
    fprintf(stderr, "Failed to connect to the server.\n");
    return;
  }

  pxe_socket_set_block(&socket, 0);

  u8 bytes[] = ENDPOINT_BYTES(socket.endpoint);
  printf("Connected to %hhu.%hhu.%hhu.%hhu:%hu\n", bytes[0], bytes[1], bytes[2],
         bytes[3], socket.port);

  pxe_buffer* handshake_data =
      generate_handshake(arena, "127.0.0.1", 25565, 498, 1);
  pxe_buffer* handshake_pkt = generate_packet(arena, 0, handshake_data);

  pxe_socket_send_buffer(&socket, handshake_pkt);

  // Send request packet after switching to status protocol state.
  pxe_buffer* request_pkt = generate_empty_packet(arena, 0);
  pxe_socket_send_buffer(&socket, request_pkt);

  const int read_buffer_size = 4096;

  fd_set read_set = {0};
  struct timeval timeout = {0};

  while (socket.state == PXE_SOCKET_STATE_CONNECTED) {
    FD_SET(socket.fd, &read_set);

    int fd_count = select(0, &read_set, NULL, NULL, &timeout);

    if (fd_count > 0) {
      char* buffer = pxe_arena_alloc(arena, read_buffer_size);
      size_t result = pxe_socket_receive(&socket, buffer, read_buffer_size);

      if (result != 0) {
        i64 pkt_length, pkt_id;

        size_t read_index = pxe_varint_read(buffer, result, &pkt_length);
        read_index +=
            pxe_varint_read(buffer + read_index, result - read_index, &pkt_id);

        printf("Packet length: %lld\n", pkt_length);
        printf("Packet id: %lld\n", pkt_id);

        if (pkt_id == 0) {
          i64 response_size;

          read_index += pxe_varint_read(buffer + read_index,
                                        result - read_index, &response_size);
        }

        buffer += read_index;

        for (size_t i = 0; i < result - read_index; ++i) {
          printf("%c", buffer[i]);
        }

        printf("\n");
        fflush(stdout);
      }

      pxe_socket_disconnect(&socket);
      pxe_arena_reset(arena);
    }
  }

  if (socket.state == PXE_SOCKET_STATE_ERROR) {
    printf("socket errno: %d\n", socket.error_code);
  }
}

#define PXE_MAX_SESSIONS 1024

typedef enum {
  PXE_PROTOCOL_STATE_HANDSHAKE = 0,
  PXE_PROTOCOL_STATE_STATUS,
  PXE_PROTOCOL_STATE_LOGIN,
  PXE_PROTOCOL_STATE_PLAY,

  PXE_PROTOCOL_STATE_COUNT,
} pxe_protocol_state;

typedef struct pxe_session {
  pxe_protocol_state protocol_state;
  pxe_socket socket;
} pxe_session;

typedef struct pxe_ping_server {
  pxe_socket listen_socket;
  pxe_session sessions[PXE_MAX_SESSIONS];
  size_t session_count;
} pxe_ping_server;

static const char pxe_ping_response[] =
    "{\"version\": { \"name\": \"1.14.4\", \"protocol\": 498 }, \"players\": "
    "{\"max\": 420, \"online\": 69, \"sample\": [{\"name\": \"plushmonkey\", "
    "\"id\": \"e812180e-a8aa-4c9f-a8b3-07f591b8de20\"}]}, \"description\": "
    "{\"text\": \"pixie ping server\"}}";
static const char pxe_login_response[] =
    "{\"text\": \"pixie ping server has no game server.\"}";

// TODO: Each session should have a process buffer instead of assuming each
// received buffer is one message.

// TODO: buffer processor that handles read/write position and size validation
bool32 pxe_ping_process_data(pxe_session* session, pxe_buffer* buffer,
                             pxe_memory_arena* trans_arena) {
  i64 pkt_len, pkt_id;

  size_t index = pxe_varint_read((char*)buffer->data, buffer->size, &pkt_len);
  if (index == 0) {
    return 0;
  }

  size_t id_size = pxe_varint_read((char*)buffer->data + index,
                                   buffer->size - index, &pkt_id);

  if (id_size == 0) {
    return 0;
  }

  index += id_size;

  if (session->protocol_state == PXE_PROTOCOL_STATE_HANDSHAKE) {
    i64 version;

    if (index > buffer->size) {
      return 0;
    }

    size_t version_size = pxe_varint_read((char*)buffer->data + index,
                                          buffer->size - index, &version);
    if (version_size == 0) {
      return 0;
    }

    index += version_size;

    if (index > buffer->size) {
      return 0;
    }

    i64 hostname_len;
    size_t hostname_len_size = pxe_varint_read(
        (char*)buffer->data + index, buffer->size - index, &hostname_len);

    if (hostname_len == 0) {
      return 0;
    }

    index += hostname_len_size;

    if (index + hostname_len > buffer->size) {
      return 0;
    }

    char* hostname = pxe_arena_alloc(trans_arena, hostname_len);
    for (i64 i = 0; i < hostname_len; ++i) {
      hostname[i] = (char)buffer->data[index + i];
    }

    index += hostname_len;

    if (index + sizeof(u16) > buffer->size) {
      return 0;
    }

    u16 port = *(u16*)(buffer->data + index);

    index += sizeof(u16);

    i64 next_state;
    size_t state_size =
        pxe_varint_read((char*)buffer->data + index, buffer->size, &next_state);
    if (state_size == 0) {
      return 0;
    }

    // printf("Transitioning to state %lld\n", next_state);

    if (next_state >= PXE_PROTOCOL_STATE_COUNT) {
      printf("Illegal state: %lld. Terminating connection.\n", next_state);
      return 0;
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
        send_packet(&session->socket, trans_arena, 1,
                    (char*)buffer->data + index, buffer->size - index);
      } break;
      default: {
        fprintf(stderr, "Received unhandled packet in state %d\n",
                session->protocol_state);
        return 0;
      }
    }
  } else if (session->protocol_state == PXE_PROTOCOL_STATE_LOGIN) {
    switch (pkt_id) {
      case 0: {
        i64 username_len;

        size_t username_len_size = pxe_varint_read(
            (char*)buffer->data + index, buffer->size - index, &username_len);

        if (username_len_size > 0) {
          index += username_len_size;

          char* username = pxe_arena_alloc(trans_arena, username_len + 1);
          for (i64 i = 0; i < username_len; ++i) {
            username[i] = (char)buffer->data[index + i];
          }

          username[username_len] = 0;

          printf("Login request from %s.\n", username);
        }

        size_t data_size = array_size(pxe_login_response);
        size_t response_size = pxe_varint_size(data_size) + data_size;
        char* response_str = pxe_arena_alloc(trans_arena, response_size);

        write_string(response_str, pxe_login_response, data_size);

        send_packet(&session->socket, trans_arena, 0, response_str,
                    response_size);
        return 0;
      } break;
      default: {
        fprintf(stderr, "Received unhandled packet in state %d\n",
                session->protocol_state);
        return 0;
      }
    }
  } else {
    printf("Unhandled protocol state\n");
    return 0;
  }

  return 1;
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

  return ping_server;
}

void test_server(pxe_memory_arena* perm_arena, pxe_memory_arena* trans_arena) {
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
        }

        fflush(stdout);
      }

      char* read_buf = pxe_arena_alloc(trans_arena, 4096);

      for (size_t i = 0; i < ping_server->session_count;) {
        pxe_session* session = ping_server->sessions + i;
        pxe_socket* socket = &session->socket;

        if (FD_ISSET(socket->fd, &read_set)) {
          size_t buf_size = pxe_socket_receive(socket, read_buf, 4096);

          if (socket->state != PXE_SOCKET_STATE_CONNECTED) {
            // Swap the last session to the current position then decrement
            // session count so this session is removed.
            ping_server->sessions[i] =
                ping_server->sessions[ping_server->session_count - 1];

            --ping_server->session_count;

            u8 bytes[] = ENDPOINT_BYTES(socket->endpoint);

            // printf("%hhu.%hhu.%hhu.%hhu:%hu disconnected.\n", bytes[0],
            //     bytes[1], bytes[2], bytes[3], socket->endpoint.sin_port);

            continue;
          }

          pxe_buffer buffer;

          buffer.data = (u8*)read_buf;
          buffer.size = buf_size;

          if (pxe_ping_process_data(session, &buffer, trans_arena) == 0) {
            u8 bytes[] = ENDPOINT_BYTES(socket->endpoint);

            printf("%hhu.%hhu.%hhu.%hhu:%hu disconnected.\n", bytes[0],
                   bytes[1], bytes[2], bytes[3], socket->endpoint.sin_port);

            pxe_socket_disconnect(socket);

            ping_server->sessions[i] =
                ping_server->sessions[ping_server->session_count - 1];
            --ping_server->session_count;

            fflush(stdout);
            continue;
          }

          fflush(stdout);
        }

        ++i;
      }
    }

    pxe_arena_reset(trans_arena);
  }
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
  WSADATA wsa;

  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    fprintf(stderr, "Failed to initialize WinSock.\n");
    return 1;
  }
#endif

  size_t trans_size = pxe_megabytes(32);
  void* trans_memory = calloc(1, trans_size);
  pxe_memory_arena trans_arena;

  size_t perm_size = pxe_megabytes(32);
  void* perm_memory = calloc(1, perm_size);
  pxe_memory_arena perm_arena;

  pxe_arena_initialize(&trans_arena, trans_memory, trans_size);
  pxe_arena_initialize(&perm_arena, perm_memory, perm_size);

  // test_connection(&trans_arena);
  test_server(&perm_arena, &trans_arena);

  return 0;
}
