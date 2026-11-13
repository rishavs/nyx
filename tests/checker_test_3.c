#include "test_support.h"
#include "golden_data.h"


TestResult test_checker_local_store(void)
{
    return compile_fails(
        "fun f(): Nil\n  var s: Store = Store(1)\n  return nil\nend\n",
        "local Store",
        MESSAGE_CHECKER_LOCAL_STORE);
}


TestResult test_checker_local_scope(void)
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


TestResult test_checker_local_shadow(void)
{
    return compile_fails(
        "fun f(x: Int32): Int32\n  var x: Int32 = 1\n  return x\nend\n",
        "local shadows parameter",
        MESSAGE_CHECKER_DUPLICATE_NAME, "x");
}


TestResult test_checker_unknown_field(void)
{
    return compile_fails(
        "type P\n  x: Int32\nend\nfun f(p: P): Int32\n  return p.y\nend\n",
        "unknown field",
        MESSAGE_CHECKER_UNKNOWN_FIELD, "y");
}


TestResult test_checker_error_field_write(void)
{
    return compile_fails(
        "fun f(e: Error): Nil\n  e.line = 1\n  return nil\nend\n",
        "Error field write",
        MESSAGE_CHECKER_FIELD_LVALUE);
}


TestResult test_checker_for_iterable(void)
{
    return compile_fails(
        "for x in 1 do\nend\n",
        "for non-iterable",
        MESSAGE_CHECKER_FOR_ITERABLE);
}


TestResult test_checker_for_duplicate_binding(void)
{
    return compile_fails(
        "var xs: Array<Int32, 2> = [1, 2]\nfor x, x in xs do\nend\n",
        "for duplicate binding",
        MESSAGE_CHECKER_DUPLICATE_NAME, "x");
}


TestResult test_checker_for_parameter_collision(void)
{
    return compile_fails(
        "fun f(x: Int32, xs: Array<Int32, 2>): Nil\n"
        "  for x in xs do\n"
        "  end\n"
        "  return nil\n"
        "end\n",
        "for parameter collision",
        MESSAGE_CHECKER_DUPLICATE_NAME, "x");
}


TestResult test_checker_handle_collection(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var h: Handle<List<Int32>> = "
        "arena.alloc<List<Int32>>(arena.list<Int32>(1))\n",
        "handle of a collection",
        MESSAGE_CHECKER_HANDLE_FIXED);
}


TestResult test_checker_alloc_text(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var f: String<8> = \"x\"\n"
        "var h: Handle<Text> = arena.alloc<Text>(arena.text(f))\n",
        "alloc of a Text",
        MESSAGE_CHECKER_HANDLE_FIXED);
}


TestResult test_checker_list_of_list(void)
{
    return compile_fails(
        "var xs: List<List<Int32>> = []\n",
        "List of List",
        MESSAGE_CHECKER_COLLECTION_ELEMENT);
}


TestResult test_checker_array_of_array(void)
{
    return compile_fails(
        "var xs: Array<Array<Int32, 1>, 1> = []\n",
        "Array of Array",
        MESSAGE_CHECKER_ARRAY_ELEMENT);
}


TestResult test_checker_bare_string(void)
{
    return compile_fails(
        "var s: String = \"hello\"\n",
        "bare String",
        MESSAGE_CHECKER_STRING_ARGS);
}


TestResult test_checker_array_args(void)
{
    return compile_fails(
        "var xs: Array = []\n",
        "bare Array",
        MESSAGE_CHECKER_ARRAY_ARGS);
}


TestResult test_checker_redefines_array(void)
{
    return compile_fails(
        "type Array = Int32\n",
        "redefining Array",
        MESSAGE_CHECKER_REDEFINES_ARRAY);
}


TestResult test_checker_handle_builder(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var h: Handle<TextBuilder> = "
        "arena.alloc<TextBuilder>(arena.builder(1))\n",
        "handle of a builder",
        MESSAGE_CHECKER_HANDLE_FIXED);
}


TestResult test_checker_list_of_slice(void)
{
    return compile_fails(
        "var xs: List<Slice<Int32>> = []\n",
        "List of Slice",
        MESSAGE_CHECKER_COLLECTION_ELEMENT);
}


TestResult test_checker_alloc_struct_with_text(void)
{
    return compile_fails(
        "type S\n"
        "  t: Text\n"
        "end\n"
        "global arena: Store = Store(64)\n"
        "var f: String<8> = \"x\"\n"
        "var t: Text = arena.text(f)\n"
        "var s: S = S(t)\n"
        "var h: Handle<S> = arena.alloc<S>(s)\n",
        "alloc of a struct with Text",
        MESSAGE_CHECKER_HANDLE_FIXED);
}


