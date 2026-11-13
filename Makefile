CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2
COMPILER_SOURCES = compiler/main.c compiler/alloc.c compiler/compiler.c \
	compiler/lexer/lexer.c compiler/parser/parser.c \
	compiler/parser/parser_type.c compiler/parser/parser_call.c \
	compiler/parser/parser_expr.c compiler/parser/parser_stmt.c \
	compiler/parser/parser_decl.c \
	compiler/checker/checker.c compiler/checker/checker_names.c \
	compiler/checker/checker_types.c compiler/checker/checker_type_cmp.c \
	compiler/checker/checker_resolve.c \
	compiler/checker/checker_infer.c compiler/checker/checker_expr.c \
	compiler/checker/checker_access.c compiler/checker/checker_calls.c \
	compiler/checker/checker_constructors.c compiler/checker/checker_print.c \
	compiler/checker/checker_methods.c \
	compiler/checker/checker_methods_collection.c \
	compiler/checker/checker_methods_string.c \
	compiler/checker/checker_methods_error.c \
	compiler/checker/checker_methods_store.c \
	compiler/checker/checker_stmt.c \
	compiler/checker/checker_flow.c compiler/checker/checker_decl.c \
	compiler/analyser/analyser.c compiler/generator/generator.c \
	compiler/generator/generator_features.c compiler/generator/generator_support.c \
	compiler/generator/generator_types.c compiler/generator/generator_expr.c \
	compiler/generator/generator_expr_literal.c \
	compiler/generator/generator_expr_string.c \
	compiler/generator/generator_expr_collection.c \
	compiler/generator/generator_expr_access.c \
	compiler/generator/generator_expr_operator.c \
	compiler/generator/generator_stmt.c compiler/generator/generator_print.c \
	compiler/generator/generator_runtime_string.c \
	compiler/generator/generator_runtime_store.c \
	compiler/generator/generator_runtime_list.c \
	compiler/generator/generator_runtime_map.c \
	compiler/generator/generator_arith.c \
	compiler/ast.c compiler/typed_program.c compiler/error.c compiler/list.c
# Everything except the command-line driver, linked into the test binaries so
# tests can exercise stages directly (not only through the nippy executable).
COMPILER_LIB_SOURCES = $(filter-out compiler/main.c,$(COMPILER_SOURCES))
COMPILER_HEADERS = $(wildcard include/*.h compiler/*/*.h)
COMPILER_TEST_SOURCES = tests/compiler_test.c tests/test_support.c \
	tests/golden_data_1.c tests/golden_data_2.c tests/golden_data_3.c \
	tests/golden_data_4.c tests/golden_data_5.c tests/golden_data_6.c \
	tests/golden_test_1.c tests/golden_test_2.c tests/syntax_test_1.c \
	tests/checker_test_1.c tests/checker_test_2.c tests/checker_test_3.c \
	tests/bounds_test.c \
	tests/analyser_test.c tests/generator_test.c tests/compiler_api_test.c \
	tests/misc_test.c tests/harness.c $(COMPILER_LIB_SOURCES)
TOOL_TEST_SOURCES = tests/tool_test.c tests/tool_support.c \
	tests/runtime_store_test.c tests/runtime_string_test.c \
	tests/runtime_collection_test.c tests/runtime_print_test.c \
	tests/runtime_language_test.c tests/runtime_trap_test.c \
	tests/driver_test.c \
	tests/harness.c $(COMPILER_LIB_SOURCES)

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
