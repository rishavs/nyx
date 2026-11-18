/* Channels, timers, and `when`: declaration rules, operation typing, the rules
   for `when`, typed-program validation, and what the generator emits. */

#include "compile_support.h"

#define BOX "type Msg id: Int32 text: String<8> end\nglobal box: Channel<Msg> = Channel(4)\n"

TEST("channels accept any plain element type")
{
    return compile_accepts(
        "type Point x: Int32 y: Float64 end\n"
        "global a: Channel<Bool> = Channel(1)\n"
        "global b: Channel<Int8> = Channel(2)\n"
        "global c: Channel<Int64> = Channel(3)\n"
        "global d: Channel<Float64> = Channel(4)\n"
        "global e: Channel<String<8>> = Channel(5)\n"
        "global f: Channel<Point> = Channel(6)\n"
        "global g: Channel<Array<Int32, 4>> = Channel(7)\n"
        "global h: Channel<Error> = Channel(8)\n"
        "global t: Channel<Int64> = Every(10)\n",
        "channel element types");
}

TEST("a Channel must be global")
{
    TestResult local = compile_fails("var c: Channel<Int32> = Channel(1)\n",
                                     "local channel",
                                     MESSAGE_CHECKER_CHANNEL_NOT_GLOBAL);

    if (local != TEST_PASS) return local;
    return compile_fails("const c: Channel<Int32> = Channel(1)\n",
                         "constant channel", MESSAGE_CHECKER_CONST_STORE);
}

TEST("a channel carries plain values")
{
    TestResult text = compile_fails("global c: Channel<Text> = Channel(1)\n",
                                    "Text channel",
                                    MESSAGE_CHECKER_CHANNEL_ELEMENT);
    TestResult list;

    if (text != TEST_PASS) return text;
    list = compile_fails("global c: Channel<List<Int32>> = Channel(1)\n",
                         "List channel", MESSAGE_CHECKER_CHANNEL_ELEMENT);
    if (list != TEST_PASS) return list;
    return compile_fails(
        "type Named name: Text end\nglobal c: Channel<Named> = Channel(1)\n",
        "struct with Text", MESSAGE_CHECKER_CHANNEL_ELEMENT);
}

TEST("a channel element is not a union")
{
    return compile_fails(
        "global c: Channel<Int32 | Bool> = Channel(1)\n", "union channel",
        MESSAGE_CHECKER_CHANNEL_UNION);
}

TEST("a channel needs a literal capacity in range")
{
    TestResult zero = compile_fails("global c: Channel<Int32> = Channel(-1)\n",
                                    "negative capacity",
                                    MESSAGE_CHECKER_CHANNEL_CAPACITY,
                                    NYX_MAX_CHANNEL_CAPACITY,
                                    NYX_MAX_CHANNEL_BYTES);
    TestResult huge;
    TestResult bytes;
    TestResult variable;

    if (zero != TEST_PASS) return zero;
    huge = compile_fails("global c: Channel<Int32> = Channel(1000000)\n",
                         "too many slots", MESSAGE_CHECKER_CHANNEL_CAPACITY,
                         NYX_MAX_CHANNEL_CAPACITY, NYX_MAX_CHANNEL_BYTES);
    if (huge != TEST_PASS) return huge;
    bytes = compile_fails(
        "global c: Channel<Array<Int64, 4096>> = Channel(3000)\n",
        "too many bytes", MESSAGE_CHECKER_CHANNEL_CAPACITY,
        NYX_MAX_CHANNEL_CAPACITY, NYX_MAX_CHANNEL_BYTES);
    if (bytes != TEST_PASS) return bytes;
    variable = compile_fails(
        "var n: Int32 = 4\nglobal c: Channel<Int32> = Channel(n)\n",
        "variable capacity", MESSAGE_CHECKER_CHANNEL_INITIALIZER, "c");
    return variable;
}

TEST("a channel needs the initialiser Channel(capacity) or Every(ms)")
{
    TestResult plain = compile_fails("global c: Channel<Int32> = 5\n",
                                     "no constructor",
                                     MESSAGE_CHECKER_CHANNEL_INITIALIZER, "c");

    if (plain != TEST_PASS) return plain;
    return compile_fails("global c: Channel<Int32> = Channel(1, 2)\n",
                         "two arguments", MESSAGE_CHECKER_CHANNEL_INITIALIZER,
                         "c");
}

