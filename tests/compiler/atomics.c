/* Atomic<T>: declaration rules, the three operations, typed-program
   validation, and what the generator emits. */

#include "compile_support.h"

TEST("atomics accept a Bool and every integer type")
{
    return compile_accepts(
        "global a: Atomic<Bool> = Atomic(true)\n"
        "global b: Atomic<Int8> = Atomic(-128)\n"
        "global c: Atomic<Int16> = Atomic(0)\n"
        "global d: Atomic<Int32> = Atomic(7)\n"
        "global e: Atomic<Int64> = Atomic(-9)\n"
        "global f: Atomic<Uint8> = Atomic(255)\n"
        "global g: Atomic<Uint16> = Atomic(1)\n"
        "global h: Atomic<Uint32> = Atomic(2)\n"
        "global i: Atomic<Uint64> = Atomic(3)\n"
        "global j: Atomic<Byte> = Atomic(4)\n"
        "var n: Int32 = d.add(1)\n"
        "a.store(false)\n",
        "atomic element types");
}

TEST("an Atomic must be global")
{
    return compile_fails("var a: Atomic<Int32> = Atomic(0)\n", "local atomic",
                         MESSAGE_CHECKER_ATOMIC_NOT_GLOBAL);
}

TEST("an Atomic holds only a Bool or an integer")
{
    TestResult real = compile_fails(
        "global a: Atomic<Float64> = Atomic(0.0)\n", "float atomic",
        MESSAGE_CHECKER_ATOMIC_TYPE);

    if (real != TEST_PASS) return real;
    return compile_fails("global a: Atomic<Text> = Atomic(\"x\")\n",
                         "text atomic", MESSAGE_CHECKER_ATOMIC_TYPE);
}

TEST("an Atomic needs a constant initialiser of its type")
{
    TestResult plain = compile_fails("global a: Atomic<Int32> = 5\n",
                                     "missing constructor",
                                     MESSAGE_CHECKER_ATOMIC_INITIALIZER, "a");
    TestResult computed;

    if (plain != TEST_PASS) return plain;
    computed = compile_fails(
        "var n: Int32 = 1\nglobal a: Atomic<Int32> = Atomic(n)\n",
        "non-constant initialiser", MESSAGE_CHECKER_ATOMIC_CONSTANT, "a");
    if (computed != TEST_PASS) return computed;
    return compile_fails("global a: Atomic<Int8> = Atomic(300)\n",
                         "out-of-range initialiser",
                         MESSAGE_CHECKER_INTEGER_RANGE);
}

TEST("an Atomic name cannot be reused")
{
    TestResult twice = compile_fails(
        "global a: Atomic<Int32> = Atomic(0)\n"
        "global a: Atomic<Int32> = Atomic(1)\n",
        "duplicate atomic", MESSAGE_CHECKER_DUPLICATE_NAME, "a");

    if (twice != TEST_PASS) return twice;
    return compile_fails(
        "global a: Atomic<Int32> = Atomic(0)\nfun a() end\n",
        "function named like an atomic", MESSAGE_CHECKER_DUPLICATE_NAME, "a");
}

TEST("an Atomic is not a value")
{
    TestResult copy = compile_fails(
        "global a: Atomic<Int32> = Atomic(0)\nvar x = a\n", "copying an atomic",
        MESSAGE_CHECKER_ATOMIC_NOT_VALUE, "a");
    TestResult pass;

    if (copy != TEST_PASS) return copy;
    pass = compile_fails(
        "global a: Atomic<Int32> = Atomic(0)\nfun f(x: Int32) end\nf(a)\n",
        "passing an atomic", MESSAGE_CHECKER_ATOMIC_NOT_VALUE, "a");
    if (pass != TEST_PASS) return pass;
    return compile_fails("fun f(x: Atomic<Int32>) end\n",
                         "an atomic parameter", MESSAGE_CHECKER_ATOMIC_USE);
}

TEST("an Atomic has load, store, and add")
{
    TestResult unknown = compile_fails(
        "global a: Atomic<Int32> = Atomic(0)\na.reset()\n", "unknown method",
        MESSAGE_CHECKER_ATOMIC_METHOD, "reset");
    TestResult few;
    TestResult many;
    TestResult bad;

    if (unknown != TEST_PASS) return unknown;
    few = compile_fails("global a: Atomic<Int32> = Atomic(0)\na.store()\n",
                        "store without a value",
                        MESSAGE_CHECKER_ATOMIC_ARGUMENTS, "store",
                        MESSAGE_FRAGMENT_ATOMIC_VALUE);
    if (few != TEST_PASS) return few;
    many = compile_fails("global a: Atomic<Int32> = Atomic(0)\na.load(1)\n",
                         "load with an argument",
                         MESSAGE_CHECKER_ATOMIC_ARGUMENTS, "load",
                         MESSAGE_FRAGMENT_NO_ARGUMENTS);
    if (many != TEST_PASS) return many;
    bad = compile_fails("global a: Atomic<Int32> = Atomic(0)\na.add(true)\n",
                        "add of the wrong type",
                        MESSAGE_CHECKER_ATOMIC_ARGUMENTS, "add",
                        MESSAGE_FRAGMENT_ATOMIC_VALUE);
    if (bad != TEST_PASS) return bad;
    return compile_fails("global a: Atomic<Bool> = Atomic(true)\na.add(1)\n",
                         "add on a Bool", MESSAGE_CHECKER_ATOMIC_ADD_TYPE);
}