TestResult test_checker_alloc_array_with_text(void)
{
    return compile_fails(
        "global arena: Store = Store(64)\n"
        "var f: String<8> = \"x\"\n"
        "var t: Text = arena.text(f)\n"
        "var a: Array<Text, 1> = [t]\n"
        "var h: Handle<Array<Text, 1>> = "
        "arena.alloc<Array<Text, 1>>(a)\n",
        "alloc of an Array with Text",
        MESSAGE_CHECKER_HANDLE_FIXED);
}


TestResult test_checker_reserved_c_keyword(void)
{
    return compile_fails("var int: Int32 = 1\n",
                         "a C keyword as a name",
                         MESSAGE_CHECKER_RESERVED_NAME, "int");
}


TestResult test_checker_reserved_generated_prefix(void)
{
    return compile_fails("var nyx_value: Int32 = 1\n",
                         "the generated-name prefix",
                         MESSAGE_CHECKER_RESERVED_NAME, "nyx_value");
}


TestResult test_checker_reserved_main(void)
{
    return compile_fails("fun main(): Nil\n  return nil\nend\n",
                         "the generated entry point",
                         MESSAGE_CHECKER_RESERVED_NAME, "main");
}


TestResult test_checker_reserved_field(void)
{
    return compile_fails("type Box\n  nyx_value: Int32\nend\n",
                         "a reserved field name",
                         MESSAGE_CHECKER_RESERVED_NAME, "nyx_value");
}


TestResult test_checker_reserved_parameter(void)
{
    return compile_fails("fun f(printf: Int32): Nil\n  return nil\nend\n",
                         "a reserved parameter name",
                         MESSAGE_CHECKER_RESERVED_NAME, "printf");
}


TestResult test_checker_reserved_local(void)
{
    return compile_fails(
        "fun f(): Nil\n  var nyx_value: Int32 = 1\n  return nil\nend\n",
        "a reserved local name", MESSAGE_CHECKER_RESERVED_NAME, "nyx_value");
}


TestResult test_checker_reserved_generic_parameter(void)
{
    return compile_fails(
        "fun f<T>(x: T, printf: Int32): T\n  return x\nend\n"
        "var n: Int32 = f(1, 2)\n",
        "a reserved generic parameter", MESSAGE_CHECKER_RESERVED_NAME,
        "printf");
}


TestResult test_checker_reserved_type_parameter(void)
{
    return compile_fails(
        "fun f<nyx_T>(x: nyx_T): nyx_T\n  return x\nend\n",
        "a reserved type parameter", MESSAGE_CHECKER_RESERVED_NAME, "nyx_T");
}


TestResult test_checker_reserved_prefix_lookalike(void)
{
    CompilerResult result = compiler_compile("var nyx: Int32 = 1\n",
                                             &test_config);
    int ok = result.succeeded;

    compiler_result_free(&result);
    return ok ? TEST_PASS : TEST_FAIL;
}


TestResult test_checker_print_statement(void)
{
    return compile_fails(
        "var n: Nil = print(1)\n",
        "print as a value",
        MESSAGE_CHECKER_PRINT_STATEMENT);
}


TestResult test_checker_print_named(void)
{
    return compile_fails(
        "print(x = 1)\n",
        "print named argument",
        MESSAGE_CHECKER_PRINT_NAMED);
}


TestResult test_checker_print_reserved(void)
{
    TestResult result = compile_fails(
        "fun print(x: Int32): Nil\n  return nil\nend\n",
        "print reserved", MESSAGE_CHECKER_PRINT_RESERVED);

    if (result != TEST_PASS) {
        return result;
    }
    result = compile_fails(
        "fun f(print: Int32): Nil\n  return nil\nend\n",
        "print parameter reserved", MESSAGE_CHECKER_PRINT_RESERVED);
    if (result != TEST_PASS) {
        return result;
    }
    return compile_fails(
        "fun f<T>(print: T): T\n  return print\nend\n"
        "var n: Int32 = f(1)\n",
        "generic print parameter reserved", MESSAGE_CHECKER_PRINT_RESERVED);
}


TestResult test_checker_error_throw_args(void)
{
    return compile_fails(
        "var e: Error = Error.throw(\"E\")\n",
        "Error.throw argument count",
        MESSAGE_CHECKER_ERROR_THROW_ARGS);
}


TestResult test_checker_record_context(void)
{
    return compile_fails(
        "var x = {\"a\": true}\n",
        "Record literal context",
        MESSAGE_CHECKER_RECORD_LITERAL_CONTEXT);
}


TestResult test_checker_record_duplicate(void)
{
    return compile_fails(
        "var x: Record<String<8>, Bool, 4> = {\"a\": true, \"a\": false}\n",
        "Record duplicate key",
        MESSAGE_CHECKER_RECORD_DUPLICATE_KEY);
}


