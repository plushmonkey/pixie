#ifndef PIXIE_GAME_SERVER_H_
#define PIXIE_GAME_SERVER_H_

#include "pixie.h"
#include "pxe_buffer.h"
#include "pxe_socket.h"

#ifndef _WIN32
#include <sys/epoll.h>
#endif

#define PXE_GAME_SERVER_MAX_SESSIONS 4096

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

  char username[20];
  i64 next_keep_alive;

  pxe_buffer_chain_reader buffer_reader;
  // The chain of buffers that have been read and need to be fully processed.
  struct pxe_buffer_chain* process_buffer_chain;
  // Store the last buffer_chain so it's easy to append in order.
  struct pxe_buffer_chain* last_buffer_chain;
} pxe_session;

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

#endif
