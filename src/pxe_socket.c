#include "pxe_socket.h"

#include <stdio.h>

#define PXE_INVALID_SOCKET (socket_handle)(~0)
#define PXE_SOCKET_ERROR (socket_handle)(-1)

#ifdef _WIN32
#define PXE_WOULDBLOCK WSAEWOULDBLOCK
#define MSG_DONTWAIT 0
#else
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

  sock->handle = socket(hint.ai_family, hint.ai_socktype, hint.ai_protocol);

  if (sock->handle < 0) {
    return 0;
  }

  char service[10] = {0};
  
  sprintf_s(service, array_size(service), "%d", port);

  if (getaddrinfo(server, service, &hint, &result) != 0) {
    return 0;
  }

  struct addrinfo* ptr = NULL;

  for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {
    struct sockaddr_in* sockaddr = (struct sockaddr_in*)ptr->ai_addr;

    if (connect(sock->handle, (struct sockaddr*)sockaddr,
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
    closesocket(socket->handle);
  }
}

size_t pxe_socket_send(pxe_socket* socket, const char* data, size_t size) {
  if (socket->state != PXE_SOCKET_STATE_CONNECTED) {
    return 0;
  }

  size_t total_sent = 0;

  while (total_sent < size) {
    int current_sent = send(socket->handle, data + total_sent, (int)(size - total_sent), 0);

    if (current_sent <= 0) {
      if (current_sent == SOCKET_ERROR) {
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

size_t pxe_socket_receive(pxe_socket* socket, char* data, size_t size) {
  int recv_amount = recv(socket->handle, data, (int)size, MSG_DONTWAIT);

  if (recv_amount <= 0) {
    int err = pxe_get_error_code();

    if (err == PXE_WOULDBLOCK) {
      return 0;
    }

    socket->error_code = err;
    pxe_socket_disconnect(socket);
    socket->state = PXE_SOCKET_STATE_ERROR;

    return 0;
  }

  return recv_amount;
}

void pxe_socket_set_block(pxe_socket* socket, bool32 block) {
  unsigned long mode = block ? 0 : 1;

#ifdef _WIN32
  ioctlsocket(socket->handle, FIONBIO, &mode);
#else
  int opts = fcntl(socket->handle, F_GETFL);

  if (opts < 0) return;

  if (block) {
    opts |= O_NONBLOCK;
  } else {
    opts &= ~O_NONBLOCK;
  }

  fcntl(m_Handle, F_SETFL, opts);
#endif
}
