#ifndef PIXIE_SESSION_H_
#define PIXIE_SESSION_H_

#include "pixie.h"
#include "protocol/pxe_protocol.h"
#include "pxe_buffer.h"
#include "pxe_socket.h"
#include "pxe_uuid.h"

typedef enum {
  PXE_GAMEMODE_SURVIVAL = 0x00,
  PXE_GAMEMODE_CREATIVE,
  PXE_GAMEMODE_ADVENTURE,
  PXE_GAMEMODE_SPECTATOR,

  PXE_GAMEMODE_COUNT
} pxe_gamemode;

#define PXE_MAX_OUTBOUND 64

typedef struct pxe_write_chain {
  pxe_buffer_chain* chain;
  struct pxe_write_chain* next;

  void* owner;
} pxe_write_chain;

typedef struct pxe_session {
  pxe_protocol_state protocol_state;
  pxe_socket socket;

  i32 entity_id;
  char username[16];
  pxe_uuid uuid;
  i64 next_keep_alive;
  i64 next_position_broadcast;
  i64 last_damage_time;

  pxe_gamemode gamemode;

  double previous_x;
  double previous_y;
  double previous_z;

  double x;
  double y;
  double z;

  float health;
  float health_regen;

  float yaw;
  float pitch;

  bool32 on_ground;

  pxe_buffer_reader buffer_reader;
  // The chain of buffers that have been read and need to be fully processed.
  struct pxe_buffer_chain* read_buffer_chain;
  // Store the last buffer_chain so it's easy to append in order.
  struct pxe_buffer_chain* last_read_chain;

  struct pxe_write_chain* write_buffer_chain;
  struct pxe_write_chain* last_write_chain;

  struct pxe_pool* write_chain_pool;
} pxe_session;

struct pxe_game_server;

void pxe_session_initialize(pxe_session* session);
void pxe_session_free(pxe_session* session, struct pxe_game_server* server);

#endif
