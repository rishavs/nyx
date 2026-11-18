/* The defer statement: syntax, the checker rules that keep an unwinding exit
   well defined, and typed-program validation. */

#include "compile_support.h"

TEST("defer requires do")
{
    return compile_fails("fun f()\n  defer print(1)\nend\n",
                         "defer without do", MESSAGE_PARSER_EXPECTED_DEFER_DO);
}

TEST("defer requires end")
{
    return compile_fails("fun f()\n  defer do print(1)\nend\n",
                         "defer without end",
                         MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
}

TEST("defer is a reserved word")
{
    return compile_fails("var defer: Int32 = 1\n", "defer as a name",
                         MESSAGE_PARSER_EXPECTED_VARIABLE_NAME);
}

TEST("defer must be inside a block")
{
    return compile_fails("defer do print(1) end\n", "program-level defer",
                         MESSAGE_CHECKER_DEFER_TOP_LEVEL);
}

TEST("defer is accepted in function bodies and in nested blocks")
{
    return compile_accepts(
        "fun f(n: Int32): Int32\n"
        "  defer do print(1) end\n"
        "  while n > 0 do\n"
        "    defer do print(2) end\n"
        "    break\n"
        "  end\n"
        "  if n > 1 then\n"
        "    defer do print(3) end\n"
        "  end\n"
        "  scope s = Store(16) do\n"
        "    defer do print(4) end\n"
        "  end\n"
        "  return n\n"
        "end\n"
        "while true do\n"
        "  defer do print(5) end\n"
        "  break\n"
        "end\n",
        "defer in nested blocks");
}

TEST("a defer block cannot return")
{
    return compile_fails("fun f()\n  defer do return end\nend\n",
                         "return in defer", MESSAGE_CHECKER_DEFER_EXIT);
}

TEST("a defer block cannot break or continue out of itself")
{
    TestResult broke = compile_fails(
        "fun f()\n  while true do\n    defer do break end\n  end\nend\n",
        "break in defer", MESSAGE_CHECKER_DEFER_EXIT);

    if (broke != TEST_PASS) return broke;
    return compile_fails(
        "fun f()\n  while true do\n    defer do continue end\n    break\n  end\nend\n",
        "continue in defer", MESSAGE_CHECKER_DEFER_EXIT);
}

TEST("a defer block may contain its own loops with break and continue")
{
    return compile_accepts(
        "fun f()\n"
        "  defer do\n"
        "    for i in range(0, 3) do\n"
        "      if i == 1 then continue end\n"
        "      if i == 2 then break end\n"
        "    end\n"
        "  end\n"
        "end\n",
        "loops inside a defer");
}

TEST("narrowing does not carry into a defer block")
{
    /* The defer runs when the block exits, after `x = nil`, so the type test
       above it proves nothing there. The same read is fine without a defer. */
    TestResult control = compile_accepts(
        "fun f(x: Int32 | Nil)\n"
        "  if typeof x is Int32 then\n"
        "    var y: Int32 = x\n"
        "    print(y)\n"
        "  end\n"
        "end\n",
        "narrowed read outside a defer");

    if (control != TEST_PASS) return control;
    return compile_fails(
        "fun f(x: Int32 | Nil)\n"
        "  if typeof x is Int32 then\n"
        "    defer do\n"
        "      var y: Int32 = x\n"
        "      print(y)\n"
        "    end\n"
        "    x = nil\n"
        "  end\n"
        "end\n",
        "narrowed read inside a defer",
        MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, "y", "Int32");
}

TEST("a defer block can test and narrow its own values")
{
    return compile_accepts(
        "fun f(x: Int32 | Nil)\n"
        "  defer do\n"
        "    if typeof x is Int32 then print(x) end\n"
        "  end\n"
        "end\n",
        "narrowing inside a defer");
}

/* The analyser validates typed-program links, so a hand-built defer with the
   wrong shape must be rejected before the generator emits it. */
TEST("a defer that does not hold a block is rejected")
{
    TypedProgram program;
    ErrorState errors;
    TypedNode node = {0};
    size_t index;
    char expected[256];
    bool ok;

    typed_program_init(&program);
    error_init(&errors);
    node.kind = TYPED_DEFER;
    node.type_index = TYPED_NO_NODE;
    node.name_index = TYPED_NO_NODE;
    node.auxiliary_type = TYPED_NO_NODE;
    node.left_child = TYPED_NO_NODE;
    node.right_child = TYPED_NO_NODE;
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_NODE_LINK, index);
    ok = !analyser_analyse(&program, NULL, &errors) &&
         error_contains(&errors, expected);
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("a defer with a second child is rejected")
{
    TypedProgram program;
    ErrorState errors;
    TypedNode node = {0};
    size_t block, index;
    char expected[256];
    bool ok;

    typed_program_init(&program);
    error_init(&errors);
    node.kind = TYPED_BLOCK;
    node.type_index = TYPED_NO_NODE;
    node.name_index = TYPED_NO_NODE;
    node.auxiliary_type = TYPED_NO_NODE;
    node.left_child = TYPED_NO_NODE;
    node.right_child = TYPED_NO_NODE;
    if (!typed_program_push(&program, &node, &block)) return TEST_ERROR;
    node.kind = TYPED_DEFER;
    node.left_child = block;
    node.right_child = block;
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_NODE_LINK, index);
    ok = !analyser_analyse(&program, NULL, &errors) &&
         error_contains(&errors, expected);
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("every allocation failure leaves defer compilation well defined")
{
    const char *source =
        "fun f(x: Int32 | Nil, n: Int32): Int32\n"
        "  defer do\n"
        "    if typeof x is Int32 then print(x) end\n"
        "  end\n"
        "  for i in range(0, n) do\n"
        "    defer do print(i) end\n"
        "    if i == 1 then continue end\n"
        "  end\n"
        "  return n\n"
        "end\n"
        "print(f(1, 2))\n";
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