TestResult test_checker_record_key(void)
{
    return compile_fails(
        "var x: Record<Int32, Bool, 4> = {}\n",
        "Record key type",
        MESSAGE_CHECKER_MAP_KEY);
}


TestResult test_checker_record_value(void)
{
    return compile_fails(
        "var x: Record<String<8>, List<Int32>, 4> = {}\n",
        "Record value collection",
        MESSAGE_CHECKER_MAP_VALUE);
}


TestResult test_checker_record_capacity(void)
{
    return compile_fails(
        "var x: Record<String<8>, Bool, 4097> = {}\n",
        "Record capacity",
        MESSAGE_CHECKER_RECORD_CAPACITY);
}


TestResult test_checker_record_literal_capacity(void)
{
    return compile_fails(
        "var x: Record<String<8>, Bool, 1> = {\"a\": true, \"b\": false}\n",
        "Record literal capacity",
        MESSAGE_CHECKER_RECORD_CAPACITY);
}


TestResult test_checker_handle_record(void)
{
    return compile_fails(
        "var x: Handle<Record<String<8>, Bool, 4>> = 0\n",
        "Handle Record",
        MESSAGE_CHECKER_HANDLE_FIXED);
}


TestResult test_checker_alloc_record(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var x: Handle<Record<String<8>, Bool, 4>> = "
        "arena.alloc<Record<String<8>, Bool, 4>>({})\n",
        "alloc Record",
        MESSAGE_CHECKER_HANDLE_FIXED);
}


TestResult test_checker_table_key(void)
{
    return compile_fails(
        "var x: Table<Text, Bool> = arena.table<Text, Bool>(1)\n",
        "Table key type",
        MESSAGE_CHECKER_MAP_KEY);
}


TestResult test_checker_table_literal(void)
{
    return compile_fails(
        "var x: Table<String<8>, Bool> = {}\n",
        "Table literal",
        MESSAGE_CHECKER_RECORD_LITERAL_CONTEXT);
}


TestResult test_checker_record_args(void)
{
    return compile_fails(
        "var x: Record = {}\n",
        "bare Record",
        MESSAGE_CHECKER_RECORD_ARGS);
}


TestResult test_checker_table_args(void)
{
    return compile_fails(
        "var x: Table = arena.table(1)\n",
        "bare Table",
        MESSAGE_CHECKER_TABLE_ARGS);
}


TestResult test_checker_redefines_builtin(void)
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

TestResult test_checker_redefines_record(void)
{
    return compile_fails(
        "type Record = Int32\n",
        "redefining Record",
        MESSAGE_CHECKER_REDEFINES_RECORD);
}


TestResult test_checker_redefines_table(void)
{
    return compile_fails(
        "type Table = Int32\n",
        "redefining Table",
        MESSAGE_CHECKER_REDEFINES_TABLE);
}


TestResult test_checker_if_expression_mismatch(void)
{
    return compile_fails(
        "var z: Int32 = if true then 1 else false end\n",
        "if expression type mismatch",
        MESSAGE_CHECKER_IF_EXPR_MISMATCH);
}


TestResult test_checker_match_expression_mismatch(void)
{
    return compile_fails(
        "var b: Bool = true\n"
        "var z: Int32 = match b with true then 1 else false end\n",
        "match expression type mismatch",
        MESSAGE_CHECKER_IF_EXPR_MISMATCH);
}


TestResult test_checker_const_assign(void)
{
    return compile_fails(
        "const x: Int32 = 1\nx = 2\n",
        "assign to const",
        MESSAGE_CHECKER_CONST_ASSIGN, "x");
}


TestResult test_checker_local_const_assign(void)
{
    return compile_fails(
        "fun f(): Nil\n  const x: Int32 = 1\n  x = 2\n  return nil\nend\n",
        "assign to local const",
        MESSAGE_CHECKER_CONST_ASSIGN, "x");
}


TestResult test_checker_const_store(void)
{
    return compile_fails(
        "const s: Store = Store(16)\n",
        "const Store",
        MESSAGE_CHECKER_CONST_STORE);
}


TestResult test_checker_match_not_exhaustive(void)
{
    return compile_fails(
        "type V = Int32 | Bool\n"
        "var x: V = 1\n"
        "match typeof x with Int32 then\n"
        "  print(\"i\")\n"
        "end\n",
        "non-exhaustive type match",
        MESSAGE_CHECKER_MATCH_NOT_EXHAUSTIVE);
}


TestResult test_checker_bool_match_not_exhaustive(void)
{
    return compile_fails(
        "var x: Bool = true\n"
        "match x with true then\n"
        "  print(\"t\")\n"
        "end\n",
        "non-exhaustive bool match",
        MESSAGE_CHECKER_MATCH_NOT_EXHAUSTIVE);
}


