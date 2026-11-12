#include "test_support.h"
#include "golden_data.h"


TestResult test_checker_expression_statement(void)
{
    return compile_fails("fun f(x:Int32):Int32 x end\n",
                         "a non-call expression statement",
                         MESSAGE_CHECKER_EXPRESSION_STATEMENT);
}


/* ---- checker messages (user-reachable) ---- */

TestResult test_checker_unknown_type(void)
{
    return compile_fails("var x : Float = 13\n", "an unknown variable type",
                             MESSAGE_CHECKER_UNKNOWN_TYPE, "Float", "x");
}


TestResult test_checker_unknown_call(void)
{
    return compile_fails("var p = Foo(1, 2)\n",
                             "an unknown call",
                             MESSAGE_CHECKER_UNKNOWN_CALL, "Foo");
}


TestResult test_checker_constructor_not_struct(void)
{
    return compile_fails("var p = Int32(1)\n",
                             "constructing a non-struct type",
                             MESSAGE_CHECKER_CONSTRUCTOR_NOT_STRUCT, "Int32");
}


TestResult test_checker_constructor_arg_count(void)
{
    return compile_fails("type P x: Int32 end\nvar p = P(1, 2)\n",
                             "a constructor with the wrong argument count",
                             MESSAGE_CHECKER_CONSTRUCTOR_ARG_COUNT,
                             "P", (size_t)1, (size_t)2);
}


TestResult test_checker_missing_constructor_arg(void)
{
    return compile_fails("type P x: Int32 y: Int32 end\nvar p = P(x = 1)\n",
                             "a constructor missing a named argument",
                             MESSAGE_CHECKER_MISSING_CONSTRUCTOR_ARG, "y");
}


TestResult test_checker_duplicate_constructor_arg(void)
{
    return compile_fails(
        "type P x: Int32 y: Int32 end\nvar p = P(x = 1, x = 2, y = 3)\n",
        "a constructor with a duplicate named argument",
        MESSAGE_CHECKER_DUPLICATE_CONSTRUCTOR_ARG, "x");
}


TestResult test_checker_unknown_constructor_field(void)
{
    return compile_fails("type P x: Int32 end\nvar p = P(z = 1)\n",
                             "a constructor naming an unknown field",
                             MESSAGE_CHECKER_UNKNOWN_CONSTRUCTOR_FIELD, "z");
}


TestResult test_checker_constructor_arg_type(void)
{
    return compile_fails(
        "type P x:Int32 end\n"
        "fun f(x:Int32):Int32\n  return x\nend\n"
        "var p = P(f)\n",
        "a constructor argument with the wrong type",
        MESSAGE_CHECKER_CONSTRUCTOR_ARG_TYPE, (size_t)1, "P");
}


TestResult test_checker_initialiser_type_mismatch(void)
{
    return compile_fails("type P x: Int32 end\nvar p : P = 5\n",
                             "an initialiser whose type disagrees with the annotation",
                             MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, "p", "P");
}


TestResult test_checker_duplicate_type(void)
{
    return compile_fails("type P x: Int32 end\ntype P y: Int32 end\n",
                             "a duplicated type declaration",
                             MESSAGE_CHECKER_DUPLICATE_TYPE, "P");
}


TestResult test_checker_redefines_int32(void)
{
    return compile_fails("type Int32 x: Int32 end\n",
                             "a type declaration named Int32",
                             MESSAGE_CHECKER_REDEFINES_INT32);
}


TestResult test_checker_unknown_name(void)
{
    return compile_fails(
        "fun f(x:Int32):Int32\n  return y\nend\n",
        "an unknown name in a return expression",
        MESSAGE_CHECKER_UNKNOWN_NAME, "y");
}


TestResult test_checker_global_in_function(void)
{
    return compile_fails(
        "var g : Int32 = 1\n"
        "fun f(x:Int32):Int32\n  return g\nend\n",
        "a program-level variable used inside a function",
        MESSAGE_CHECKER_GLOBAL_IN_FUNCTION, "g");
}


