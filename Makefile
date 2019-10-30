CFLAGS=
LIBS=-lws2_32
CC=clang

SRC=$(shell find src -type f -name *.c)

.PHONY: clean

all: pixie.exe clean

pixie.exe: $(SRC:.c=.o)
	$(CC) -o $@ $(CFLAGS) $^ $(LIBS)

clean:
	-rm -f $(SRC:.c=.o)

