CC=gcc
CFLAGS=-Wall -Wextra -ggdb
PROGRAM_NAME=foo

build: src/main.c bin/
	$(CC) -o bin/$(PROGRAM_NAME) src/main.c $(CFLAGS)

run: build
	./bin/$(PROGRAM_NAME) $(FILE)

bin/:
	mkdir -p bin