TestResult test_checker_type_as_value(void)
{
    return compile_fails(
        "type P x:Int32 end\n"
        "fun f(x:Int32):Int32\n  return P\nend\n",
        "a type used as a value",
        MESSAGE_CHECKER_TYPE_AS_VALUE, "P");
}


TestResult test_checker_not_callable(void)
{
    return compile_fails(
        "fun f(x:Int32):Int32\n  return x\nend\n"
        "var y : Int32 = 1\n"
        "var z : Int32 = y(1)\n",
        "calling a non-function value",
        MESSAGE_CHECKER_NOT_CALLABLE, "y");
}


TestResult test_checker_named_function_arg(void)
{
    return compile_fails(
        "fun f(x:Int32):Int32\n  return x\nend\n"
        "var y : Int32 = f(x = 1)\n",
        "a named function argument",
        MESSAGE_CHECKER_NAMED_FUNCTION_ARG);
}


TestResult test_checker_function_arg_count(void)
{
    return compile_fails(
        "fun f(x:Int32):Int32\n  return x\nend\n"
        "var y : Int32 = f(1, 2)\n",
        "a function call with the wrong argument count",
        MESSAGE_CHECKER_FUNCTION_ARG_COUNT, "f", (size_t)1, (size_t)2);
}


TestResult test_checker_function_arg_type(void)
{
    return compile_fails(
        "type P x:Int32 end\n"
        "fun f(p:P):Int32\n  return 1\nend\n"
        "var y : Int32 = f(1)\n",
        "a function call with the wrong argument type",
        MESSAGE_CHECKER_FUNCTION_ARG_TYPE, (size_t)1, "f");
}


TestResult test_checker_return_type_mismatch(void)
{
    return compile_fails(
        "type P x:Int32 end\n"
        "fun f(x:Int32):P\n  return x\nend\n",
        "a return expression with the wrong type",
        MESSAGE_CHECKER_RETURN_TYPE_MISMATCH, "f");
}


TestResult test_checker_operator_type(void)
{
    return compile_fails(
        "type P x:Int32 end\n"
        "fun f(p:P):P\n  return p + p\nend\n",
        "an operator applied to a non-Int32 value",
        MESSAGE_CHECKER_OPERATOR_TYPE, "+");
}


TestResult test_checker_duplicate_name(void)
{
    return compile_fails(
        "fun f(x:Int32):Int32\n  return x\nend\n"
        "var f : Int32 = 1\n",
        "a duplicate top-level name",
        MESSAGE_CHECKER_DUPLICATE_NAME, "f");
}


TestResult test_checker_duplicate_parameter(void)
{
    return compile_fails(
        "fun f(x:Int32, x:Int32):Int32\n  return x\nend\n",
        "a duplicate parameter",
        MESSAGE_CHECKER_DUPLICATE_PARAMETER, "x");
}


TestResult test_checker_condition_type(void)
{
    return compile_fails(
        "fun f(x:Int32):Int32\n"
        "  if x then\n    return 1\n  end\n"
        "  return 0\n"
        "end\n",
        "an Int32 if condition",
        MESSAGE_CHECKER_CONDITION_TYPE);
}


TestResult test_checker_break_outside_loop(void)
{
    return compile_fails(
        "fun f(x:Int32):Int32\n  break\n  return x\nend\n",
        "break outside a loop",
        MESSAGE_CHECKER_BREAK_OUTSIDE_LOOP);
}


TestResult test_checker_continue_outside_loop(void)
{
    return compile_fails(
        "fun f(x:Int32):Int32\n  continue\n  return x\nend\n",
        "continue outside a loop",
        MESSAGE_CHECKER_CONTINUE_OUTSIDE_LOOP);
}


TestResult test_checker_function_may_not_return(void)
{
    return compile_fails(
        "fun f(x:Int32):Int32\n"
        "  if x > 0 then\n    return 1\n  end\n"
        "end\n",
        "a function with a missing return path",
        MESSAGE_CHECKER_FUNCTION_MAY_NOT_RETURN, "f");
}