TEST("a timer is a Channel<Int64> with a positive literal interval")
{
    TestResult type = compile_fails("global t: Channel<Int32> = Every(10)\n",
                                    "wrong timer type",
                                    MESSAGE_CHECKER_CHANNEL_TIMER_TYPE);
    TestResult zero;
    TestResult variable;

    if (type != TEST_PASS) return type;
    zero = compile_fails("global t: Channel<Int64> = Every(0)\n", "zero interval",
                         MESSAGE_CHECKER_CHANNEL_TIMER_INTERVAL);
    if (zero != TEST_PASS) return zero;
    variable = compile_fails(
        "var n: Int32 = 5\nglobal t: Channel<Int64> = Every(n)\n",
        "variable interval", MESSAGE_CHECKER_CHANNEL_TIMER_INTERVAL);
    return variable;
}

TEST("a channel name cannot be reused")
{
    TestResult twice = compile_fails(
        "global c: Channel<Int32> = Channel(1)\n"
        "global c: Channel<Int32> = Channel(1)\n",
        "duplicate channel", MESSAGE_CHECKER_DUPLICATE_NAME, "c");

    if (twice != TEST_PASS) return twice;
    return compile_fails(
        "global c: Channel<Int32> = Channel(1)\nfun c() end\n",
        "function named like a channel", MESSAGE_CHECKER_DUPLICATE_NAME, "c");
}

TEST("a Channel is not a value")
{
    TestResult copy = compile_fails(
        "global c: Channel<Int32> = Channel(1)\nvar x = c\n",
        "copying a channel", MESSAGE_CHECKER_CHANNEL_NOT_VALUE, "c");
    TestResult pass;

    if (copy != TEST_PASS) return copy;
    pass = compile_fails(
        "global c: Channel<Int32> = Channel(1)\nfun f(n: Int32) end\nf(c)\n",
        "passing a channel", MESSAGE_CHECKER_CHANNEL_NOT_VALUE, "c");
    if (pass != TEST_PASS) return pass;
    return compile_accepts("fun f(c: Channel<Int32>) end\n",
                           "a channel parameter");
}

TEST("a Channel value is a variable, a parameter, or an argument, nothing more")
{
    TestResult field = compile_fails("type Bag\n  c: Channel<Int32>\nend\n",
                                     "a channel field",
                                     MESSAGE_CHECKER_FIELD_TYPE, "c");
    TestResult result;

    if (field != TEST_PASS) return field;
    result = compile_fails(
        "fun make(s: Store): Channel<Int32>\n  return s.channel<Int32>(2)\nend\n",
        "a channel result", MESSAGE_CHECKER_CHANNEL_USE);
    if (result != TEST_PASS) return result;
    return compile_fails("fun f(xs: List<Channel<Int32>>) end\n",
                         "a channel in a list",
                         MESSAGE_CHECKER_COLLECTION_ELEMENT);
}

TEST("a channel has send, recv, try_send, try_recv, close, and closed")
{
    TestResult unknown = compile_fails(
        BOX "box.peek()\n", "unknown method", MESSAGE_CHECKER_CHANNEL_METHOD,
        "peek");
    TestResult few;
    TestResult many;
    TestResult wrong;

    if (unknown != TEST_PASS) return unknown;
    few = compile_fails(BOX "box.send()\n", "send without a value",
                        MESSAGE_CHECKER_CHANNEL_ARGUMENTS, "send",
                        MESSAGE_FRAGMENT_CHANNEL_VALUE);
    if (few != TEST_PASS) return few;
    many = compile_fails(BOX "box.recv(1)\n", "recv with an argument",
                         MESSAGE_CHECKER_CHANNEL_ARGUMENTS, "recv",
                         MESSAGE_FRAGMENT_NO_ARGUMENTS);
    if (many != TEST_PASS) return many;
    wrong = compile_fails(BOX "box.send(7)\n", "send of the wrong type",
                          MESSAGE_CHECKER_CHANNEL_ARGUMENTS, "send",
                          MESSAGE_FRAGMENT_CHANNEL_VALUE);
    return wrong;
}

