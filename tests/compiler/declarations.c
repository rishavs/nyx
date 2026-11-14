/* Names: declarations, duplicates, reserved and redefined names, assignment,
   const, and local scope. */

#include "compile_support.h"

TEST("struct defaults are checked even when unused")
{
    return compile_fails("type Bad x: Bool = 10 end\n",
                         "incorrect field default", MESSAGE_CHECKER_FIELD_DEFAULT_TYPE,
                         "x");
}

TEST("required struct fields cannot be omitted")
{
    return compile_fails("type Box x: Int64 = 1 y: Int64 end\n"
                         "var box = Box(x = 2)\n",
                         "missing required field", MESSAGE_CHECKER_MISSING_CONSTRUCTOR_ARG,
                         "y");
}

TEST("generic struct defaults are checked per specialization")
{
    return compile_fails("type Box<T> value: T = 1 end\n"
                         "var box: Box<Bool> = Box()\n",
                         "invalid generic field default", MESSAGE_CHECKER_FIELD_DEFAULT_TYPE,
                         "value");
}

TEST("independent declaration errors are reported together")
{
    CompilerResult result = compiler_compile(
        "var a: Int32 = true\n"
        "var b: Bool = 3\n"
        "var c: Int8 = 128\n"
        "var d: String<2> = \"long\"\n", &test_config);
    bool ok = !result.succeeded && result.error_message != NULL &&
              strstr(result.error_message, "'a'") != NULL &&
              strstr(result.error_message, "'b'") != NULL &&
              strstr(result.error_message, "integer literal") != NULL &&
              strstr(result.error_message, "does not fit String<2>") != NULL;

    if (!ok) {
        fprintf(stderr, "  missing independent errors: %s\n",
                result.error_message == NULL ? "(none)" : result.error_message);
    }
    compiler_result_free(&result);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("independent function errors are reported together")
{
    CompilerResult result = compiler_compile(
        "fun bad()\n"
        "  var a: Int32 = true\n"
        "  var b: Bool = 3\n"
        "  var c: Int8 = 128\n"
        "end\n", &test_config);
    bool ok = !result.succeeded && result.error_message != NULL &&
              strstr(result.error_message, "'a'") != NULL &&
              strstr(result.error_message, "'b'") != NULL &&
              strstr(result.error_message, "integer literal") != NULL;

    if (!ok) {
        fprintf(stderr, "  missing independent errors: %s\n",
                result.error_message == NULL ? "(none)" : result.error_message);
    }
    compiler_result_free(&result);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("independent errors in both branches are reported")
{
    CompilerResult result = compiler_compile(
        "fun bad()\n"
        "  if true then var a: Int32 = true\n"
        "  else var b: Bool = 3 end\n"
        "end\n", &test_config);
    bool ok = !result.succeeded && result.error_message != NULL &&
              strstr(result.error_message, "'a'") != NULL &&
              strstr(result.error_message, "'b'") != NULL;
    if (!ok) {
        fprintf(stderr, "  missing branch errors: %s\n",
                result.error_message == NULL ? "(none)" : result.error_message);
    }
    compiler_result_free(&result);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("independent errors in match arms are reported")
{
    CompilerResult result = compiler_compile(
        "fun bad(x: Bool)\n"
        "  match x with true then var a: Int32 = true\n"
        "  with false then var b: Bool = 3\n"
        "  else var c: Int8 = 128 end\n"
        "end\n", &test_config);
    bool ok = !result.succeeded && result.error_message != NULL &&
              strstr(result.error_message, "'a'") != NULL &&
              strstr(result.error_message, "'b'") != NULL &&
              strstr(result.error_message, "integer literal") != NULL;
    if (!ok) {
        fprintf(stderr, "  missing match arm errors: %s\n",
                result.error_message == NULL ? "(none)" : result.error_message);
    }
    compiler_result_free(&result);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("independent errors in match patterns are reported")
{
    CompilerResult result = compiler_compile(
        "var x: Bool = true\n"
        "match x with missing_a then print(1)\n"
        "with missing_b then print(2) else print(3) end\n", &test_config);
    bool ok = !result.succeeded && result.error_message != NULL &&
              strstr(result.error_message, "missing_a") != NULL &&
              strstr(result.error_message, "missing_b") != NULL;
    if (!ok) {
        fprintf(stderr, "  missing pattern errors: %s\n",
                result.error_message == NULL ? "(none)" : result.error_message);
    }
    compiler_result_free(&result);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("independent errors in if expression arms are reported")
{
    CompilerResult result = compiler_compile(
        "var result = if true then missing_a else missing_b end\n",
        &test_config);
    bool ok = !result.succeeded && result.error_message != NULL &&
              strstr(result.error_message, "missing_a") != NULL &&
              strstr(result.error_message, "missing_b") != NULL;
    if (!ok) {
        fprintf(stderr, "  missing expression errors: %s\n",
                result.error_message == NULL ? "(none)" : result.error_message);
    }
    compiler_result_free(&result);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("independent invalid Nil types are reported together")
{
    CompilerResult result = compiler_compile(
        "fun a(x: Nil) end\nfun b(y: Nil) end\n", &test_config);
    bool ok = !result.succeeded && result.error_message != NULL &&
              strstr(result.error_message, "test.nip:1:") != NULL &&
              strstr(result.error_message, "test.nip:2:") != NULL;
    if (!ok) {
        fprintf(stderr, "  missing Nil type errors: %s\n",
                result.error_message == NULL ? "(none)" : result.error_message);
    }
    compiler_result_free(&result);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("invalid Nil types do not hide independent checker errors")
{
    CompilerResult result = compiler_compile(
        "var a: Nil = nil\nvar b: Int32 = true\n", &test_config);
    bool ok = !result.succeeded && result.error_message != NULL &&
              strstr(result.error_message, "test.nip:1:") != NULL &&
              strstr(result.error_message, "'b'") != NULL;
    if (!ok) {
        fprintf(stderr, "  missing independent errors: %s\n",
                result.error_message == NULL ? "(none)" : result.error_message);
    }
    compiler_result_free(&result);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("initialiser type mismatch")
{
    return compile_fails("type P x: Int32 end\nvar p : P = 5\n",
                             "an initialiser whose type disagrees with the annotation",
                             MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, "p", "P");
}

TEST("duplicate type")
{
    return compile_fails("type P x: Int32 end\ntype P y: Int32 end\n",
                             "a duplicated type declaration",
                             MESSAGE_CHECKER_DUPLICATE_TYPE, "P");
}

TEST("redefines Int32")
{
    return compile_fails("type Int32 x: Int32 end\n",
                             "a type declaration named Int32",
                             MESSAGE_CHECKER_REDEFINES_INT32);
}

TEST("unknown name")
{
    return compile_fails(
        "fun f(x:Int32):Int32\n  return y\nend\n",
        "an unknown name in a return expression",
        MESSAGE_CHECKER_UNKNOWN_NAME, "y");
}

TEST("global in function")
{
    return compile_fails(
        "var g : Int32 = 1\n"
        "fun f(x:Int32):Int32\n  return g\nend\n",
        "a program-level variable used inside a function",
        MESSAGE_CHECKER_GLOBAL_IN_FUNCTION, "g");
}

TEST("duplicate name")
{
    return compile_fails(
        "fun f(x:Int32):Int32\n  return x\nend\n"
        "var f : Int32 = 1\n",
        "a duplicate top-level name",
        MESSAGE_CHECKER_DUPLICATE_NAME, "f");
}

TEST("duplicate parameter")
{
    return compile_fails(
        "fun f(x:Int32, x:Int32):Int32\n  return x\nend\n",
        "a duplicate parameter",
        MESSAGE_CHECKER_DUPLICATE_PARAMETER, "x");
}

TEST("redefines Bool")
{
    return compile_fails("type Bool x:Int32 end\n",
                             "a type declaration named Bool",
                             MESSAGE_CHECKER_REDEFINES_BOOL);
}

TEST("no initialiser")
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

TEST("redefines Nil")
{
    return compile_fails("type Nil x: Int32 end\n",
                         "a type declaration named Nil",
                         MESSAGE_CHECKER_REDEFINES_NIL);
}

TEST("variable named Nil")
{
    return compile_fails("var Nil = 1\n", "a variable named Nil",
                         MESSAGE_CHECKER_DUPLICATE_NAME, "Nil");
}

TEST("string: redefining String")
{
    return compile_fails(
        "type String = Int32\n",
        "redefining String",
        MESSAGE_CHECKER_REDEFINES_STRING);
}

TEST("string: redefining Text")
{
    return compile_fails(
        "type Text = Int32\n",
        "redefining Text",
        MESSAGE_CHECKER_REDEFINES_TEXT);
}

TEST("string: redefining Error")
{
    return compile_fails(
        "type Error = Int32\n",
        "redefining Error",
        MESSAGE_CHECKER_REDEFINES_ERROR);
}

TEST("string: a variable named Text")
{
    return compile_fails(
        "var Text: Int32 = 1\n",
        "a variable named Text",
        MESSAGE_CHECKER_DUPLICATE_NAME, "Text");
}

TEST("assignment: an assignment to an unknown name")
{
    return compile_fails(
        "x = 1\n",
        "an assignment to an unknown name",
        MESSAGE_CHECKER_UNKNOWN_NAME, "x");
}

TEST("assignment: an assignment to a store")
{
    return compile_fails(
        "var s: Store = Store(8)\ns = Store(8)\n",
        "an assignment to a store",
        MESSAGE_CHECKER_ASSIGN_TARGET, "s");
}

TEST("assignment: an assignment to a function")
{
    return compile_fails(
        "fun f()\n  return\nend\nf = 1\n",
        "an assignment to a function",
        MESSAGE_CHECKER_ASSIGN_TARGET, "f");
}

TEST("assignment: an assignment to a type")
{
    return compile_fails(
        "type P\n  x: Int32\nend\nP = 1\n",
        "an assignment to a type",
        MESSAGE_CHECKER_ASSIGN_TARGET, "P");
}

TEST("assignment: an assignment to a built-in type")
{
    return compile_fails(
        "Int32 = 1\n",
        "an assignment to a built-in type",
        MESSAGE_CHECKER_ASSIGN_TARGET, "Int32");
}

TEST("assignment: assigning a program-level variable inside a function")
{
    return compile_fails(
        "var n: Int32 = 1\nfun f()\n  n = 2\n  return\nend\n",
        "assigning a program-level variable inside a function",
        MESSAGE_CHECKER_GLOBAL_IN_FUNCTION, "n");
}

TEST("assignment: assigning a Bool to an Int32")
{
    return compile_fails(
        "var n: Int32 = 1\nn = true\n",
        "assigning a Bool to an Int32",
        MESSAGE_CHECKER_ASSIGN_TYPE, "n");
}

TEST("assignment: assigning an out-of-range Byte literal")
{
    return compile_fails(
        "var b: Byte = 1\nb = 300\n",
        "assigning an out-of-range Byte literal",
        MESSAGE_CHECKER_BYTE_RANGE, 300);
}

TEST("assignment: assigning a literal larger than the destination")
{
    return compile_fails(
        "var s: String<4> = \"a\"\ns = \"hello\"\n",
        "assigning a literal larger than the destination",
        MESSAGE_CHECKER_STRING_TOO_LONG, (size_t)5, 4);
}

TEST("builder: redefining TextBuilder")
{
    return compile_fails(
        "type TextBuilder = Int32\n",
        "redefining TextBuilder",
        MESSAGE_CHECKER_REDEFINES_BUILDER);
}

TEST("list: redefining List")
{
    return compile_fails(
        "type List = Int32\n",
        "redefining List",
        MESSAGE_CHECKER_REDEFINES_LIST);
}

TEST("slice: redefining Slice")
{
    return compile_fails(
        "type Slice = Int32\n",
        "redefining Slice",
        MESSAGE_CHECKER_REDEFINES_SLICE);
}

TEST("local Store")
{
    return compile_fails(
        "fun f()\n  var s: Store = Store(1)\n  return\nend\n",
        "local Store",
        MESSAGE_CHECKER_LOCAL_STORE);
}

TEST("local out of scope")
{
    return compile_fails(
        "fun f(): Int32\n"
        "  if true then\n"
        "    var y: Int32 = 1\n"
        "  end\n"
        "  return y\n"
        "end\n",
        "local out of scope",
        MESSAGE_CHECKER_UNKNOWN_NAME, "y");
}

TEST("local shadows parameter")
{
    return compile_fails(
        "fun f(x: Int32): Int32\n  var x: Int32 = 1\n  return x\nend\n",
        "local shadows parameter",
        MESSAGE_CHECKER_DUPLICATE_NAME, "x");
}

TEST("for duplicate binding")
{
    return compile_fails(
        "var xs: Array<Int32, 2> = [1, 2]\nfor x, x in xs do\nend\n",
        "for duplicate binding",
        MESSAGE_CHECKER_DUPLICATE_NAME, "x");
}

TEST("for parameter collision")
{
    return compile_fails(
        "fun f(x: Int32, xs: Array<Int32, 2>)\n"
        "  for x in xs do\n"
        "  end\n"
        "  return\n"
        "end\n",
        "for parameter collision",
        MESSAGE_CHECKER_DUPLICATE_NAME, "x");
}

TEST("redefining Array")
{
    return compile_fails(
        "type Array = Int32\n",
        "redefining Array",
        MESSAGE_CHECKER_REDEFINES_ARRAY);
}

TEST("reserved C keyword")
{
    return compile_fails("var int: Int32 = 1\n",
                         "a C keyword as a name",
                         MESSAGE_CHECKER_RESERVED_NAME, "int");
}

TEST("reserved generated prefix")
{
    return compile_fails("var nyx_value: Int32 = 1\n",
                         "the generated-name prefix",
                         MESSAGE_CHECKER_RESERVED_NAME, "nyx_value");
}

TEST("reserved entry point")
{
    return compile_fails("fun main()\n  return\nend\n",
                         "the generated entry point",
                         MESSAGE_CHECKER_RESERVED_NAME, "main");
}

TEST("reserved field name")
{
    return compile_fails("type Box\n  nyx_value: Int32\nend\n",
                         "a reserved field name",
                         MESSAGE_CHECKER_RESERVED_NAME, "nyx_value");
}

TEST("reserved parameter name")
{
    return compile_fails("fun f(printf: Int32)\n  return\nend\n",
                         "a reserved parameter name",
                         MESSAGE_CHECKER_RESERVED_NAME, "printf");
}

TEST("reserved local name")
{
    return compile_fails(
        "fun f()\n  var nyx_value: Int32 = 1\n  return\nend\n",
        "a reserved local name", MESSAGE_CHECKER_RESERVED_NAME, "nyx_value");
}

TEST("reserved generic parameter")
{
    return compile_fails(
        "fun f<T>(x: T, printf: Int32): T\n  return x\nend\n"
        "var n: Int32 = f(1, 2)\n",
        "a reserved generic parameter", MESSAGE_CHECKER_RESERVED_NAME,
        "printf");
}

TEST("reserved type parameter")
{
    return compile_fails(
        "fun f<nyx_T>(x: nyx_T): nyx_T\n  return x\nend\n",
        "a reserved type parameter", MESSAGE_CHECKER_RESERVED_NAME, "nyx_T");
}

TEST("prefix lookalike")
{
    CompilerResult result = compiler_compile("var nyx: Int32 = 1\n",
                                             &test_config);
    int ok = result.succeeded;

    compiler_result_free(&result);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("print reserved")
{
    TestResult result = compile_fails(
        "fun print(x: Int32)\n  return\nend\n",
        "print reserved", MESSAGE_CHECKER_BUILTIN_RESERVED, "print");

    if (result != TEST_PASS) {
        return result;
    }
    result = compile_fails(
        "fun f(print: Int32)\n  return\nend\n",
        "print parameter reserved", MESSAGE_CHECKER_BUILTIN_RESERVED, "print");
    if (result != TEST_PASS) {
        return result;
    }
    return compile_fails(
        "fun f<T>(print: T): T\n  return print\nend\n"
        "var n: Int32 = f(1)\n",
        "generic print parameter reserved", MESSAGE_CHECKER_BUILTIN_RESERVED, "print");
}

TEST("redefining built-in type")
{
    TestResult result;

    result = compile_fails("type Byte = Int32\n", "redefining Byte",
                           MESSAGE_CHECKER_REDEFINES_BUILTIN, "Byte");
    if (result != TEST_PASS) {
        return result;
    }
    result = compile_fails("type Store = Int32\n", "redefining Store",
                           MESSAGE_CHECKER_REDEFINES_BUILTIN, "Store");
    if (result != TEST_PASS) {
        return result;
    }
    result = compile_fails("type Handle = Int32\n", "redefining Handle",
                           MESSAGE_CHECKER_REDEFINES_BUILTIN, "Handle");
    if (result != TEST_PASS) {
        return result;
    }
    result = compile_fails("var Record: Int32 = 1\n",
                           "a value named like a built-in",
                           MESSAGE_CHECKER_DUPLICATE_NAME, "Record");
    if (result != TEST_PASS) {
        return result;
    }
    return compile_fails(
        "fun f(): Int32\n  var Table: Int32 = 1\n  return Table\nend\n",
        "a local named like a built-in", MESSAGE_CHECKER_DUPLICATE_NAME,
        "Table");
}

TEST("redefining Record")
{
    return compile_fails(
        "type Record = Int32\n",
        "redefining Record",
        MESSAGE_CHECKER_REDEFINES_RECORD);
}

TEST("redefining Table")
{
    return compile_fails(
        "type Table = Int32\n",
        "redefining Table",
        MESSAGE_CHECKER_REDEFINES_TABLE);
}

TEST("assign to const")
{
    return compile_fails(
        "const x: Int32 = 1\nx = 2\n",
        "assign to const",
        MESSAGE_CHECKER_CONST_ASSIGN, "x");
}

TEST("assign to local const")
{
    return compile_fails(
        "fun f()\n  const x: Int32 = 1\n  x = 2\n  return\nend\n",
        "assign to local const",
        MESSAGE_CHECKER_CONST_ASSIGN, "x");
}

TEST("const Store")
{
    return compile_fails(
        "const s: Store = Store(16)\n",
        "const Store",
        MESSAGE_CHECKER_CONST_STORE);
}
