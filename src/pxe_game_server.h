#ifndef PIXIE_GAME_SERVER_H_
#define PIXIE_GAME_SERVER_H_

#include "pixie.h"
#include "pxe_buffer.h"
#include "pxe_session.h"
#include "pxe_socket.h"

#ifndef _WIN32
#include <sys/epoll.h>
#endif

#define PXE_GAME_SERVER_MAX_SESSIONS 4096

typedef struct pxe_game_server {
  pxe_socket listen_socket;
  pxe_session sessions[PXE_GAME_SERVER_MAX_SESSIONS];
  size_t session_count;

#ifdef _WIN32
  WSAPOLLFD events[PXE_GAME_SERVER_MAX_SESSIONS];
  size_t nevents;
#else
  int epollfd;
  struct epoll_event events[PXE_GAME_SERVER_MAX_SESSIONS];
#endif

  struct pxe_buffer_chain* free_buffers;
} pxe_game_server;

pxe_game_server* pxe_game_server_create(struct pxe_memory_arena* perm_arena);
void pxe_game_server_run(struct pxe_memory_arena* perm_arena,
                         struct pxe_memory_arena* trans_arena);

inline void pxe_game_free_buffer_chain(pxe_game_server* server,
                                       pxe_buffer_chain* chain) {
  chain->next = server->free_buffers;
  server->free_buffers = chain;
}

#endif
