#include "pixie.h"

#include "pxe_alloc.h"
#include "pxe_game_server.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
#ifdef _WIN32
  WSADATA wsa;

  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    fprintf(stderr, "Failed to initialize WinSock.\n");
    return 1;
  }
#endif

  size_t trans_size = pxe_megabytes(32);
  void* trans_memory = calloc(1, trans_size);
  pxe_memory_arena trans_arena;

  size_t perm_size = pxe_megabytes(32);
  void* perm_memory = calloc(1, perm_size);
  pxe_memory_arena perm_arena;

  pxe_arena_initialize(&trans_arena, trans_memory, trans_size);
  pxe_arena_initialize(&perm_arena, perm_memory, perm_size);

  pxe_game_server_run(&perm_arena, &trans_arena);

  return 0;
}
