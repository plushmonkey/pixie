#include "pxe_socket.h"

#include "pxe_alloc.h"
#include "pxe_buffer.h"

#include <stdio.h>

#define PXE_INVALID_SOCKET (pxe_socket_handle)(~0)
#define PXE_SOCKET_ERROR (pxe_socket_handle)(-1)

#ifdef _WIN32
#define PXE_WOULDBLOCK WSAEWOULDBLOCK
#define MSG_DONTWAIT 0
#else
#include <fcntl.h>
#define PXE_WOULDBLOCK EWOULDBLOCK
#endif

static int pxe_get_error_code() {
#if defined(_WIN32) || defined(WIN32)
  int err = WSAGetLastError();
#else
  int err = errno;
#endif

  return err;
}

bool32 pxe_socket_connect(pxe_socket* sock, const char* server, u16 port) {
  sock->state = PXE_SOCKET_STATE_DISCONNECTED;
  sock->port = port;

  struct addrinfo hint = {0};
  struct addrinfo* result = NULL;

  hint.ai_family = AF_INET;
  hint.ai_socktype = SOCK_STREAM;
  hint.ai_protocol = IPPROTO_TCP;

  sock->fd = socket(hint.ai_family, hint.ai_socktype, hint.ai_protocol);

  if (sock->fd < 0) {
    return 0;
  }

  char service[10] = {0};

  sprintf_s(service, pxe_array_size(service), "%d", port);

  if (getaddrinfo(server, service, &hint, &result) != 0) {
    return 0;
  }

  struct addrinfo* ptr = NULL;

  for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {
    struct sockaddr_in* sockaddr = (struct sockaddr_in*)ptr->ai_addr;

    if (connect(sock->fd, (struct sockaddr*)sockaddr,
                sizeof(struct sockaddr_in)) == 0) {
      sock->endpoint = *sockaddr;
      break;
    }
  }

  freeaddrinfo(result);

  if (!ptr) {
    return 0;
  }

  sock->state = PXE_SOCKET_STATE_CONNECTED;

  return 1;
}

void pxe_socket_disconnect(pxe_socket* socket) {
  if (socket->state == PXE_SOCKET_STATE_CONNECTED) {
    closesocket(socket->fd);
    socket->state = PXE_SOCKET_STATE_DISCONNECTED;
  }
}

bool32 pxe_socket_listen(pxe_socket* sock, const char* local_host, u16 port) {
  struct addrinfo hint = {0}, *result;

  hint.ai_family = AF_INET;
  hint.ai_socktype = SOCK_STREAM;
  hint.ai_protocol = IPPROTO_TCP;

  sock->fd = socket(hint.ai_family, hint.ai_socktype, hint.ai_protocol);

  if (sock->fd < 0) {
    return 0;
  }

  int optval = 1;
  setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, (char*)&optval,
             sizeof(optval));

  char service[32];

  sprintf_s(service, pxe_array_size(service), "%d", port);

  if (getaddrinfo(local_host, service, &hint, &result) != 0) {
    return 0;
  }

  if (bind(sock->fd, result->ai_addr, (int)result->ai_addrlen) != 0) {
    freeaddrinfo(result);
    return 0;
  }

  freeaddrinfo(result);

  if (listen(sock->fd, 20) != 0) {
    return 0;
  }

  sock->state = PXE_SOCKET_STATE_LISTENING;

  return 1;
}

bool32 pxe_socket_accept(pxe_socket* socket, pxe_socket* result) {
  struct sockaddr_in their_addr;

  socklen_t addr_size = (int)sizeof(their_addr);

  pxe_socket_set_block(socket, 0);
  pxe_socket_handle new_fd =
      accept(socket->fd, (struct sockaddr*)&their_addr, &addr_size);

  if (new_fd == PXE_SOCKET_ERROR) {
    return 0;
  }

  result->endpoint = their_addr;
  result->fd = new_fd;
  result->port = 0;
  result->state = PXE_SOCKET_STATE_CONNECTED;

  return 1;
}

size_t pxe_socket_send(pxe_socket* socket, const char* data, size_t size) {
  if (socket->state != PXE_SOCKET_STATE_CONNECTED) {
    return 0;
  }

  size_t total_sent = 0;

  while (total_sent < size) {
    int current_sent =
        send(socket->fd, data + total_sent, (int)(size - total_sent), 0);

    if (current_sent <= 0) {
      if (current_sent == PXE_SOCKET_ERROR) {
        socket->error_code = pxe_get_error_code();
        socket->state = PXE_SOCKET_STATE_ERROR;
      } else {
        pxe_socket_disconnect(socket);
      }

      return 0;
    }

    total_sent += current_sent;
  }

  return total_sent;
}

size_t pxe_socket_send_buffer(pxe_socket* socket, pxe_buffer* buffer) {
  return pxe_socket_send(socket, (char*)buffer->data, buffer->size);
}

size_t pxe_socket_send_chain(pxe_socket* socket, struct pxe_memory_arena* arena,
                             pxe_buffer_chain* chain) {
#ifdef _WIN32
  WSABUF* wsa_buffers = pxe_arena_push_type(arena, WSABUF);
  WSABUF* current_buf = wsa_buffers;
  size_t num_buffers = 0;

  do {
    current_buf->buf = (char*)chain->buffer->data;
    current_buf->len = (ULONG)chain->buffer->size;

    ++num_buffers;

    current_buf = pxe_arena_push_type(arena, WSABUF);

    chain = chain->next;
  } while (chain);

  size_t sent = 0;

  if (WSASend(socket->fd, wsa_buffers, (DWORD)num_buffers, (DWORD*)&sent, 0,
              NULL, NULL) != 0) {
    int err = pxe_get_error_code();
    fprintf(stderr, "Error using WSASend: %d\n", err);
  }

  return sent;
#else
  struct iovec* io_buffers = pxe_arena_push_type(arena, struct iovec);
  struct iovec* cur_buf = io_buffers;
  size_t num_buffers = 0;

  do {
    cur_buf->iov_base = chain->buffer->data;
    cur_buf->iov_len = chain->buffer->size;

    ++num_buffers;

    cur_buf = pxe_arena_push_type(arena, struct iovec);

    chain = chain->next;
  } while (chain);

  ssize_t sent;

  sent = writev(socket->fd, io_buffers, num_buffers);

  if (sent < 0) {
    int err = errno;
    fprintf(stderr, "Error using writev: %d\n", err);
  }

  return sent;
#endif
}

size_t pxe_socket_receive(pxe_socket* socket, char* data, size_t size) {
  int recv_amount = recv(socket->fd, data, (int)size, MSG_DONTWAIT);

  if (recv_amount <= 0) {
    int err = pxe_get_error_code();

    if (err == PXE_WOULDBLOCK) {
      return 0;
    }

    pxe_socket_disconnect(socket);

    if (err != 0) {
      socket->error_code = err;
      socket->state = PXE_SOCKET_STATE_ERROR;
    }

    return 0;
  }

  return recv_amount;
}

void pxe_socket_set_block(pxe_socket* socket, bool32 block) {
  unsigned long mode = block ? 0 : 1;

#ifdef _WIN32
  ioctlsocket(socket->fd, FIONBIO, &mode);
#else
  int flags = fcntl(socket->fd, F_GETFL, 0);

  flags = block ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);

  fcntl(socket->fd, F_SETFL, flags);
#endif
}
