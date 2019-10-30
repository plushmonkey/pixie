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

void send_packet(pxe_socket* socket, int packet_id, char* dest, const char* src,
                 size_t size) {
  size_t id_size = pxe_varint_size(packet_id);
  size_t index = 0;

  index += pxe_varint_write(size + id_size, dest + index);
  index += pxe_varint_write(packet_id, dest + index);

  char* write = dest + index;

  for (size_t i = 0; i < size; ++i) {
    write[i] = src[i];
  }

  index += size;

  pxe_socket_send(socket, dest, index);
}

void test_handshake(pxe_socket* socket) {
  char buffer[1024];
  size_t index = 0;

  index += pxe_varint_write(498, buffer);

  const char* hostname = "localhost";
  size_t host_len = strlen(hostname);

  char* dest = buffer + index;
  index += write_string(buffer + index, hostname, host_len);

  // 0x63DD
  buffer[index++] = 0xDD;
  buffer[index++] = 0x63;

  index += pxe_varint_write(1, buffer + index);

  char write_buffer[2048];
  send_packet(socket, 0, write_buffer, buffer, index);
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

  test_handshake(&socket);

  char write_buffer[2048];

  send_packet(&socket, 0, write_buffer, NULL, 0);

  char buffer[1024];
  while (socket.state == PXE_SOCKET_STATE_CONNECTED) {
    size_t result = pxe_socket_receive(&socket, buffer, array_size(buffer));

    if (result != 0) {
      for (size_t i = 0; i < result; ++i) {
        printf("%c", buffer[i]);
      }

      printf("\n");
      fflush(stdout);
    }
  }

  printf("Socket state: %d\n", socket.state);

  if (socket.state == PXE_SOCKET_STATE_ERROR) {
    printf("socket errno: %d\n", socket.error_code);
  }
  return 0;
}
