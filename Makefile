CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Werror -Wpedantic -Iinclude
AR = ar

LIB = libdunlin.a
OBJS = src/arena.o src/header.o src/request.o

TESTS = tests/test_arena tests/test_header tests/test_parse

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs $@ $^

src/%.o: src/%.c include/dunlin/dunlin.h
	$(CC) $(CFLAGS) -c $< -o $@

tests/test_arena: tests/test_arena.c $(LIB)
	$(CC) $(CFLAGS) $< -L. -ldunlin -o $@

tests/test_header: tests/test_header.c $(LIB)
	$(CC) $(CFLAGS) $< -L. -ldunlin -o $@

tests/test_parse: tests/test_parse.c $(LIB)
	$(CC) $(CFLAGS) $< -L. -ldunlin -o $@

test: $(TESTS)
	./tests/test_arena
	./tests/test_header
	./tests/test_parse

examples:
	@# TODO: hello/routing/middleware examples land with server.c

clean:
	rm -f src/*.o $(LIB) $(TESTS)

format:
	clang-format -i src/*.c tests/*.c include/dunlin/dunlin.h

# TODO(valgrind): run on Linux before step 6 (server.c).
valgrind: $(TESTS)
	valgrind --leak-check=full ./tests/test_arena
	valgrind --leak-check=full ./tests/test_header
	valgrind --leak-check=full ./tests/test_parse

.PHONY: all test examples clean format valgrind
