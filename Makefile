CC=gcc
RAYLIB=./vendor/raylib/src
CFLAGS=-Wall -Wextra -ggdb -I$(RAYLIB)
LDFLAGS=-L./bin -lraylib -lm
PROGRAM_NAME=foo

build: src/main.c bin/ build_raylib
	$(CC) -o bin/$(PROGRAM_NAME) src/main.c $(CFLAGS) $(LDFLAGS)

run: build
	./bin/$(PROGRAM_NAME) ./examples/simples.foo

bin/:
	mkdir -p bin

build_raylib:
	make -C $(RAYLIB) PLATFORM=PLATFORM_DESKTOP
	cp $(RAYLIB)/libraylib.a ./bin

clean:
	rm -r bin
	rm $(RAYLIB)/*.a
	rm $(RAYLIB)/*.o