TestResult test_checker_redefines_bool(void)
{
    return compile_fails("type Bool x:Int32 end\n",
                             "a type declaration named Bool",
                             MESSAGE_CHECKER_REDEFINES_BOOL);
}


TestResult test_checker_missing_ast_root(void)
{
    Ast ast;
    TypedProgram program;
    ErrorState errors;

    ast_init(&ast);
    typed_program_init(&program);
    error_init(&errors);

    if (checker_check(&ast, &program, &errors) ||
        !error_contains(&errors, MESSAGE_CHECKER_MISSING_AST_ROOT)) {
        fprintf(stderr, "  empty AST did not produce \"missing AST root\"\n");
        return TEST_FAIL;
    }
    ast_free(&ast);
    typed_program_free(&program);
    return TEST_PASS;
}


TestResult test_checker_missing_ast_node(void)
{
    Ast ast;
    TypedProgram program;
    ErrorState errors;
    char expected[256];
    size_t bad_root = 999;

    ast_init(&ast);
    typed_program_init(&program);
    error_init(&errors);
    list_push(&ast.roots, &bad_root);

    expect_message(expected, sizeof(expected), MESSAGE_CHECKER_MISSING_AST_NODE,
                   bad_root);
    if (checker_check(&ast, &program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  out-of-range root did not produce \"missing AST node\"\n");
        return TEST_FAIL;
    }
    ast_free(&ast);
    typed_program_free(&program);
    return TEST_PASS;
}


TestResult test_checker_unsupported_ast_node(void)
{
    Ast ast;
    TypedProgram program;
    ErrorState errors;
    AstNode node = {0};
    size_t root = 0;

    ast_init(&ast);
    typed_program_init(&program);
    error_init(&errors);
    node.kind = (AstNodeKind)999;
    list_push(&ast.nodes, &node);
    list_push(&ast.roots, &root);

    if (checker_check(&ast, &program, &errors) ||
        !error_contains(&errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE)) {
        fprintf(stderr,
                "  unknown node kind did not produce \"unsupported AST node\"\n");
        return TEST_FAIL;
    }
    ast_free(&ast);
    typed_program_free(&program);
    return TEST_PASS;
}


