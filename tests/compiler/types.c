/* Checker rules for types: aliases, generics, unions, and recursion. */

#include "compile_support.h"

TEST("unknown type")
{
    return compile_fails("var x : Float = 13\n", "an unknown variable type",
                             MESSAGE_CHECKER_UNKNOWN_TYPE, "Float", "x");
}

TEST("type has no fields")
{
    Ast ast;
    TypedProgram program;
    ErrorState errors;
    AstNode node = {0};
    size_t root = 0;

    ast_init(&ast);
    typed_program_init(&program);
    error_init(&errors);
    node.kind = AST_TYPE_DECLARATION;
    node.name_index = ast_intern_symbol(&ast, "Point", 5);
    node.left_child = AST_NO_NODE;
    node.right_child = AST_NO_NODE;
    node.children_start = 0;
    node.children_count = 0;
    list_push(&ast.nodes, &node);
    list_push(&ast.roots, &root);

    if (checker_check(&ast, &program, &errors) ||
        !error_contains(&errors, MESSAGE_CHECKER_TYPE_HAS_NO_FIELDS)) {
        fprintf(stderr, "  fieldless type did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    ast_free(&ast);
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("generic type without arguments")
{
    const char *source =
        "type Box<T>\n"
        "  value: T\n"
        "end\n"
        "\n"
        "var b: Box = Box(1)\n";

    return compile_fails(source, "a generic type without arguments",
                         MESSAGE_CHECKER_MISSING_TYPE_ARGUMENTS, "Box");
}

TEST("type arguments on non-generic type")
{
    const char *source =
        "type Point\n"
        "  x: Int32\n"
        "end\n"
        "\n"
        "var p: Point<Int32> = Point(1)\n";

    return compile_fails(source, "arguments on a non-generic type",
                         MESSAGE_CHECKER_TYPE_NOT_GENERIC, "Point");
}

TEST("generic type argument count")
{
    const char *source =
        "type Box<T>\n"
        "  value: T\n"
        "end\n"
        "\n"
        "var b: Box<Int32, Bool> = Box(1)\n";

    return compile_fails(source, "a generic type with too many arguments",
                         MESSAGE_CHECKER_TYPE_ARGUMENT_COUNT, "Box",
                         (size_t)1, (size_t)2);
}

TEST("duplicate type parameter")
{
    const char *source =
        "type Pair<T, T>\n"
        "  first: T\n"
        "  second: T\n"
        "end\n";

    return compile_fails(source, "a duplicate type parameter",
                         MESSAGE_CHECKER_DUPLICATE_TYPE_PARAMETER, "T");
}

TEST("is-test target is a union")
{
    const char *source =
        "fun f(x: Int32 | Bool): Int32\n"
        "  if typeof x is Int32 | Bool then\n"
        "    return 0\n"
        "  end\n"
        "  return 1\n"
        "end\n";

    return compile_fails(source, "an is-test whose target is a union",
                         MESSAGE_CHECKER_IS_TYPE_IS_UNION);
}

TEST("recursive alias")
{
    const char *source =
        "type Loop = Loop | Int32\n"
        "var x: Loop = 1\n";

    return compile_fails(source, "a recursive alias",
                         MESSAGE_CHECKER_RECURSIVE_TYPE, "Loop");
}

TEST("function field")
{
    const char *source =
        "type Holder\n"
        "  f: Fun<(Int32):Int32>\n"
        "end\n";

    return compile_fails(source, "a function-typed struct field",
                         MESSAGE_CHECKER_FIELD_TYPE, "f");
}

TEST("recursive struct")
{
    return compile_fails("type Box\n  value: Box\nend\n",
                         "a recursive struct",
                         MESSAGE_CHECKER_RECURSIVE_TYPE, "Box");
}

TEST("recursive generic struct")
{
    const char *source =
        "type Box<T>\n"
        "  value: Box<T>\n"
        "end\n"
        "var b: Box<Int32> = Box(1)\n";

    return compile_fails(source, "a recursive generic struct",
                         MESSAGE_CHECKER_RECURSIVE_TYPE, "Box");
}

TEST("incompatible union injection")
{
    const char *source =
        "type Point\n"
        "  x: Int32\n"
        "end\n"
        "type S = Int32 | Bool\n"
        "var s: S = Point(1)\n";

    return compile_fails(source, "an incompatible union initializer",
                         MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, "s", "S");
}

TEST("construct union alias")
{
    const char *source =
        "type S = Int32 | Bool\n"
        "var s = S(1)\n";

    return compile_fails(source, "constructing a union alias",
                         MESSAGE_CHECKER_CONSTRUCTOR_NOT_STRUCT, "S");
}

TEST("builtin type arguments")
{
    return compile_fails("var x: Int32<Bool> = 1\n",
                         "type arguments on a builtin",
                         MESSAGE_CHECKER_TYPE_NOT_GENERIC, "Int32");
}
