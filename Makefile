CC = gcc
CFLAGS = -std=c17 -Wall -Wextra -O2 -g -Iinclude
LDFLAGS = -lpthread

all: server

asan: CFLAGS += -fsanitize=address -fno-omit-frame-pointer
asan: server

server: src/server.c src/http.c src/router.c src/threadpool.c
	$(CC) $(CFLAGS) -o server src/server.c src/http.c src/router.c src/threadpool.c $(LDFLAGS)

clean:
	rm -f server *.o
