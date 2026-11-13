/* Checker rules for functions and calls: arity, argument and return types,
   and generics. */

#include "compile_support.h"

TEST("unknown call")
{
    return compile_fails("var p = Foo(1, 2)\n",
                             "an unknown call",
                             MESSAGE_CHECKER_UNKNOWN_CALL, "Foo");
}

TEST("not callable")
{
    return compile_fails(
        "fun f(x:Int32):Int32\n  return x\nend\n"
        "var y : Int32 = 1\n"
        "var z : Int32 = y(1)\n",
        "calling a non-function value",
        MESSAGE_CHECKER_NOT_CALLABLE, "y");
}

TEST("named function argument")
{
    return compile_fails(
        "fun f(x:Int32):Int32\n  return x\nend\n"
        "var y : Int32 = f(x = 1)\n",
        "a named function argument",
        MESSAGE_CHECKER_NAMED_FUNCTION_ARG);
}

TEST("function argument count")
{
    return compile_fails(
        "fun f(x:Int32):Int32\n  return x\nend\n"
        "var y : Int32 = f(1, 2)\n",
        "a function call with the wrong argument count",
        MESSAGE_CHECKER_FUNCTION_ARG_COUNT, "f", (size_t)1, (size_t)2);
}

TEST("function argument type")
{
    return compile_fails(
        "type P x:Int32 end\n"
        "fun f(p:P):Int32\n  return 1\nend\n"
        "var y : Int32 = f(1)\n",
        "a function call with the wrong argument type",
        MESSAGE_CHECKER_FUNCTION_ARG_TYPE, (size_t)1, "f");
}

TEST("return type mismatch")
{
    return compile_fails(
        "type P x:Int32 end\n"
        "fun f(x:Int32):P\n  return x\nend\n",
        "a return expression with the wrong type",
        MESSAGE_CHECKER_RETURN_TYPE_MISMATCH, "f");
}

TEST("function may not return")
{
    return compile_fails(
        "fun f(x:Int32):Int32\n"
        "  if x > 0 then\n    return 1\n  end\n"
        "end\n",
        "a function with a missing return path",
        MESSAGE_CHECKER_FUNCTION_MAY_NOT_RETURN, "f");
}

TEST("missing return")
{
    Ast ast;
    TypedProgram program;
    ErrorState errors;
    AstNode type_node = {0};
    AstNode function = {0};
    char expected[256];
    size_t function_index = 1;

    ast_init(&ast);
    typed_program_init(&program);
    error_init(&errors);

    type_node.kind = AST_TYPE_NAME;
    type_node.name_index = ast_intern_symbol(&ast, "Int32", 5);
    list_push(&ast.nodes, &type_node);

    function.kind = AST_FUNCTION_DECLARATION;
    function.name_index = ast_intern_symbol(&ast, "f", 1);
    function.left_child = AST_NO_NODE;
    function.right_child = 0;
    function.children_start = 0;
    function.children_count = 0;
    list_push(&ast.nodes, &function);
    list_push(&ast.roots, &function_index);

    expect_message(expected, sizeof(expected), MESSAGE_CHECKER_MISSING_RETURN,
                   "f");
    if (checker_check(&ast, &program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  a function without a return body was not rejected\n");
        ast_free(&ast);
        typed_program_free(&program);
        return TEST_FAIL;
    }
    ast_free(&ast);
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("cannot infer type parameter")
{
    const char *source =
        "fun make<T>(): T\n"
        "  return nil\n"
        "end\n"
        "\n"
        "var x: Int32 = make()\n";

    return compile_fails(source, "a call with no type to infer",
                         MESSAGE_CHECKER_CANNOT_INFER_TYPE, "T", "make");
}

TEST("generic function as value")
{
    const char *source =
        "fun identity<T>(x: T): T\n"
        "  return x\n"
        "end\n"
        "\n"
        "var f = identity\n";

    return compile_fails(source, "a generic function used as a value",
                         MESSAGE_CHECKER_GENERIC_AS_VALUE, "identity");
}
