CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2

# Build in parallel by default. An explicit `make -jN` or JOBS=N wins.
JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)
ifeq (,$(filter -j%,$(MAKEFLAGS)))
MAKEFLAGS += -j$(JOBS)
endif
COMPILER_SOURCES = compiler/main.c compiler/alloc.c compiler/compiler.c \
	compiler/lexer/lexer.c compiler/parser/parser.c \
	compiler/parser/parser_type.c compiler/parser/parser_call.c \
	compiler/parser/parser_expr.c compiler/parser/parser_stmt.c \
	compiler/parser/parser_statement.c \
	compiler/parser/parser_decl.c \
	compiler/checker/checker.c compiler/checker/checker_names.c \
	compiler/checker/checker_types.c \
	compiler/checker/checker_type_cmp.c \
	compiler/checker/checker_type_props.c \
	compiler/checker/checker_resolve.c \
	compiler/checker/checker_infer.c compiler/checker/checker_expr.c \
	compiler/checker/checker_access.c compiler/checker/checker_calls.c \
	compiler/checker/checker_constructors.c compiler/checker/checker_print.c \
	compiler/checker/checker_methods.c \
	compiler/checker/checker_methods_collection.c \
	compiler/checker/checker_methods_string.c \
	compiler/checker/checker_methods_error.c \
	compiler/checker/checker_methods_store.c \
	compiler/checker/checker_stmt.c compiler/checker/checker_parallel.c \
	compiler/checker/checker_channel.c \
	compiler/checker/checker_flow.c compiler/checker/checker_decl.c \
	compiler/checker/checker_numeric.c compiler/checker/checker_base64.c \
	compiler/checker/checker_inject.c compiler/checker/checker_match.c compiler/checker/checker_generic.c \
	compiler/analyser/analyser.c compiler/analyser/analyser_validate.c compiler/analyser/analyser_stores.c compiler/analyser/analyser_parallel.c compiler/analyser/analyser_regions.c compiler/analyser/analyser_facts.c compiler/analyser/analyser_types.c compiler/generator/generator.c \
	compiler/generator/generator_support.c \
	compiler/generator/generator_types.c compiler/generator/generator_expr.c \
	compiler/generator/generator_expr_literal.c \
	compiler/generator/generator_expr_string.c \
	compiler/generator/generator_expr_collection.c \
	compiler/generator/generator_expr_access.c \
	compiler/generator/generator_expr_operator.c \
	compiler/generator/generator_equality.c \
	compiler/generator/generator_stmt.c compiler/generator/generator_loops.c compiler/generator/generator_print.c \
	compiler/generator/generator_parallel.c \
	compiler/generator/generator_channel.c \
	compiler/generator/generator_runtime_string.c \
	compiler/generator/generator_runtime_store.c \
	compiler/generator/generator_runtime_list.c \
	compiler/generator/generator_runtime_map.c \
	compiler/generator/generator_arith.c \
	compiler/ast.c compiler/typed_program.c compiler/error.c compiler/list.c \
	compiler/name_index.c
# Everything except the command-line driver, linked into the test binaries so
# tests can exercise stages directly (not only through the nyx executable).
COMPILER_LIB_SOURCES = $(filter-out compiler/main.c,$(COMPILER_SOURCES))

