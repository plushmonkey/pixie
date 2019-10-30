CFLAGS=
LIBS=-lws2_32
CC=clang

WIN32_SRC=$(shell find src -maxdepth 1 -type f -name *.c)


.PHONY: clean

pixie.exe: $(WIN32_SRC:.c=.o)
	$(CC) -o $@ $(CFLAGS) $^ $(LIBS)

clean:
	-rm -f $(SRC:.c=.o)

