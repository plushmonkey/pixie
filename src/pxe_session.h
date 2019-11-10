#ifndef PIXIE_SESSION_H_
#define PIXIE_SESSION_H_

#include "pixie.h"
#include "protocol/pxe_protocol.h"
#include "pxe_buffer.h"
#include "pxe_socket.h"
#include "pxe_uuid.h"

typedef struct pxe_session {
  pxe_protocol_state protocol_state;
  pxe_socket socket;

  i32 entity_id;
  char username[16];
  pxe_uuid uuid;
  i64 next_keep_alive;
  i64 next_position_broadcast;

  double previous_x;
  double previous_y;
  double previous_z;

  double x;
  double y;
  double z;

  float yaw;
  float pitch;

  bool32 on_ground;

  pxe_buffer_chain_reader buffer_reader;
  // The chain of buffers that have been read and need to be fully processed.
  struct pxe_buffer_chain* process_buffer_chain;
  // Store the last buffer_chain so it's easy to append in order.
  struct pxe_buffer_chain* last_buffer_chain;
} pxe_session;

struct pxe_game_server;

void pxe_session_initialize(pxe_session* session);
void pxe_session_free(pxe_session* session, struct pxe_game_server* server);

#endif