TEST("an Atomic cannot be assigned")
{
    char expected[128];

    expect_message(expected, sizeof(expected), MESSAGE_CHECKER_ASSIGN_TARGET,
                   "a");
    return compile_fails(
        "global a: Atomic<Int32> = Atomic(0)\na = 3\n", "assigning an atomic",
        MESSAGE_CHECKER_ASSIGN_TARGET, "a");
}

TEST("atomic operations have the cell's type")
{
    return compile_accepts(
        "global total: Atomic<Int64> = Atomic(0)\n"
        "fun ticket(): Int64 return total.add(1) end\n"
        "var seen: Int64 = total.load() + ticket()\n"
        "total.store(seen)\n",
        "atomic operation types");
}

/* Hand-built programs: the analyser must reject a malformed atomic node. */
static void atomic_setup(TypedProgram *program, ErrorState *errors,
                         size_t *int64_type, size_t *bool_type,
                         size_t *nil_type, size_t *float_type)
{
    TypedType type = {0};

    typed_program_init(program);
    error_init(errors);
    type.name_index = TYPED_NO_NODE;
    type.signature_index = TYPED_NO_NODE;
    type.kind = NYX_TYPE_INT64;
    typed_program_add_type(program, &type, int64_type);
    type.kind = NYX_TYPE_BOOL;
    typed_program_add_type(program, &type, bool_type);
    type.kind = NYX_TYPE_NIL;
    typed_program_add_type(program, &type, nil_type);
    type.kind = NYX_TYPE_FLOAT64;
    typed_program_add_type(program, &type, float_type);
}

static TypedNode atomic_node(TypedNodeKind kind, size_t type_index)
{
    TypedNode node = {0};

    node.kind = kind;
    node.type_index = type_index;
    node.name_index = TYPED_NO_NODE;
    node.auxiliary_type = TYPED_NO_NODE;
    node.left_child = TYPED_NO_NODE;
    node.right_child = TYPED_NO_NODE;
    return node;
}

TEST("an atomic cell of a float type is rejected")
{
    TypedProgram program;
    ErrorState errors;
    size_t i64, boolean, nil, real, index;
    TypedNode node;
    char expected[256];
    bool ok;

    atomic_setup(&program, &errors, &i64, &boolean, &nil, &real);
    node = atomic_node(TYPED_ATOMIC_DECLARATION, real);
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_TYPE_INDEX, index);
    ok = !analyser_analyse(&program, NULL, &errors) &&
         error_contains(&errors, expected);
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("an atomic add on a Bool is rejected")
{
    TypedProgram program;
    ErrorState errors;
    size_t i64, boolean, nil, real, literal, index;
    TypedNode node;
    char expected[256];
    bool ok;

    atomic_setup(&program, &errors, &i64, &boolean, &nil, &real);
    node = atomic_node(TYPED_BOOL_LITERAL, boolean);
    if (!typed_program_push(&program, &node, &literal)) return TEST_ERROR;
    node = atomic_node(TYPED_ATOMIC_ADD, boolean);
    node.left_child = literal;
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_TYPE_INDEX, index);
    ok = !analyser_analyse(&program, NULL, &errors) &&
         error_contains(&errors, expected);
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("an atomic store without a value is rejected")
{
    TypedProgram program;
    ErrorState errors;
    size_t i64, boolean, nil, real, index;
    TypedNode node;
    char expected[256];
    bool ok;

    atomic_setup(&program, &errors, &i64, &boolean, &nil, &real);
    node = atomic_node(TYPED_ATOMIC_STORE, nil);
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_NODE_LINK, index);
    ok = !analyser_analyse(&program, NULL, &errors) &&
         error_contains(&errors, expected);
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("an atomic load with an operand is rejected")
{
    TypedProgram program;
    ErrorState errors;
    size_t i64, boolean, nil, real, literal, index;
    TypedNode node;
    char expected[256];
    bool ok;

    atomic_setup(&program, &errors, &i64, &boolean, &nil, &real);
    node = atomic_node(TYPED_INTEGER_LITERAL, i64);
    if (!typed_program_push(&program, &node, &literal)) return TEST_ERROR;
    node = atomic_node(TYPED_ATOMIC_LOAD, i64);
    node.left_child = literal;
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_NODE_LINK, index);
    ok = !analyser_analyse(&program, NULL, &errors) &&
         error_contains(&errors, expected);
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("the atomics header and wrappers appear only when atomics are used")
{
    CompilerResult used = compiler_compile(
        "global a: Atomic<Int32> = Atomic(0)\na.add(1)\n", &test_config);
    CompilerResult unused = compiler_compile("var n: Int32 = 1\n", &test_config);
    int ok;

    ok = used.succeeded && unused.succeeded &&
         strstr(used.c_source, "#include <stdatomic.h>") != NULL &&
         strstr(used.c_source, "nyx_atomic_add") != NULL &&
         strstr(used.c_source, "memory_order_seq_cst") != NULL &&
         strstr(unused.c_source, "stdatomic") == NULL &&
         strstr(unused.c_source, "nyx_atomic") == NULL;
    compiler_result_free(&used);
    compiler_result_free(&unused);
    return ok ? TEST_PASS : TEST_FAIL;
}
