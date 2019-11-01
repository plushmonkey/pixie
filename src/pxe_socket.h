#ifndef PIXIE_SOCKET_H_
#define PIXIE_SOCKET_H_

#include "pixie.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <WS2tcpip.h>
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define closesocket close
#endif

#ifdef _WIN64
typedef unsigned long long pxe_socket_handle;
#else
typedef unsigned int pxe_socket_handle;
#endif

#define ENDPOINT_BYTES(endpoint)              \
  {                                           \
    (endpoint).sin_addr.S_un.S_un_b.s_b1,     \
        (endpoint).sin_addr.S_un.S_un_b.s_b2, \
        (endpoint).sin_addr.S_un.S_un_b.s_b3, \
        (endpoint).sin_addr.S_un.S_un_b.s_b4  \
  }

typedef enum {
  PXE_SOCKET_STATE_DISCONNECTED = 0,
  PXE_SOCKET_STATE_CONNECTED,
  PXE_SOCKET_STATE_LISTENING,
  PXE_SOCKET_STATE_ERROR
} pxe_socket_state;

typedef struct {
  pxe_socket_handle fd;
  struct sockaddr_in endpoint;
  pxe_socket_state state;
  i32 error_code;
  u16 port;
} pxe_socket;

struct pxe_memory_arena;
struct pxe_buffer;
struct pxe_buffer_chain;

bool32 pxe_socket_connect(pxe_socket* socket, const char* server, u16 port);
void pxe_socket_disconnect(pxe_socket* socket);
// Binds and listens on the provided port.
bool32 pxe_socket_listen(pxe_socket* socket, const char* local_host, u16 port);
bool32 pxe_socket_accept(pxe_socket* socket, pxe_socket* result);
size_t pxe_socket_send(pxe_socket* socket, const char* data, size_t size);
size_t pxe_socket_send_buffer(pxe_socket* socket, struct pxe_buffer* buffer);
size_t pxe_socket_send_chain(pxe_socket* socket, struct pxe_memory_arena* arena,
                             struct pxe_buffer_chain* chain);
size_t pxe_socket_receive(pxe_socket* socket, char* data, size_t size);
void pxe_socket_set_block(pxe_socket* socket, bool32 block);

#endif
