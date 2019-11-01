#ifndef PIXIE_PING_SERVER_H_
#define PIXIE_PING_SERVER_H_

#include "pixie.h"
#include "pxe_buffer.h"
#include "pxe_socket.h"

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

  pxe_buffer_chain_reader buffer_reader;
  // The chain of buffers that have been read and need to be fully processed.
  struct pxe_buffer_chain* process_buffer_chain;
  // Store the last buffer_chain so it's easy to append in order.
  struct pxe_buffer_chain* last_buffer_chain;
} pxe_session;

typedef struct pxe_ping_server {
  pxe_socket listen_socket;
  pxe_session sessions[PXE_MAX_SESSIONS];
  size_t session_count;

  struct pxe_buffer_chain* free_buffers;
} pxe_ping_server;

pxe_ping_server* pxe_ping_server_create(struct pxe_memory_arena* perm_arena);
void pxe_ping_server_run(struct pxe_memory_arena* perm_arena,
                         struct pxe_memory_arena* trans_arena);

#endif
