/* The parallel block and cores(): syntax, the rules that keep arms from
   sharing mutable memory, the audit of what an arm can reach, typed-program
   validation, and the C the generator emits. */

#include "compile_support.h"

#define WORKERS \
    "fun square(n: Int32): Int32 return n * n end\n" \
    "fun fill(out: Store, n: Int32): List<Int32>\n" \
    "  var xs: List<Int32> = out.list<Int32>(n)\n" \
    "  xs.push(n)\n" \
    "  return xs\n" \
    "end\n"

TEST("parallel is a reserved word")
{
    return compile_fails("var parallel: Int32 = 1\n", "parallel as a name",
                         MESSAGE_PARSER_EXPECTED_VARIABLE_NAME);
}

TEST("a parallel block needs do and an arm")
{
    TestResult none = compile_fails("parallel with square(1) end\n",
                                    "parallel without do",
                                    MESSAGE_PARSER_EXPECTED_PARALLEL_DO);

    if (none != TEST_PASS) return none;
    return compile_fails("parallel do end\n", "parallel without an arm",
                         MESSAGE_PARSER_PARALLEL_ARM);
}

TEST("a parallel block needs end")
{
    return compile_fails("parallel do with square(1)\n", "parallel without end",
                         MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
}

TEST("with var needs a name and an equals sign")
{
    return compile_fails("parallel do with var square(1) end\n",
                         "with var without a name",
                         MESSAGE_PARSER_PARALLEL_BINDING);
}

TEST("a repeated arm needs for name in range")
{
    TestResult name = compile_fails(
        "parallel do with square(1) for 3 in range(0, 2) end\n",
        "lane without a name", MESSAGE_PARSER_PARALLEL_LANE);

    if (name != TEST_PASS) return name;
    return compile_fails(
        "parallel do with square(1) for lane range(0, 2) end\n",
        "lane without in", MESSAGE_PARSER_PARALLEL_LANE);
}

TEST("parallel blocks are accepted in every form")
{
    return compile_accepts(
        WORKERS
        "global ticket: Atomic<Int32> = Atomic(0)\n"
        "var a: Int32 = 0\n"
        "var b: Int32 = 0\n"
        "parallel do\n"
        "  with a = square(3)\n"
        "  with b = square(4)\n"
        "end\n"
        "parallel do\n"
        "  with square(ticket.add(1))\n"
        "end\n"
        "parallel pool = Store(1024) do\n"
        "  with var xs = fill(pool, 3)\n"
        "  with var ys = fill(pool, 4)\n"
        "  with square(5) for lane in range(0, cores())\n"
        "then\n"
        "  print(xs.len(), ys.len())\n"
        "end\n",
        "parallel forms");
}

TEST("a parallel block may sit inside a function and a loop")
{
    return compile_accepts(
        WORKERS
        "fun run(n: Int32): Int32\n"
        "  var total: Int32 = 0\n"
        "  for i in range(0, n) do\n"
        "    var part: Int32 = 0\n"
        "    parallel do\n"
        "      with part = square(i)\n"
        "    end\n"
        "    total = total + part\n"
        "  end\n"
        "  return total\n"
        "end\n"
        "print(run(3))\n",
        "parallel in a function");
}

TEST("an arm calls a named, non-generic function")
{
    TestResult unknown = compile_fails(
        "parallel do with nothing(1) end\n", "unknown function",
        MESSAGE_CHECKER_PARALLEL_CALL);
    TestResult generic;
    TestResult member;

    if (unknown != TEST_PASS) return unknown;
    generic = compile_fails(
        "fun id<T>(x: T): T return x end\nparallel do with id(1) end\n",
        "generic function", MESSAGE_CHECKER_PARALLEL_CALL);
    if (generic != TEST_PASS) return generic;
    member = compile_fails(
        "var n: Int32 = 1\nparallel do with n.len() end\n",
        "member call", MESSAGE_CHECKER_PARALLEL_CALL);
    return member;
}

TEST("an arm cannot call its own function")
{
    return compile_fails(
        "fun again(n: Int32): Int32\n"
        "  var r: Int32 = 0\n"
        "  parallel do with r = again(n) end\n"
        "  return r\n"
        "end\n",
        "an arm calling its function", MESSAGE_CHECKER_PARALLEL_SELF);
}

TEST("arm arguments are plain values")
{
    TestResult text = compile_fails(
        "fun f(t: Text): Int32 return 1 end\nparallel do with f(\"x\") end\n",
        "Text argument", MESSAGE_CHECKER_PARALLEL_ARGUMENT, (size_t)1);
    TestResult list;
    TestResult handle;

    if (text != TEST_PASS) return text;
    list = compile_fails(
        "global arena: Store = Store(64)\n"
        "fun f(a: Int32, xs: List<Int32>): Int32 return a end\n"
        "var xs: List<Int32> = arena.list<Int32>(2)\n"
        "parallel do with f(1, xs) end\n",
        "List argument", MESSAGE_CHECKER_PARALLEL_ARGUMENT, (size_t)2);
    if (list != TEST_PASS) return list;
    handle = compile_fails(
        "global arena: Store = Store(64)\n"
        "fun f(h: Handle<Int32>): Int32 return 1 end\n"
        "var h: Handle<Int32> = arena.alloc<Int32>(1)\n"
        "parallel do with f(h) end\n",
        "Handle argument", MESSAGE_CHECKER_PARALLEL_ARGUMENT, (size_t)1);
    return handle;
}

TEST("structs, unions, and arrays of plain values are plain values")
{
    return compile_accepts(
        "type Point x: Int32 y: Float64 end\n"
        "type Shape corners: Array<Point, 4> label: String<8> end\n"
        "fun f(s: Shape, u: Int32 | Bool | Nil, e: Error): Int32 return 1 end\n"
        "var s: Shape = Shape([Point(1, 2.0)], \"box\")\n"
        "var u: Int32 | Bool | Nil = 3\n"
        "var e: Error = Error(\"E\", \"d\", \"f\", 1, 2, 3)\n"
        "parallel do with f(s, u, e) end\n",
        "plain aggregate arguments");
}

TEST("a struct holding a Text is not a plain value")
{
    return compile_fails(
        "type Named name: Text end\n"
        "fun f(n: Named): Int32 return 1 end\n"
        "parallel do with f(Named(\"x\")) end\n",
        "struct with Text", MESSAGE_CHECKER_PARALLEL_ARGUMENT, (size_t)1);
}

TEST("an arm can receive only the store of its own block")
{
    TestResult global = compile_fails(
        "global arena: Store = Store(64)\n"
        "fun f(out: Store): Int32 return 1 end\n"
        "parallel do with f(arena) end\n",
        "a global Store", MESSAGE_CHECKER_PARALLEL_STORE);

    if (global != TEST_PASS) return global;
    return compile_fails(
        "fun f(out: Store): Int32 return 1 end\n"
        "scope s = Store(64) do\n"
        "  parallel do with f(s) end\n"
        "end\n",
        "a scope Store", MESSAGE_CHECKER_PARALLEL_STORE);
}

TEST("the pool needs an Int32 capacity")
{
    return compile_fails(
        "parallel pool = Store(true) do with square(1) end\n",
        "a Bool capacity", MESSAGE_CHECKER_PARALLEL_CAPACITY);
}

TEST("the pool name cannot be reused")
{
    return compile_fails(
        "var pool: Int32 = 1\n"
        "parallel pool = Store(64) do with square(1) end\n",
        "a pool named like a variable", MESSAGE_CHECKER_DUPLICATE_NAME, "pool");
}

TEST("a repeated arm counts over an Int32 range")
{
    TestResult shape = compile_fails(
        WORKERS "parallel do with square(lane) for lane in 5 end\n",
        "a repeated arm without range", MESSAGE_CHECKER_PARALLEL_LANE_RANGE);
    TestResult wide;

    if (shape != TEST_PASS) return shape;
    wide = compile_fails(
        WORKERS "parallel do with square(lane) for lane in range(0, Int64(3)) end\n",
        "an Int64 bound", MESSAGE_CHECKER_PARALLEL_LANE_RANGE);
    if (wide != TEST_PASS) return wide;
    return compile_accepts(
        WORKERS "parallel do with square(lane) for lane in range(0, 3) end\n",
        "a repeated arm that uses its lane");
}

TEST("a repeated arm cannot bind a result with var")
{
    return compile_fails(
        WORKERS "parallel do with var x = square(1) for lane in range(0, 2)\n"
        "then print(x) end\n",
        "a repeated arm with var", MESSAGE_CHECKER_PARALLEL_LANE_RESULT);
}

TEST("a repeated arm collects into an Array of its result type")
{
    return compile_accepts(
        WORKERS "var xs: Array<Int32, 4> = []\n"
        "parallel do with xs = square(lane) for lane in range(0, 4) end\n",
        "a repeated arm collecting results");
}

TEST("a repeated arm cannot collect into a scalar")
{
    return compile_fails(
        WORKERS "var x: Int32 = 0\n"
        "parallel do with x = square(1) for lane in range(0, 2) end\n",
        "a scalar target", MESSAGE_CHECKER_PARALLEL_LANE_TARGET, "x");
}

TEST("a repeated arm cannot collect into an Array of another element type")
{
    return compile_fails(
        WORKERS "var xs: Array<Int64, 4> = []\n"
        "parallel do with xs = square(1) for lane in range(0, 2) end\n",
        "an Array of another element", MESSAGE_CHECKER_PARALLEL_LANE_TARGET,
        "xs");
}

TEST("an assigned result needs a variable of its type")
{
    TestResult missing = compile_fails(
        WORKERS "parallel do with nowhere = square(1) end\n",
        "an unknown target", MESSAGE_CHECKER_PARALLEL_TARGET, "nowhere");
    TestResult constant;
    TestResult wrong;

    if (missing != TEST_PASS) return missing;
    constant = compile_fails(
        WORKERS "const k: Int32 = 1\nparallel do with k = square(1) end\n",
        "a constant target", MESSAGE_CHECKER_PARALLEL_TARGET, "k");
    if (constant != TEST_PASS) return constant;
    wrong = compile_fails(
        WORKERS "var x: Int64 = 0\nparallel do with x = square(1) end\n",
        "a target of another type", MESSAGE_CHECKER_PARALLEL_TARGET_TYPE, "x");
    return wrong;
}

TEST("two arms cannot assign the same variable")
{
    return compile_fails(
        WORKERS "var x: Int32 = 0\n"
        "parallel do with x = square(1) with x = square(2) end\n",
        "two arms, one target", MESSAGE_CHECKER_PARALLEL_DUPLICATE_TARGET, "x");
}

TEST("an assigned result must be a plain value")
{
    return compile_fails(
        WORKERS "global arena: Store = Store(64)\n"
        "var xs: List<Int32> = arena.list<Int32>(1)\n"
        "parallel pool = Store(64) do with xs = fill(pool, 1) end\n",
        "a pooled result assigned outside", MESSAGE_CHECKER_PARALLEL_RESULT_PLAIN);
}

TEST("with var needs a then section")
{
    return compile_fails(
        WORKERS "parallel do with var x = square(1) end\n",
        "with var without then", MESSAGE_CHECKER_PARALLEL_VAR_THEN);
}

TEST("an arm may call a function that returns nothing, but cannot bind it")
{
    TestResult plain = compile_accepts(
        "global hits: Atomic<Int32> = Atomic(0)\n"
        "fun note() hits.add(1) end\n"
        "parallel do with note() with note() end\n",
        "arms with no result");

    if (plain != TEST_PASS) return plain;
    return compile_fails(
        "fun nothing() end\n"
        "parallel do with var x = nothing() then print(1) end\n",
        "with var of a call with no result", MESSAGE_CHECKER_VOID_VALUE);
}

TEST("a result that is not a plain value needs the pool")
{
    return compile_fails(
        "fun make(out: Store): List<Int32> return out.list<Int32>(1) end\n"
        "scope s = Store(64) do\n"
        "  parallel do with var xs = make(s) then print(1) end\n"
        "end\n",
        "a list result without a pool", MESSAGE_CHECKER_PARALLEL_STORE);
}

TEST("a with var result may be a plain value without a pool")
{
    return compile_accepts(
        WORKERS "parallel do with var n = square(7) then print(n) end\n",
        "a plain with var result");
}

TEST("names declared by with var cannot repeat")
{
    return compile_fails(
        WORKERS "parallel do with var n = square(1) with var n = square(2) then print(n) end\n",
        "two with var names", MESSAGE_CHECKER_DUPLICATE_NAME, "n");
}

TEST("the then section can return and use the pool")
{
    return compile_accepts(
        WORKERS
        "fun run(): Int32\n"
        "  parallel pool = Store(256) do\n"
        "    with var xs = fill(pool, 2)\n"
        "  then\n"
        "    if xs.len() > 0 then return xs.len() end\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "print(run())\n",
        "return from a then section");
}

TEST("cores takes no arguments and is an Int32")
{
    TestResult args = compile_fails("var n: Int32 = cores(1)\n", "cores(1)",
                                    MESSAGE_CHECKER_CORES_ARGUMENTS);

    if (args != TEST_PASS) return args;
    return compile_accepts("var n: Int32 = cores()\n", "cores");
}

TEST("cores is reserved")
{
    return compile_fails("fun cores(): Int32 return 1 end\n",
                         "a function named cores",
                         MESSAGE_CHECKER_BUILTIN_RESERVED, "cores");
}

/* The audit follows every call an arm can make. */
TEST("an arm cannot reach a global Store")
{
    return compile_fails(
        "global arena: Store = Store(64)\n"
        "fun leaf(): Int32\n"
        "  var t: Text = arena.copy(\"x\")\n"
        "  return 1\n"
        "end\n"
        "fun mid(): Int32 return leaf() end\n"
        "parallel do with mid() end\n",
        "a global Store two calls down", MESSAGE_ANALYSER_PARALLEL_GLOBAL_STORE,
        "leaf");
}

TEST("an arm cannot call through a function value")
{
    return compile_fails(
        "fun call_it(f: Fun<(Int32): Int32>): Int32 return f(1) end\n"
        "fun twice(x: Int32): Int32 return x * 2 end\n"
        "fun go(): Int32 return call_it(twice) end\n"
        "parallel do with go() end\n",
        "an indirect call", MESSAGE_ANALYSER_PARALLEL_INDIRECT, "call_it");
}

TEST("an arm can reach methods, recursion, and atomics")
{
    return compile_accepts(
        "type Box value: Int32 end\n"
        "method Box.next(): Int32 return self.value + 1 end\n"
        "global hits: Atomic<Int64> = Atomic(0)\n"
        "fun down(n: Int32): Int32\n"
        "  if n == 0 then return 0 end\n"
        "  return 1 + down(n - 1)\n"
        "end\n"
        "fun go(): Int32\n"
        "  var b: Box = Box(1)\n"
        "  hits.add(1)\n"
        "  return b.next() + down(3)\n"
        "end\n"
        "var r: Int32 = 0\n"
        "parallel do with r = go() end\n",
        "methods, recursion, and atomics in an arm");
}

TEST("a value from the pool cannot be stored where it outlives the block")
{
    return compile_fails(
        WORKERS
        "global arena: Store = Store(64)\n"
        "var saved: List<Int32> = arena.list<Int32>(1)\n"
        "parallel pool = Store(256) do\n"
        "  with var xs = fill(pool, 3)\n"
        "then\n"
        "  saved = xs\n"
        "end\n",
        "a pooled list stored in an outer variable",
        MESSAGE_ANALYSER_ESCAPE_ASSIGN, MESSAGE_FRAGMENT_STORE_PREFIX "pool\'", "saved");
}

/* Hand-built programs: the analyser must reject malformed parallel nodes. */
static TypedNode parallel_node(TypedNodeKind kind, size_t type_index)
{
    TypedNode node = {0};

    node.kind = kind;
    node.type_index = type_index;
    node.name_index = TYPED_NO_NODE;
    node.second_name_index = TYPED_NO_NODE;
    node.auxiliary_type = TYPED_NO_NODE;
    node.left_child = TYPED_NO_NODE;
    node.right_child = TYPED_NO_NODE;
    return node;
}

static size_t parallel_type(TypedProgram *program, NyxType kind)
{
    TypedType type = {0};
    size_t index;

    type.kind = kind;
    type.name_index = TYPED_NO_NODE;
    type.signature_index = TYPED_NO_NODE;
    typed_program_add_type(program, &type, &index);
    return index;
}

TEST("a parallel block without arms is rejected")
{
    TypedProgram program;
    ErrorState errors;
    TypedNode node;
    size_t index;
    char expected[256];
    bool ok;

    typed_program_init(&program);
    error_init(&errors);
    node = parallel_node(TYPED_PARALLEL, TYPED_NO_NODE);
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_NODE_LINK, index);
    ok = !analyser_analyse(&program, NULL, &errors) &&
         error_contains(&errors, expected);
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("an arm without a call is rejected")
{
    TypedProgram program;
    ErrorState errors;
    TypedNode node;
    size_t index;
    char expected[256];
    bool ok;

    typed_program_init(&program);
    error_init(&errors);
    node = parallel_node(TYPED_PARALLEL_ARM, TYPED_NO_NODE);
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_NODE_LINK, index);
    ok = !analyser_analyse(&program, NULL, &errors) &&
         error_contains(&errors, expected);
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("a result that names no declaring arm is rejected")
{
    TypedProgram program;
    ErrorState errors;
    TypedNode node;
    size_t index;
    char expected[256];
    bool ok;

    typed_program_init(&program);
    error_init(&errors);
    node = parallel_node(TYPED_PARALLEL_RESULT, TYPED_NO_NODE);
    node.integer_value_u = 99;
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_NODE_LINK, index);
    ok = !analyser_analyse(&program, NULL, &errors) &&
         error_contains(&errors, expected);
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("cores with the wrong type is rejected")
{
    TypedProgram program;
    ErrorState errors;
    TypedNode node;
    size_t index, boolean;
    char expected[256];
    bool ok;

    typed_program_init(&program);
    error_init(&errors);
    boolean = parallel_type(&program, NYX_TYPE_BOOL);
    node = parallel_node(TYPED_CORES, boolean);
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_TYPE_INDEX, index);
    ok = !analyser_analyse(&program, NULL, &errors) &&
         error_contains(&errors, expected);
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("threaded runtime appears only when a program uses parallel")
{
    const char *threaded = WORKERS
        "var a: Int32 = 0\n"
        "parallel pool = Store(64) do with a = square(2) end\n"
        "print(a)\n";
    CompilerResult used = compiler_compile(threaded, &test_config);
    CompilerResult unused = compiler_compile(
        WORKERS "print(square(2))\n", &test_config);
    int ok;

    ok = used.succeeded && unused.succeeded &&
         strstr(used.c_source, "#include <pthread.h>") != NULL &&
         strstr(used.c_source, "nyx_exit_once") != NULL &&
         strstr(used.c_source, "pthread_create") != NULL &&
         strstr(used.c_source, "flockfile(stdout)") != NULL &&
         strstr(used.c_source, "_Atomic uint64_t nyx_scope_generation") != NULL &&
         strstr(used.c_source, "nyx_pool_take") != NULL &&
         strstr(used.h_source, "_Atomic size_t cursor") != NULL &&
         strstr(unused.c_source, "pthread") == NULL &&
         strstr(unused.c_source, "nyx_exit_once") == NULL &&
         strstr(unused.c_source, "flockfile") == NULL &&
         strstr(unused.c_source, "nyx_pool") == NULL &&
         (unused.h_source == NULL || strstr(unused.h_source, "cursor") == NULL);
    compiler_result_free(&used);
    compiler_result_free(&unused);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("a recursive function gets a thread-local depth counter in a threaded program")
{
    const char *program =
        "fun down(n: Int32): Int32\n"
        "  if n == 0 then return 0 end\n"
        "  return 1 + down(n - 1)\n"
        "end\n"
        "var r: Int32 = 0\n"
        "parallel do with r = down(3) end\n";
    CompilerResult threaded = compiler_compile(program, &test_config);
    CompilerResult serial = compiler_compile(
        "fun down(n: Int32): Int32\n"
        "  if n == 0 then return 0 end\n"
        "  return 1 + down(n - 1)\n"
        "end\n"
        "print(down(3))\n", &test_config);
    int ok;

    ok = threaded.succeeded && serial.succeeded &&
         strstr(threaded.c_source, "static _Thread_local size_t nyx_depth_") != NULL &&
         strstr(serial.c_source, "_Thread_local") == NULL;
    compiler_result_free(&threaded);
    compiler_result_free(&serial);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("every allocation failure leaves parallel compilation well defined")
{
    const char *source =
        WORKERS
        "var a: Int32 = 0\n"
        "parallel pool = Store(256) do\n"
        "  with a = square(2)\n"
        "  with var xs = fill(pool, 3)\n"
        "  with square(lane) for lane in range(0, 2)\n"
        "then\n"
        "  print(a, xs.len())\n"
        "end\n";
    CompilerResult baseline;
    size_t count, i;

    nyx_alloc_reset();
    nyx_alloc_begin_counting();
    baseline = compiler_compile(source, &test_config);
    count = nyx_alloc_attempts();
    nyx_alloc_reset();
    if (!baseline.succeeded) {
        fprintf(stderr, "  baseline did not compile: %s\n",
                baseline.error_message);
        compiler_result_free(&baseline);
        return TEST_ERROR;
    }
    for (i = 0; i < count; i++) {
        CompilerResult result;
        bool valid;

        nyx_alloc_fail_after(i);
        result = compiler_compile(source, &test_config);
        nyx_alloc_reset();
        valid = result.succeeded
                    ? result.c_source != NULL &&
                          strcmp(result.c_source, baseline.c_source) == 0
                    : result.c_source == NULL && result.error_message != NULL;
        compiler_result_free(&result);
        if (!valid) {
            fprintf(stderr, "  invalid result after allocation %zu of %zu\n",
                    i, count);
            compiler_result_free(&baseline);
            return TEST_FAIL;
        }
    }
    compiler_result_free(&baseline);
    return TEST_PASS;
}
