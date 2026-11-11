CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2
COMPILER_SOURCES = compiler/main.c compiler/alloc.c compiler/compiler.c compiler/lexer.c compiler/parser.c compiler/checker.c compiler/analyser.c compiler/generator.c compiler/ast.c compiler/typed_program.c compiler/error.c compiler/list.c
# Everything except the command-line driver, linked into the test binaries so
# tests can exercise stages directly (not only through the nippy executable).
COMPILER_LIB_SOURCES = $(filter-out compiler/main.c,$(COMPILER_SOURCES))
COMPILER_HEADERS = $(wildcard include/*.h)
COMPILER_TEST_SOURCES = tests/compiler_test.c tests/harness.c $(COMPILER_LIB_SOURCES)
TOOL_TEST_SOURCES = tests/tool_test.c tests/harness.c $(COMPILER_LIB_SOURCES)

.PHONY: all test test-compiler test-tool clean

all: bin/nippy

bin/nippy: $(COMPILER_SOURCES) $(COMPILER_HEADERS)
	mkdir -p bin
	$(CC) $(CFLAGS) -Iinclude -o $@ $(COMPILER_SOURCES)

bin/compiler-tester: $(COMPILER_TEST_SOURCES) $(COMPILER_HEADERS)
	mkdir -p bin
	$(CC) $(CFLAGS) -Iinclude -o $@ $(COMPILER_TEST_SOURCES)

bin/tool-tester: $(TOOL_TEST_SOURCES) $(COMPILER_HEADERS)
	mkdir -p bin
	$(CC) $(CFLAGS) -Iinclude -o $@ $(TOOL_TEST_SOURCES)

test-compiler: bin/compiler-tester
	./bin/compiler-tester

test-tool: bin/nippy bin/tool-tester
	./bin/tool-tester

test: bin/nippy bin/compiler-tester bin/tool-tester
	@status=0; \
	./bin/compiler-tester || status=1; \
	./bin/tool-tester || status=1; \
	exit $$status

clean:
	rm -rf bin
