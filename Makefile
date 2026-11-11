CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2
COMPILER_SOURCES = compiler/main.c compiler/alloc.c compiler/compiler.c compiler/lexer.c compiler/parser.c compiler/checker.c compiler/analyser.c compiler/generator.c compiler/ast.c compiler/typed_program.c compiler/error.c compiler/list.c
# Everything except the command-line driver, linked into the test binary so
# tests can exercise stages directly (not only through the nippy executable).
COMPILER_LIB_SOURCES = $(filter-out compiler/main.c,$(COMPILER_SOURCES))
COMPILER_HEADERS = $(wildcard include/*.h)

.PHONY: all example test clean

all: bin/nippy

bin/nippy: $(COMPILER_SOURCES) $(COMPILER_HEADERS)
	mkdir -p bin
	$(CC) $(CFLAGS) -Iinclude -o $@ $(COMPILER_SOURCES)

example: bin/nippy
	./bin/nippy examples/13.nip bin/13

bin/tester: tests/runner.c tests/test.c include/test_cases.h $(COMPILER_HEADERS) $(COMPILER_LIB_SOURCES)
	mkdir -p bin
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/runner.c tests/test.c $(COMPILER_LIB_SOURCES)

test: bin/nippy bin/tester
	./bin/tester

clean:
	rm -rf bin
