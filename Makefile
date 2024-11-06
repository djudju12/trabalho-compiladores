CC=gcc
RAYLIB=./vendor/raylib/src
CFLAGS=-Wall -Wextra -ggdb -I$(RAYLIB)
LDFLAGS=-L./bin -lraylib -lm
PROGRAM_NAME=foo

build: src/main.c bin/ build_raylib bundle
	$(CC) -o bin/$(PROGRAM_NAME) src/main.c $(CFLAGS) $(LDFLAGS)

run: build
	./bin/$(PROGRAM_NAME) ./examples/simples.foo

run_gateway: build
	./bin/$(PROGRAM_NAME) ./examples/gateway.foo

bin/:
	mkdir -p bin

build_raylib:
	make -C $(RAYLIB) PLATFORM=PLATFORM_DESKTOP
	cp $(RAYLIB)/libraylib.a ./bin

bundle: build_bundler
	./bin/bundler

build_bundler: bin/ src/bundler.c
	$(CC) -o ./bin/bundler src/bundler.c $(CFLAGS)

clean:
	rm -r bin
	rm $(RAYLIB)/*.a
	rm $(RAYLIB)/*.o