TestResult test_checker_no_initialiser(void)
{
    Ast ast;
    TypedProgram program;
    ErrorState errors;
    char expected[256];
    AstNode node = {0};
    size_t root = 0;

    ast_init(&ast);
    typed_program_init(&program);
    error_init(&errors);
    node.kind = AST_DECLARATION;
    node.name_index = ast_intern_symbol(&ast, "x", 1);
    node.left_child = AST_NO_NODE;
    node.right_child = AST_NO_NODE;
    list_push(&ast.nodes, &node);
    list_push(&ast.roots, &root);

    expect_message(expected, sizeof(expected), MESSAGE_CHECKER_NO_INITIALISER,
                   "x");
    if (checker_check(&ast, &program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  missing initialiser did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    ast_free(&ast);
    typed_program_free(&program);
    return TEST_PASS;
}


TestResult test_checker_missing_name_or_type(void)
{
    Ast ast;
    TypedProgram program;
    ErrorState errors;
    AstNode node = {0};
    size_t root = 0;

    ast_init(&ast);
    typed_program_init(&program);
    error_init(&errors);
    node.kind = AST_DECLARATION;
    node.name_index = AST_NO_NODE;
    node.left_child = AST_NO_NODE;
    node.right_child = AST_NO_NODE;
    list_push(&ast.nodes, &node);
    list_push(&ast.roots, &root);

    if (checker_check(&ast, &program, &errors) ||
        !error_contains(&errors, MESSAGE_CHECKER_MISSING_NAME_OR_TYPE)) {
        fprintf(stderr, "  missing name did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    ast_free(&ast);
    typed_program_free(&program);
    return TEST_PASS;
}


TestResult test_checker_type_has_no_fields(void)
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


TestResult test_checker_missing_return(void)
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


/* ---- type-system messages ---- */

TestResult test_checker_generic_arity(void)
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


TestResult test_checker_type_not_generic(void)
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


TestResult test_checker_type_argument_count(void)
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


TestResult test_checker_duplicate_type_parameter(void)
{
    const char *source =
        "type Pair<T, T>\n"
        "  first: T\n"
        "  second: T\n"
        "end\n";

    return compile_fails(source, "a duplicate type parameter",
                         MESSAGE_CHECKER_DUPLICATE_TYPE_PARAMETER, "T");
}


TestResult test_checker_cannot_infer_type(void)
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


TestResult test_checker_generic_as_value(void)
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


TestResult test_checker_is_type_is_union(void)
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


TestResult test_checker_recursive_alias(void)
{
    const char *source =
        "type Loop = Loop | Int32\n"
        "var x: Loop = 1\n";

    return compile_fails(source, "a recursive alias",
                         MESSAGE_CHECKER_RECURSIVE_TYPE, "Loop");
}


TestResult test_checker_nil_type_mismatch(void)
{
    return compile_fails("var x: Int32 = nil\n",
                         "nil initialising an Int32",
                         MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, "x",
                         "Int32");
}


TestResult test_checker_redefines_nil(void)
{
    return compile_fails("type Nil x: Int32 end\n",
                         "a type declaration named Nil",
                         MESSAGE_CHECKER_REDEFINES_NIL);
}



TestResult test_checker_function_field(void)
{
    const char *source =
        "type Holder\n"
        "  f: Fun<(Int32):Int32>\n"
        "end\n";

    return compile_fails(source, "a function-typed struct field",
                         MESSAGE_CHECKER_FIELD_TYPE, "f");
}


TestResult test_checker_recursive_struct(void)
{
    return compile_fails("type Box\n  value: Box\nend\n",
                         "a recursive struct",
                         MESSAGE_CHECKER_RECURSIVE_TYPE, "Box");
}


TestResult test_checker_recursive_generic_struct(void)
{
    const char *source =
        "type Box<T>\n"
        "  value: Box<T>\n"
        "end\n"
        "var b: Box<Int32> = Box(1)\n";

    return compile_fails(source, "a recursive generic struct",
                         MESSAGE_CHECKER_RECURSIVE_TYPE, "Box");
}


TestResult test_checker_union_injection_mismatch(void)
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



TestResult test_checker_redefines_nil_variable(void)
{
    return compile_fails("var Nil = 1\n", "a variable named Nil",
                         MESSAGE_CHECKER_DUPLICATE_NAME, "Nil");
}


TestResult test_checker_nil_as_value(void)
{
    return compile_fails("var x = Nil\n", "the Nil type used as a value",
                         MESSAGE_CHECKER_TYPE_AS_VALUE, "Nil");
}



TestResult test_checker_construct_alias_union(void)
{
    const char *source =
        "type S = Int32 | Bool\n"
        "var s = S(1)\n";

    return compile_fails(source, "constructing a union alias",
                         MESSAGE_CHECKER_CONSTRUCTOR_NOT_STRUCT, "S");
}


TestResult test_checker_builtin_type_arguments(void)
{
    return compile_fails("var x: Int32<Bool> = 1\n",
                         "type arguments on a builtin",
                         MESSAGE_CHECKER_TYPE_NOT_GENERIC, "Int32");
}


TestResult test_checker_store_size_constant(void)
{
    const char *source =
        "var n: Int32 = 4\n"
        "var s: Store = Store(n)\n";

    return compile_fails(source, "a non-constant store size",
                         MESSAGE_CHECKER_STORE_SIZE);
}


TestResult test_checker_store_initializer(void)
{
    return compile_fails("var s: Store = 16\n",
                         "a store without Store(bytes)",
                         MESSAGE_CHECKER_STORE_INITIALIZER, "s");
}


TestResult test_checker_store_without_annotation(void)
{
    return compile_fails("var s = Store(16)\n",
                         "Store(...) without a Store annotation",
                         MESSAGE_CHECKER_STORE_TYPE);
}


TestResult test_checker_store_as_value(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var t = s\n";

    return compile_fails(source, "a store used as a value",
                         MESSAGE_CHECKER_STORE_NOT_VALUE, "s");
}

const TestCase checker_test_1_tests[] = {
    {"checker: expression statement must be a call", test_checker_expression_statement},
    {"checker: unknown type", test_checker_unknown_type},
    {"checker: unknown call", test_checker_unknown_call},
    {"checker: constructor of non-struct", test_checker_constructor_not_struct},
    {"checker: constructor argument count", test_checker_constructor_arg_count},
    {"checker: missing constructor argument", test_checker_missing_constructor_arg},
    {"checker: duplicate constructor argument", test_checker_duplicate_constructor_arg},
    {"checker: unknown constructor field", test_checker_unknown_constructor_field},
    {"checker: constructor argument type", test_checker_constructor_arg_type},
    {"checker: initialiser type mismatch", test_checker_initialiser_type_mismatch},
    {"checker: duplicate type", test_checker_duplicate_type},
    {"checker: redefines Int32", test_checker_redefines_int32},
    {"checker: unknown name", test_checker_unknown_name},
    {"checker: global in function", test_checker_global_in_function},
    {"checker: type as value", test_checker_type_as_value},
    {"checker: not callable", test_checker_not_callable},
    {"checker: named function argument", test_checker_named_function_arg},
    {"checker: function argument count", test_checker_function_arg_count},
    {"checker: function argument type", test_checker_function_arg_type},
    {"checker: return type mismatch", test_checker_return_type_mismatch},
    {"checker: operator type", test_checker_operator_type},
    {"checker: duplicate name", test_checker_duplicate_name},
    {"checker: duplicate parameter", test_checker_duplicate_parameter},
    {"checker: condition type", test_checker_condition_type},
    {"checker: break outside loop", test_checker_break_outside_loop},
    {"checker: continue outside loop", test_checker_continue_outside_loop},
    {"checker: function may not return", test_checker_function_may_not_return},
    {"checker: redefines Bool", test_checker_redefines_bool},
    {"checker: missing AST root", test_checker_missing_ast_root},
    {"checker: missing AST node", test_checker_missing_ast_node},
    {"checker: unsupported AST node", test_checker_unsupported_ast_node},
    {"checker: no initialiser", test_checker_no_initialiser},
    {"checker: missing name or type", test_checker_missing_name_or_type},
    {"checker: type has no fields", test_checker_type_has_no_fields},
    {"checker: missing return", test_checker_missing_return},
    {"checker: generic type without arguments", test_checker_generic_arity},
    {"checker: type arguments on non-generic type", test_checker_type_not_generic},
    {"checker: generic type argument count", test_checker_type_argument_count},
    {"checker: duplicate type parameter", test_checker_duplicate_type_parameter},
    {"checker: cannot infer type parameter", test_checker_cannot_infer_type},
    {"checker: generic function as value", test_checker_generic_as_value},
    {"checker: is-test target is a union", test_checker_is_type_is_union},
    {"checker: recursive alias", test_checker_recursive_alias},
    {"checker: nil type mismatch", test_checker_nil_type_mismatch},
    {"checker: redefines Nil", test_checker_redefines_nil},
    {"checker: function field", test_checker_function_field},
    {"checker: recursive struct", test_checker_recursive_struct},
    {"checker: recursive generic struct", test_checker_recursive_generic_struct},
    {"checker: incompatible union injection", test_checker_union_injection_mismatch},
    {"checker: variable named Nil", test_checker_redefines_nil_variable},
    {"checker: Nil as a value", test_checker_nil_as_value},
    {"checker: construct union alias", test_checker_construct_alias_union},
    {"checker: builtin type arguments", test_checker_builtin_type_arguments},
    {"store: size must be constant", test_checker_store_size_constant},
    {"store: initializer must be Store(bytes)", test_checker_store_initializer},
    {"store: missing annotation", test_checker_store_without_annotation},
    {"store: not a value", test_checker_store_as_value},
};

const size_t checker_test_1_test_count =
    sizeof(checker_test_1_tests) / sizeof(checker_test_1_tests[0]);