# ---- tests ----
#
# Test sources are found by directory, so adding a test file needs no edit here:
#   tests/compiler/  tests run in-process against the compiler stages
#   tests/tool/      tests that run bin/nyx and the programs it generates
#   tests/support/   helpers shared by the tests
#   tests/harness/   the runner (see tests/harness/harness.h)
HARNESS_SOURCES = tests/harness/harness.c tests/harness/main.c
COMPILER_TEST_SOURCES = $(wildcard tests/compiler/*.c) \
	tests/support/compile_support.c $(HARNESS_SOURCES)
TOOL_TEST_SOURCES = $(wildcard tests/tool/*.c) \
	tests/support/tool_support.c $(HARNESS_SOURCES)

# ---- objects ----
#
# Every source is compiled once into bin/obj and shared, so the compiler is not
# rebuilt for each of the three binaries, an edit rebuilds only what it touches
# (headers are tracked with -MMD), and `make -j` builds files in parallel.
#
# `make test-poison` builds a second copy of everything in bin/obj-poison, with
# the compiler's lists poisoned (see compiler/list.c), into binaries with a
# -poison suffix, and runs the same suites against it.
ifdef POISON
OBJ_DIR = bin/obj-poison
SUFFIX = -poison
POISON_FLAGS = -DNYX_POISON_LISTS
# Tests that depend on allocation counts check this and stand down.
export NYX_POISON_BUILD = 1
else
OBJ_DIR = bin/obj
SUFFIX =
POISON_FLAGS =
endif
# `make test-sanitize` builds another in bin/obj-san with AddressSanitizer
# (which includes the leak checker) and UndefinedBehaviorSanitizer.
ifdef SANITIZE
OBJ_DIR = bin/obj-san
SUFFIX = -san
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=undefined
# Tests that depend on allocation counts or timing check this and stand down.
export NYX_SANITIZE_BUILD = 1
endif
# `make crap` builds a third copy in bin/obj-cov with gcov instrumentation.
ifdef COVERAGE
OBJ_DIR = bin/obj-cov
SUFFIX = -cov
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O0 --coverage
COVERAGE_FLAGS = -DNYX_COVERAGE
endif
CPPFLAGS = -Iinclude -Itests/harness -Itests/support $(POISON_FLAGS) $(COVERAGE_FLAGS)
objects = $(addprefix $(OBJ_DIR)/,$(1:.c=.o))
COMPILER_OBJECTS = $(call objects,$(COMPILER_SOURCES))
COMPILER_LIB_OBJECTS = $(call objects,$(COMPILER_LIB_SOURCES))
COMPILER_TEST_OBJECTS = $(call objects,$(COMPILER_TEST_SOURCES))
TOOL_TEST_OBJECTS = $(call objects,$(TOOL_TEST_SOURCES))
ALL_OBJECTS = $(sort $(COMPILER_OBJECTS) $(COMPILER_TEST_OBJECTS) \
	$(TOOL_TEST_OBJECTS))
NYX = bin/nyx$(SUFFIX)
COMPILER_TESTER = bin/compiler-tester$(SUFFIX)
TOOL_TESTER = bin/tool-tester$(SUFFIX)

.PHONY: all test test-compiler test-tool test-list test-poison test-sanitize \
	golden-update \
	crap clean

all: $(NYX)

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -MP -c -o $@ $<

-include $(ALL_OBJECTS:.o=.d)

$(NYX): $(COMPILER_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(COMPILER_OBJECTS)

# -rdynamic lets a crash backtrace in a test name its functions.
$(COMPILER_TESTER): $(COMPILER_TEST_OBJECTS) $(COMPILER_LIB_OBJECTS)
	$(CC) $(CFLAGS) -rdynamic -o $@ $^

$(TOOL_TESTER): $(TOOL_TEST_OBJECTS) $(COMPILER_LIB_OBJECTS)
	$(CC) $(CFLAGS) -rdynamic -o $@ $^

# `make test T=numeric V=1 J=4` forwards a filter, -v, and -j to the runners.
TESTFLAGS = $(if $(V),-v) $(if $(J),-j $(J)) $(T)

test-compiler: $(COMPILER_TESTER)
	./$(COMPILER_TESTER) $(TESTFLAGS)

test-tool: $(NYX) $(TOOL_TESTER)
	NYX_BIN=./$(NYX) ./$(TOOL_TESTER) $(TESTFLAGS)

# A filter that matches nothing in one suite is not a failure, unless it
# matches nothing in either (a runner exits 2 for "no tests match").
test: $(NYX) $(COMPILER_TESTER) $(TOOL_TESTER)
	@./$(COMPILER_TESTER) $(TESTFLAGS); compiler=$$?; \
	NYX_BIN=./$(NYX) ./$(TOOL_TESTER) $(TESTFLAGS); tool=$$?; \
	if [ $$compiler -eq 1 ] || [ $$tool -eq 1 ]; then exit 1; fi; \
	if [ $$compiler -eq 2 ] && [ $$tool -eq 2 ]; then exit 1; fi; \
	exit 0

# Runs both suites against a compiler whose lists move and poison their old
# storage on every push, which turns a pointer held across a push from a latent
# bug into an immediate failure. Slower than `make test`.
test-poison:
	$(MAKE) POISON=1 test

# Runs both suites under AddressSanitizer, LeakSanitizer, and UBSan. The
# allocation-failure sweeps make this the check that a failing stage releases
# everything it acquired. Slower than `make test`.
test-sanitize:
	$(MAKE) SANITIZE=1 test

# Rewrites the golden tests' expected output from the compiler's current
# output. Review `git diff tests/compiler/golden` before committing it.
golden-update: $(COMPILER_TESTER)
	NYX_UPDATE_GOLDEN=1 ./$(COMPILER_TESTER) -v golden | grep -v "^PASS"; true

test-list: $(COMPILER_TESTER) $(TOOL_TESTER)
	@./$(COMPILER_TESTER) --list $(T); ./$(TOOL_TESTER) --list $(T); true

# CRAP = complexity^2 * (1 - coverage)^3 + complexity, per function. Runs both
# suites with coverage, then reports every function at or above CRAP_LIMIT and
# fails if there is one. See tools/crap.c.
CRAP_LIMIT ?= 20
CRAP_SOURCES = $(COMPILER_SOURCES) $(filter-out compiler/main.c,$(wildcard compiler/*/*.c))

bin/crap: tools/crap.c
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -O2 -o $@ $< -lclang

crap: bin/crap
	@$(MAKE) COVERAGE=1 test > /dev/null
	@for data in $$(find bin/obj-cov -name '*.gcda'); do \
		gcov -f -n $$data 2>/dev/null; done > bin/crap.gcov
	@./bin/crap $(CRAP_LIMIT) bin/crap.gcov $(sort $(CRAP_SOURCES))

clean:
	rm -rf bin/obj bin/obj-poison bin/obj-cov bin/nyx bin/compiler-tester \
		bin/tool-tester bin/nyx-poison bin/compiler-tester-poison \
		bin/tool-tester-poison bin/nyx-cov bin/compiler-tester-cov \
		bin/tool-tester-cov bin/crap bin/crap.gcov
