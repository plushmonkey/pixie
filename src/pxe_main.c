#include "pixie.h"

#include "pxe_alloc.h"
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

int main(int argc, char* argv[]) {
#ifdef _WIN32
  WSADATA wsa;

  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    fprintf(stderr, "Failed to initialize WinSock.");
    return 1;
  }
#endif

  size_t trans_size = pxe_megabytes(32);
  void* trans_memory = calloc(1, trans_size);
  pxe_memory_arena trans_arena;

  pxe_arena_initialize(&trans_arena, trans_memory, trans_size);

  pxe_socket socket = {0};
  if (!pxe_socket_connect(&socket, "127.0.0.1", 25565)) {
    fprintf(stderr, "Failed to connect to the server.\n");
    return 1;
  }

  pxe_socket_set_block(&socket, 0);

  u8 bytes[] = ENDPOINT_BYTES(socket.endpoint);
  printf("Connected to %hhu.%hhu.%hhu.%hhu:%hu\n", bytes[0], bytes[1], bytes[2],
         bytes[3], socket.port);

  send_handshake(&socket, &trans_arena, "localhost", 498, 1);

  // Send request packet after switching to status protocol state.
  send_packet(&socket, &trans_arena, 0, NULL, 0);

  const int read_buffer_size = 1024;

  fd_set read_set = {0};
  struct timeval timeout = {0};

  while (socket.state == PXE_SOCKET_STATE_CONNECTED) {
    FD_SET(socket.handle, &read_set);

    int fd_count = select(0, &read_set, NULL, NULL, &timeout);

    if (fd_count > 0) {
      char* buffer = pxe_arena_alloc(&trans_arena, read_buffer_size);
      size_t result = pxe_socket_receive(&socket, buffer, read_buffer_size);

      if (result != 0) {
        for (size_t i = 0; i < result; ++i) {
          printf("%c", buffer[i]);
        }

        printf("\n");
        fflush(stdout);
      }

      pxe_arena_reset(&trans_arena);
    }
  }

  printf("Socket state: %d\n", socket.state);

  if (socket.state == PXE_SOCKET_STATE_ERROR) {
    printf("socket errno: %d\n", socket.error_code);
  }

  return 0;
}
