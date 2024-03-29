CFLAGS=-O2
LIBS=
CC=clang

ifeq ($(OS), Windows_NT)
	LIBS += -lws2_32
else
	LIBS += -lm
endif

WIN32_SRC=$(shell find src -maxdepth 2 -type f -name "*.c")

.PHONY: clean

pixie: $(WIN32_SRC:.c=.o)
	$(CC) -o $@ $(CFLAGS) $^ $(LIBS)
unity:
	$(CC) -o pixie-unity $(CFLAGS) unity.c $(LIBS)

clean:
	-rm -f $(WIN32_SRC:.c=.o)

