#include "pxe_session.h"
#include "pxe_game_server.h"

void pxe_session_free(pxe_session* session, pxe_game_server* server) {
  pxe_buffer_chain* current = session->process_buffer_chain;

  // Free all of the unprocessed data for this session.
  while (current) {
    pxe_buffer_chain* next = current->next;

    pxe_game_free_buffer_chain(server, current);

    current = next;
  }

  session->buffer_reader.chain = NULL;
  session->buffer_reader.read_pos = 0;
  session->process_buffer_chain = NULL;
  session->last_buffer_chain = NULL;
}
