#include "pxe_session.h"
#include "pxe_game_server.h"

void pxe_session_initialize(pxe_session* session) {
  session->protocol_state = PXE_PROTOCOL_STATE_HANDSHAKING;
  session->socket.state = PXE_SOCKET_STATE_DISCONNECTED;
  session->buffer_reader.read_pos = 0;
  session->buffer_reader.chain = NULL;
  session->last_read_chain = NULL;
  session->read_buffer_chain = NULL;
  session->last_write_chain = NULL;
  session->write_buffer_chain = NULL;
  session->username[0] = 0;
  session->next_keep_alive = 0;
  session->previous_x = session->x = 0;
  session->previous_y = session->y = 0;
  session->previous_z = session->z = 0;
  session->yaw = 0;
  session->pitch = 0;
  session->on_ground = 1;
  session->next_position_broadcast = 0;
  session->gamemode = PXE_GAMEMODE_SURVIVAL;
  session->health_regen = 0.25f;
  session->health = 20.0f;
  session->last_damage_time = 0;
  session->write_chain_pool = NULL;
}

void pxe_session_free(pxe_session* session, pxe_game_server* server) {
  pxe_buffer_chain* chain = session->read_buffer_chain;

  pxe_pool_free(server->read_pool, chain, 1);

  // Free all of the write buffers
  pxe_write_chain* write_chain = session->write_buffer_chain;

  while (write_chain) {
    pxe_pool_free(server->write_pool, write_chain->chain, 1);

    write_chain = write_chain->next;
  }

  write_chain = session->write_buffer_chain;

  // Free all of the write buffer chain holders
  while (write_chain) {
    pxe_write_chain* next = write_chain->next;

    pxe_pool_free(session->write_chain_pool, write_chain->owner, 0);

    write_chain = next;
  }

  session->buffer_reader.chain = NULL;
  session->buffer_reader.read_pos = 0;
  session->read_buffer_chain = NULL;
  session->last_read_chain = NULL;
  session->write_buffer_chain = NULL;
  session->last_write_chain = NULL;
}
