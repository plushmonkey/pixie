#include "pixie.h"

#include "pxe_alloc.h"
#include "pxe_game_server.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef _MSC_VER
int sprintf_s(char* str, size_t str_size, const char* format, ...) {
  va_list args;

  va_start(args, format);

  int result = vsprintf(str, format, args);

  va_end(args);

  return result;
}
#endif

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
