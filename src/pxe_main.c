#include "pixie.h"

#include "pxe_alloc.h"
#include "pxe_buffer.h"
#include "pxe_game_server.h"
#include "pxe_nbt.h"
#include "pxe_socket.h"
#include "pxe_varint.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

size_t write_string(char* dest, const char* src, size_t len) {
  size_t varint_size = pxe_varint_write(len, dest);

  dest += varint_size;

  for (size_t i = 0; i < len; ++i) {
    dest[i] = src[i];
  }

  return varint_size + len;
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

pxe_buffer_chain* generate_packet_chain(pxe_memory_arena* arena, int packet_id,
                                        pxe_buffer_chain* payload_chain) {
  size_t payload_size = pxe_buffer_chain_size(payload_chain);
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
  pxe_game_server_run(&perm_arena, &trans_arena);
  // test_nbt_read(&perm_arena, &trans_arena, "out.nbt");
  // test_nbt_write(&perm_arena, &trans_arena, "out.nbt");

  return 0;
}
