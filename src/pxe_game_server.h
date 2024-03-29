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

  i32 next_entity_id;
  u64 world_age;
  u64 world_time;

  pxe_pool* write_pool;
  pxe_pool* read_pool;
} pxe_game_server;

pxe_game_server* pxe_game_server_create(struct pxe_memory_arena* perm_arena);
void pxe_game_server_run(struct pxe_memory_arena* perm_arena,
                         struct pxe_memory_arena* trans_arena);

#endif
