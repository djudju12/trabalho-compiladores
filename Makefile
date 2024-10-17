CC=gcc
CFLAGS=-Wall -Wextra -ggdb
CLIBS=-lm -lraylib
PROGRAM_NAME=foo

build: src/main.c bin/
	$(CC) -o bin/$(PROGRAM_NAME) src/main.c $(CFLAGS) $(CLIBS)

run: build
	./bin/$(PROGRAM_NAME) ./examples/simples.foo

bin/:
	mkdir -p bin