TEST("channel operations have the types the language promises")
{
    return compile_accepts(
        BOX
        "var sent: Bool = box.send(Msg(1, \"a\"))\n"
        "var tried: Bool = box.try_send(Msg(2, \"b\"))\n"
        "var got: Msg | Nil = box.recv()\n"
        "var maybe: Msg | Nil = box.try_recv()\n"
        "var done: Bool = box.closed()\n"
        "box.close()\n"
        "if typeof got is Msg then print(got.id) end\n",
        "channel operation types");
}

TEST("a channel cannot be assigned")
{
    return compile_fails(
        BOX "box = 3\n", "assigning a channel",
        MESSAGE_CHECKER_ASSIGN_TARGET, "box");
}

TEST("when is a reserved word")
{
    return compile_fails("var when: Int32 = 1\n", "when as a name",
                         MESSAGE_PARSER_EXPECTED_VARIABLE_NAME);
}

TEST("a when block needs an arm, then, and end")
{
    TestResult none = compile_fails(BOX "when end\n", "when without an arm",
                                    MESSAGE_PARSER_WHEN_ARM);
    TestResult then;

    if (none != TEST_PASS) return none;
    then = compile_fails(BOX "when with box.recv() print(1) end\n",
                         "arm without then", MESSAGE_PARSER_WHEN_THEN);
    if (then != TEST_PASS) return then;
    return compile_fails(BOX "when with box.recv() then print(1)\n",
                         "when without end",
                         MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
}

TEST("a when arm waits on recv or send of a channel")
{
    TestResult call = compile_fails(
        BOX "fun f() end\nwhen with f() then print(1) end\n", "not a channel op",
        MESSAGE_CHECKER_WHEN_OPERATION);
    TestResult close;
    TestResult unknown;

    if (call != TEST_PASS) return call;
    close = compile_fails(BOX "when with box.close() then print(1) end\n",
                          "close in an arm", MESSAGE_CHECKER_WHEN_OPERATION);
    if (close != TEST_PASS) return close;
    unknown = compile_fails("when with nothing.recv() then print(1) end\n",
                            "unknown channel", MESSAGE_CHECKER_UNKNOWN_NAME,
                            "nothing");
    return unknown;
}

TEST("when arms bind a receive and a send, and may have an else")
{
    return compile_accepts(
        BOX
        "global tick: Channel<Int64> = Every(5)\n"
        "when\n"
        "  with m = box.recv() then\n"
        "    if typeof m is Msg then print(m.id) end\n"
        "  with ok = box.send(Msg(1, \"x\")) then\n"
        "    var again: Bool = ok\n"
        "  with box.recv() then\n"
        "    print(\"unbound\")\n"
        "  with t = tick.recv() then\n"
        "    print(\"tick\")\n"
        "else\n"
        "  print(\"idle\")\n"
        "end\n",
        "when forms");
}

TEST("break and continue in a when arm reach the enclosing loop")
{
    TestResult inside = compile_accepts(
        BOX
        "while true do\n"
        "  when\n"
        "    with m = box.recv() then\n"
        "      if typeof m is Msg then continue else break end\n"
        "  end\n"
        "end\n",
        "break and continue in arms");

    if (inside != TEST_PASS) return inside;
    return compile_fails(
        BOX "when with m = box.recv() then break end\n",
        "break outside a loop", MESSAGE_CHECKER_BREAK_OUTSIDE_LOOP);
}

TEST("a function whose when arms all return needs no final return")
{
    TestResult ok = compile_accepts(
        BOX
        "fun f(): Int32\n"
        "  when\n"
        "    with m = box.recv() then return 1\n"
        "  else\n"
        "    return 2\n"
        "  end\n"
        "end\n",
        "all arms return");

    if (ok != TEST_PASS) return ok;
    return compile_fails(
        BOX
        "fun f(): Int32\n"
        "  when\n"
        "    with m = box.recv() then return 1\n"
        "    with box.recv() then print(2)\n"
        "  end\n"
        "end\n",
        "an arm that does not return", MESSAGE_CHECKER_FUNCTION_MAY_NOT_RETURN, "f");
}

TEST("code after a when whose arms all return is unreachable")
{
    return compile_fails(
        BOX
        "fun f(): Int32\n"
        "  when\n"
        "    with m = box.recv() then return 1\n"
        "  end\n"
        "  return 2\n"
        "end\n",
        "unreachable after when", MESSAGE_CHECKER_UNREACHABLE_CODE);
}

TEST("a while true loop with a break inside a when is not endless")
{
    return compile_accepts(
        BOX
        "fun f(): Int32\n"
        "  while true do\n"
        "    when\n"
        "      with m = box.recv() then break\n"
        "    end\n"
        "  end\n"
        "  return 1\n"
        "end\n",
        "break inside a when");
}

TEST("channels work through parallel arms and defer")
{
    return compile_accepts(
        BOX
        "fun send_all() box.send(Msg(1, \"a\")) box.close() end\n"
        "fun drain()\n"
        "  defer do print(\"drained\") end\n"
        "  while true do\n"
        "    var m = box.recv()\n"
        "    if typeof m is Msg then print(m.id) else break end\n"
        "  end\n"
        "end\n"
        "parallel do with send_all() with drain() end\n",
        "channels in arms");
}

/* Hand-built programs: the analyser must reject malformed channel nodes. */
static void channel_setup(TypedProgram *program, ErrorState *errors,
                          size_t *int64_type, size_t *bool_type,
                          size_t *nil_type, size_t *union_type)
{
    TypedType type = {0};
    size_t alternatives[2];

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
    alternatives[0] = *int64_type;
    alternatives[1] = *nil_type;
    type.kind = NYX_TYPE_UNION;
    type.first_child = program->type_children.length;
    type.child_count = 2;
    typed_program_add_type_child(program, alternatives[0]);
    typed_program_add_type_child(program, alternatives[1]);
    typed_program_add_type(program, &type, union_type);
}

static TypedNode channel_node(TypedNodeKind kind, size_t type_index)
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

static bool channel_rejects(TypedProgram *program, ErrorState *errors,
                            const char *format, size_t index)
{
    char expected[256];
    bool ok;

    expect_message(expected, sizeof(expected), format, index);
    ok = !analyser_analyse(program, NULL, errors) && error_contains(errors, expected);
    typed_program_free(program);
    return ok;
}

TEST("a channel declaration of a union element is rejected")
{
    TypedProgram program;
    ErrorState errors;
    size_t i64, boolean, nil, optional, index;
    TypedNode node;

    channel_setup(&program, &errors, &i64, &boolean, &nil, &optional);
    node = channel_node(TYPED_CHANNEL_DECLARATION, optional);
    node.auxiliary_type = optional;
    node.integer_value = 2;
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    return channel_rejects(&program, &errors,
                           MESSAGE_ANALYSER_INVALID_TYPE_INDEX, index)
               ? TEST_PASS : TEST_FAIL;
}

TEST("a channel operation on an undeclared channel is rejected")
{
    TypedProgram program;
    ErrorState errors;
    size_t i64, boolean, nil, optional, index;
    TypedNode node;

    channel_setup(&program, &errors, &i64, &boolean, &nil, &optional);
    node = channel_node(TYPED_CHANNEL_OP, boolean);
    node.integer_value = CHANNEL_OP_CLOSED;
    node.store_id = 4;
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    return channel_rejects(&program, &errors,
                           MESSAGE_ANALYSER_INVALID_NODE_LINK, index)
               ? TEST_PASS : TEST_FAIL;
}

TEST("a send without a value is rejected")
{
    TypedProgram program;
    ErrorState errors;
    size_t i64, boolean, nil, optional, declaration, index;
    TypedNode node;

    channel_setup(&program, &errors, &i64, &boolean, &nil, &optional);
    node = channel_node(TYPED_CHANNEL_DECLARATION, i64);
    node.auxiliary_type = optional;
    node.integer_value = 2;
    node.store_id = 0;
    if (!typed_program_push(&program, &node, &declaration) ||
        !list_push(&program.roots, &declaration)) return TEST_ERROR;
    node = channel_node(TYPED_CHANNEL_OP, boolean);
    node.integer_value = CHANNEL_OP_SEND;
    node.store_id = 0;
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    return channel_rejects(&program, &errors,
                           MESSAGE_ANALYSER_INVALID_NODE_LINK, index)
               ? TEST_PASS : TEST_FAIL;
}

TEST("a when without arms is rejected")
{
    TypedProgram program;
    ErrorState errors;
    size_t i64, boolean, nil, optional, index;
    TypedNode node;

    channel_setup(&program, &errors, &i64, &boolean, &nil, &optional);
    node = channel_node(TYPED_WHEN, TYPED_NO_NODE);
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    return channel_rejects(&program, &errors,
                           MESSAGE_ANALYSER_INVALID_NODE_LINK, index)
               ? TEST_PASS : TEST_FAIL;
}

TEST("a when arm without a channel operation is rejected")
{
    TypedProgram program;
    ErrorState errors;
    size_t i64, boolean, nil, optional, index;
    TypedNode node;

    channel_setup(&program, &errors, &i64, &boolean, &nil, &optional);
    node = channel_node(TYPED_WHEN_ARM, TYPED_NO_NODE);
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    return channel_rejects(&program, &errors,
                           MESSAGE_ANALYSER_INVALID_NODE_LINK, index)
               ? TEST_PASS : TEST_FAIL;
}

TEST("a when result that names no declaring arm is rejected")
{
    TypedProgram program;
    ErrorState errors;
    size_t i64, boolean, nil, optional, index;
    TypedNode node;

    channel_setup(&program, &errors, &i64, &boolean, &nil, &optional);
    node = channel_node(TYPED_WHEN_RESULT, TYPED_NO_NODE);
    node.integer_value_u = 77;
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    return channel_rejects(&program, &errors,
                           MESSAGE_ANALYSER_INVALID_NODE_LINK, index)
               ? TEST_PASS : TEST_FAIL;
}

TEST("channel runtime appears only when a program declares a channel")
{
    CompilerResult used = compiler_compile(
        BOX "var ok: Bool = box.send(Msg(1, \"a\"))\n", &test_config);
    CompilerResult unused = compiler_compile("var n: Int32 = 1\n", &test_config);
    int ok;

    ok = used.succeeded && unused.succeeded &&
         strstr(used.c_source, "#include <pthread.h>") != NULL &&
         strstr(used.c_source, "nyx_chans[1]") != NULL &&
         strstr(used.c_source, "nyx_chan_t") != NULL &&
         strstr(used.c_source, "nyx_chanref") == NULL &&
         strstr(used.c_source, "nyx_chan_arm_started") == NULL &&
         strstr(unused.c_source, "nyx_chan") == NULL &&
         strstr(unused.c_source, "pthread") == NULL;
    compiler_result_free(&used);
    compiler_result_free(&unused);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("thread accounting appears only with both channels and parallel")
{
    const char *both =
        BOX "fun a() box.send(Msg(1, \"a\")) end\nparallel do with a() end\n";
    CompilerResult with_both = compiler_compile(both, &test_config);
    CompilerResult parallel_only = compiler_compile(
        "fun a() end\nparallel do with a() end\n", &test_config);
    int ok;

    ok = with_both.succeeded && parallel_only.succeeded &&
         strstr(with_both.c_source, "nyx_chan_arm_started") != NULL &&
         strstr(with_both.c_source, "nyx_chan_join_begin") != NULL &&
         strstr(with_both.c_source, "nyx_chan_arm_finished") != NULL &&
         strstr(parallel_only.c_source, "nyx_chan") == NULL;
    compiler_result_free(&with_both);
    compiler_result_free(&parallel_only);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("every allocation failure leaves channel compilation well defined")
{
    const char *source =
        BOX
        "global tick: Channel<Int64> = Every(5)\n"
        "fun a()\n"
        "  while true do\n"
        "    when\n"
        "      with m = box.recv() then\n"
        "        if typeof m is Msg then print(m.id) else break end\n"
        "      with t = tick.recv() then print(t)\n"
        "    else\n"
        "      print(\"idle\")\n"
        "    end\n"
        "  end\n"
        "end\n"
        "parallel do with a() end\n";
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

TEST("a rendezvous channel (capacity 0) refuses try_send")
{
    return compile_fails(
        "global r: Channel<Int64> = Channel(0)\nvar ok: Bool = r.try_send(1)\n",
        "rendezvous try_send", MESSAGE_CHECKER_CHANNEL_RENDEZVOUS_SEND);
}

TEST("a rendezvous channel refuses a when send arm")
{
    return compile_fails(
        "global r: Channel<Int64> = Channel(0)\n"
        "when with ok = r.send(1) then print(ok) end\n",
        "rendezvous when send", MESSAGE_CHECKER_CHANNEL_RENDEZVOUS_SEND);
}

#define POOL_FN "fun nothing() end\n"

TEST("pool.channel<T>(n) makes a Channel<T> that arms, when, and the then section use")
{
    return compile_accepts(
        "fun feed(c: Channel<Int64>) c.send(1) end\n"
        "fun drain(a: Channel<Int64>, b: Channel<Int64>)\n"
        "  when\n"
        "    with x = a.recv() then print(1)\n"
        "    with ok = b.send(2) then print(2)\n"
        "  end\n"
        "end\n"
        "parallel pool = Store(4096) do\n"
        "  var a = pool.channel<Int64>(4)\n"
        "  const b: Channel<Int64> = pool.channel<Int64>(4)\n"
        "  with feed(a)\n"
        "  with drain(a, b)\n"
        "then\n"
        "  var v = a.try_recv()\n"
        "  print(a.closed())\n"
        "end\n",
        "pool channels");
}

TEST("a pool channel's element follows the rules of a global channel's")
{
    TestResult text = compile_fails(
        POOL_FN "parallel pool = Store(1024) do\n"
        "  var c = pool.channel<Text>(2)\n  with nothing()\nend\n",
        "a Text element", MESSAGE_CHECKER_CHANNEL_ELEMENT);
    TestResult nil;

    if (text != TEST_PASS) return text;
    nil = compile_fails(
        POOL_FN "parallel pool = Store(1024) do\n"
        "  var c = pool.channel<Int32 | Bool>(2)\n  with nothing()\nend\n",
        "a union element", MESSAGE_CHECKER_CHANNEL_UNION);
    return nil;
}

TEST("pool.channel needs one element type and one capacity")
{
    TestResult no_type = compile_fails(
        POOL_FN "parallel pool = Store(1024) do\n"
        "  var c = pool.channel(2)\n  with nothing()\nend\n",
        "no type argument", MESSAGE_CHECKER_CHANNEL_CREATE);
    TestResult no_capacity;

    if (no_type != TEST_PASS) return no_type;
    no_capacity = compile_fails(
        POOL_FN "parallel pool = Store(1024) do\n"
        "  var c = pool.channel<Int32>()\n  with nothing()\nend\n",
        "no capacity", MESSAGE_CHECKER_CHANNEL_CREATE);
    return no_capacity;
}

TEST("a Channel value cannot escape the block whose pool holds it")
{
    return compile_fails(
        POOL_FN "parallel p0 = Store(1024) do\n"
        "  var keep = p0.channel<Int32>(2)\n  with nothing()\n"
        "then\n"
        "  parallel pool = Store(1024) do\n"
        "    var c = pool.channel<Int32>(2)\n    with nothing()\n"
        "  then\n"
        "    keep = c\n"
        "  end\n"
        "end\n",
        "a channel assigned outward", MESSAGE_ANALYSER_ESCAPE_ASSIGN, MESSAGE_FRAGMENT_STORE_PREFIX "pool\'",
        "keep");
}

TEST("declarations may precede a block's arms, and nothing else may")
{
    TestResult statement = compile_fails(
        POOL_FN "parallel do\n  print(1)\n  with nothing()\nend\n",
        "a statement before the arms", MESSAGE_PARSER_PARALLEL_ARM);
    TestResult plain;

    if (statement != TEST_PASS) return statement;
    plain = compile_accepts(
        POOL_FN "parallel do\n  var n: Int32 = 3\n  with nothing()\nthen\n"
        "  print(n)\nend\n",
        "a declaration before the arms");
    return plain;
}

TEST("a pool channel's operations are typed like a global channel's")
{
    return compile_fails(
        POOL_FN "parallel pool = Store(1024) do\n"
        "  var c = pool.channel<Int32>(2)\n"
        "  with nothing()\n"
        "then\n"
        "  c.send(\"text\")\n"
        "end\n",
        "a wrong send type", MESSAGE_CHECKER_CHANNEL_ARGUMENTS, "send",
        MESSAGE_FRAGMENT_CHANNEL_VALUE);
}