TestResult test_checker_match_duplicate(void)
{
    return compile_fails(
        "type V = Int32 | Bool\n"
        "var x: V = 1\n"
        "match typeof x with Int32 then\n"
        "  print(\"i\")\n"
        "with Int32 then\n"
        "  print(\"j\")\n"
        "end\n",
        "duplicate match pattern",
        MESSAGE_CHECKER_MATCH_DUPLICATE);
}


TestResult test_checker_match_pattern(void)
{
    return compile_fails(
        "type V = Int32 | Bool\n"
        "var x: V = 1\n"
        "match typeof x with String<8> then\n"
        "  print(\"s\")\n"
        "else\n"
        "  print(\"o\")\n"
        "end\n",
        "match pattern does not fit",
        MESSAGE_CHECKER_MATCH_PATTERN);
}


TestResult test_checker_literal_divide_zero(void)
{
    return compile_fails(
        "var x: Int32 = 1 / 0\n",
        "literal division by zero",
        MESSAGE_CHECKER_ZERO_DIVISOR, "/");
}


TestResult test_checker_literal_modulo_zero(void)
{
    return compile_fails(
        "var x: Int32 = 1 % 0\n",
        "literal modulo by zero",
        MESSAGE_CHECKER_ZERO_DIVISOR, "%");
}

const TestCase checker_test_3_tests[] = {
    {"reserved C keyword", test_checker_reserved_c_keyword},
    {"reserved generated prefix", test_checker_reserved_generated_prefix},
    {"reserved entry point", test_checker_reserved_main},
    {"reserved field name", test_checker_reserved_field},
    {"reserved parameter name", test_checker_reserved_parameter},
    {"reserved generic parameter", test_checker_reserved_generic_parameter},
    {"reserved local name", test_checker_reserved_local},
    {"reserved type parameter", test_checker_reserved_type_parameter},
    {"prefix lookalike", test_checker_reserved_prefix_lookalike},
    {"print as a value", test_checker_print_statement},
    {"print named argument", test_checker_print_named},
    {"print reserved", test_checker_print_reserved},
    {"assign to const", test_checker_const_assign},
    {"assign to local const", test_checker_local_const_assign},
    {"const Store", test_checker_const_store},
    {"non-exhaustive type match", test_checker_match_not_exhaustive},
    {"non-exhaustive bool match", test_checker_bool_match_not_exhaustive},
    {"duplicate match pattern", test_checker_match_duplicate},
    {"match pattern does not fit", test_checker_match_pattern},
    {"literal division by zero", test_checker_literal_divide_zero},
    {"literal modulo by zero", test_checker_literal_modulo_zero},
    {"if expression type mismatch", test_checker_if_expression_mismatch},
    {"match expression type mismatch", test_checker_match_expression_mismatch},
    {"Record literal context", test_checker_record_context},
    {"Record duplicate key", test_checker_record_duplicate},
    {"Record key type", test_checker_record_key},
    {"Record value collection", test_checker_record_value},
    {"Record capacity", test_checker_record_capacity},
    {"Record literal capacity", test_checker_record_literal_capacity},
    {"Handle Record", test_checker_handle_record},
    {"alloc Record", test_checker_alloc_record},
    {"Table key type", test_checker_table_key},
    {"Table literal", test_checker_table_literal},
    {"bare Record", test_checker_record_args},
    {"bare Table", test_checker_table_args},
    {"redefining built-in type", test_checker_redefines_builtin},
    {"redefining Record", test_checker_redefines_record},
    {"redefining Table", test_checker_redefines_table},
    {"Error.throw argument count", test_checker_error_throw_args},
    {"for non-iterable", test_checker_for_iterable},
    {"for duplicate binding", test_checker_for_duplicate_binding},
    {"for parameter collision", test_checker_for_parameter_collision},
    {"handle of a collection", test_checker_handle_collection},
    {"alloc of a Text", test_checker_alloc_text},
    {"alloc of a struct with Text", test_checker_alloc_struct_with_text},
    {"alloc of an Array with Text", test_checker_alloc_array_with_text},
    {"List of List", test_checker_list_of_list},
    {"handle of a builder", test_checker_handle_builder},
    {"List of Slice", test_checker_list_of_slice},
    {"Array of Array", test_checker_array_of_array},
    {"bare String", test_checker_bare_string},
    {"bare Array", test_checker_array_args},
    {"redefining Array", test_checker_redefines_array},
    {"local Store", test_checker_local_store},
    {"local out of scope", test_checker_local_scope},
    {"local shadows parameter", test_checker_local_shadow},
    {"unknown field", test_checker_unknown_field},
    {"Error field write", test_checker_error_field_write},
};

const size_t checker_test_3_test_count =
    sizeof(checker_test_3_tests) / sizeof(checker_test_3_tests[0]);
