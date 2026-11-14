/* Parser errors: each way a program can be malformed. */

#include "compile_support.h"

TEST("struct defaults must be literals")
{
    return compile_fails("type R x: Int64 = 1 + 2 end\n",
                         "nonliteral struct default", MESSAGE_PARSER_DEFAULT_LITERAL);
}

TEST("function parameter defaults are not supported")
{
    return compile_fails("fun f(x: Int64 = 1) end\n",
                         "function default", MESSAGE_PARSER_EXPECTED_PARAMETER_SEPARATOR);
}

TEST("independent syntax errors are reported together")
{
    CompilerResult result = compiler_compile(
        "var a: = 1\nvar b: = 2\n", &test_config);
    bool ok = !result.succeeded && result.error_message != NULL &&
              strstr(result.error_message, "test.nip:1:") != NULL &&
              strstr(result.error_message, "test.nip:2:") != NULL;

    if (!ok) {
        fprintf(stderr, "  missing syntax errors: %s\n",
                result.error_message == NULL ? "(none)" : result.error_message);
    }
    compiler_result_free(&result);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("independent syntax errors in a function are reported together")
{
    CompilerResult result = compiler_compile(
        "fun bad()\nvar a: = 1\nvar b: = 2\nend\n", &test_config);
    bool ok = !result.succeeded && result.error_message != NULL &&
              strstr(result.error_message, "test.nip:2:") != NULL &&
              strstr(result.error_message, "test.nip:3:") != NULL;

    if (!ok) {
        fprintf(stderr, "  missing syntax errors: %s\n",
                result.error_message == NULL ? "(none)" : result.error_message);
    }
    compiler_result_free(&result);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("syntax recovery continues into other branches")
{
    CompilerResult result = compiler_compile(
        "fun bad()\n"
        "  if true then\n"
        "    var a: = 1\n"
        "  else\n"
        "    var b: = 2\n"
        "  end\n"
        "end\n", &test_config);
    bool ok = !result.succeeded && result.error_message != NULL &&
              strstr(result.error_message, "test.nip:3:") != NULL &&
              strstr(result.error_message, "test.nip:5:") != NULL;
    if (!ok) {
        fprintf(stderr, "  missing branch errors: %s\n",
                result.error_message == NULL ? "(none)" : result.error_message);
    }
    compiler_result_free(&result);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("syntax recovery continues through match arms")
{
    CompilerResult result = compiler_compile(
        "var x: Bool = true\n"
        "match x with then print(1)\n"
        "with then print(2) else print(3) end\n", &test_config);
    bool ok = !result.succeeded && result.error_message != NULL &&
              strstr(result.error_message, "test.nip:2:") != NULL &&
              strstr(result.error_message, "test.nip:3:") != NULL;
    if (!ok) {
        fprintf(stderr, "  missing match syntax errors: %s\n",
                result.error_message == NULL ? "(none)" : result.error_message);
    }
    compiler_result_free(&result);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("syntax recovery continues at identifier statements")
{
    CompilerResult result = compiler_compile(
        "fun bad()\n"
        "  first(,)\n"
        "  second(,)\n"
        "  value = )\n"
        "  other = )\n"
        "end\n", &test_config);
    bool ok = !result.succeeded && result.error_message != NULL &&
              strstr(result.error_message, "test.nip:2:") != NULL &&
              strstr(result.error_message, "test.nip:3:") != NULL &&
              strstr(result.error_message, "test.nip:4:") != NULL &&
              strstr(result.error_message, "test.nip:5:") != NULL;
    if (!ok) {
        fprintf(stderr, "  missing statement errors: %s\n",
                result.error_message == NULL ? "(none)" : result.error_message);
    }
    compiler_result_free(&result);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("syntax recovery continues after unmatched delimiters")
{
    CompilerResult result = compiler_compile(
        "fun bad()\n"
        "  first(,\n"
        "  second(,\n"
        "  third(,)\n"
        "end\n", &test_config);
    bool ok = !result.succeeded && result.error_message != NULL &&
              strstr(result.error_message, "test.nip:2:") != NULL &&
              strstr(result.error_message, "test.nip:3:") != NULL &&
              strstr(result.error_message, "test.nip:4:") != NULL;
    if (!ok) {
        fprintf(stderr, "  missing delimiter errors: %s\n",
                result.error_message == NULL ? "(none)" : result.error_message);
    }
    compiler_result_free(&result);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("match recovery continues after unmatched delimiters")
{
    CompilerResult result = compiler_compile(
        "var x: Bool = true\n"
        "match x with first(,\n"
        "with second(,\n"
        "with third(,) then print(1) else print(2) end\n", &test_config);
    bool ok = !result.succeeded && result.error_message != NULL &&
              strstr(result.error_message, "test.nip:2:") != NULL &&
              strstr(result.error_message, "test.nip:3:") != NULL &&
              strstr(result.error_message, "test.nip:4:") != NULL;
    if (!ok) {
        fprintf(stderr, "  missing match delimiter errors: %s\n",
                result.error_message == NULL ? "(none)" : result.error_message);
    }
    compiler_result_free(&result);
    return ok ? TEST_PASS : TEST_FAIL;
}

#include <string.h>

TEST("expected var, type, or fun")
{
    return compile_fails(")\n", "a token that cannot start a declaration",
                         MESSAGE_PARSER_EXPECTED_VAR_TYPE_OR_FUN);
}

TEST("expected variable name")
{
    return compile_fails("var : Int32 = 1\n", "a missing variable name",
                             MESSAGE_PARSER_EXPECTED_VARIABLE_NAME);
}

TEST("expected type name")
{
    return compile_fails("var x : = 1\n", "a missing type annotation",
                             MESSAGE_PARSER_EXPECTED_TYPE_NAME);
}

TEST("expected equals")
{
    return compile_fails("var x : Int32 13\n", "a declaration without '='",
                             MESSAGE_PARSER_EXPECTED_EQUALS);
}

TEST("expected expression")
{
    return compile_fails("var x : Int32 = +\n",
                             "an invalid initialiser expression",
                             MESSAGE_PARSER_EXPECTED_EXPRESSION);
}

TEST("excessive recursive syntax nesting is diagnosed")
{
    char source[1024];
    const char *prefix = "var x: Int32 = ";
    size_t offset = strlen(prefix);
    size_t i;
    CompilerResult result;
    int ok;

    memcpy(source, prefix, offset);
    for (i = 0; i < 300; i++) {
        source[offset++] = '(';
    }
    source[offset++] = '1';
    for (i = 0; i < 300; i++) {
        source[offset++] = ')';
    }
    source[offset++] = '\n';
    source[offset] = '\0';
    result = compiler_compile(source, NULL);
    ok = !result.succeeded && result.error_message != NULL;
    if (ok) {
        char expected[128];

        expect_message(expected, sizeof(expected),
                       MESSAGE_PARSER_NESTING_LIMIT, 256);
        ok = strstr(result.error_message, expected) != NULL;
    }
    compiler_result_free(&result);
    if (!ok) {
        fprintf(stderr, "  excessive parser nesting was not diagnosed\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}

TEST("excessive syntax tree depth is diagnosed")
{
    char source[4096];
    const char *prefix = "var x: Int32 = ";
    size_t offset = strlen(prefix);
    size_t i;
    CompilerResult result;
    int ok;

    memcpy(source, prefix, offset);
    for (i = 0; i < 1200; i++) {
        source[offset++] = '1';
        source[offset++] = '+';
    }
    source[offset++] = '1';
    source[offset++] = '\n';
    source[offset] = '\0';

    result = compiler_compile(source, NULL);
    ok = !result.succeeded && result.error_message != NULL;
    if (ok) {
        char expected[128];

        expect_message(expected, sizeof(expected),
                       MESSAGE_PARSER_TREE_DEPTH_LIMIT, 1024);
        ok = strstr(result.error_message, expected) != NULL;
    }
    compiler_result_free(&result);
    if (!ok) {
        fprintf(stderr, "  excessive syntax tree depth was not diagnosed\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}

TEST("expected type name after type")
{
    return compile_fails("type 13 x: Int32 end\n",
                             "a type declaration without a name",
                             MESSAGE_PARSER_EXPECTED_TYPE_NAME_AFTER_TYPE);
}

TEST("expected at least one field")
{
    return compile_fails("type Point end\n",
                             "a type declaration with no fields",
                             MESSAGE_PARSER_EXPECTED_AT_LEAST_ONE_FIELD);
}

TEST("expected field name")
{
    return compile_fails("type Point 13 : Int32 end\n",
                             "a field without a name",
                             MESSAGE_PARSER_EXPECTED_FIELD_NAME);
}

TEST("expected field colon")
{
    return compile_fails("type Point x Int32 end\n", "a field without ':'",
                             MESSAGE_PARSER_EXPECTED_FIELD_COLON);
}

TEST("expected field type")
{
    return compile_fails("type Point x : 13 end\n", "a field without a type",
                             MESSAGE_PARSER_EXPECTED_FIELD_TYPE);
}

TEST("expected field or end")
{
    return compile_fails("type Point x : Int32 13 end\n",
                             "a field not followed by another field or 'end'",
                             MESSAGE_PARSER_EXPECTED_FIELD_OR_END);
}

TEST("expected argument")
{
    return compile_fails("type P x: Int32 end\nvar p = P(+)\n",
                             "a missing call argument",
                             MESSAGE_PARSER_EXPECTED_ARGUMENT);
}

TEST("expected comma or rparen")
{
    return compile_fails("type P x: Int32 end\nvar p = P(1 2)\n",
                             "constructor arguments without a separator",
                             MESSAGE_PARSER_EXPECTED_COMMA_OR_RPAREN);
}

TEST("mixed named and positional args")
{
    return compile_fails("type P x: Int32 y: Int32 end\nvar p = P(1, y = 2)\n",
                             "mixed named and positional constructor arguments",
                             MESSAGE_PARSER_MIXED_ARGS);
}

TEST("expected function name")
{
    return compile_fails("fun (x:Int32):Int32 return x end\n",
                             "a function without a name",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_NAME);
}

TEST("expected function lparen")
{
    return compile_fails("fun f x:Int32):Int32 return x end\n",
                             "a function without '('",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_LPAREN);
}

TEST("expected parameter name")
{
    return compile_fails("fun f(:Int32):Int32 return 1 end\n",
                             "a parameter without a name",
                             MESSAGE_PARSER_EXPECTED_PARAMETER_NAME);
}

TEST("expected parameter colon")
{
    return compile_fails("fun f(x Int32):Int32 return x end\n",
                             "a parameter without ':'",
                             MESSAGE_PARSER_EXPECTED_PARAMETER_COLON);
}

TEST("expected parameter separator")
{
    return compile_fails("fun f(x:Int32 y:Int32):Int32 return x end\n",
                             "parameters without a separator",
                             MESSAGE_PARSER_EXPECTED_PARAMETER_SEPARATOR);
}

TEST("expected return type")
{
    return compile_fails("fun f(x:Int32): return x end\n",
                             "a function with an incomplete return annotation",
                             MESSAGE_PARSER_EXPECTED_TYPE);
}

TEST("expected end")
{
    return compile_fails("fun f(x:Int32):Int32 return x\n",
                             "a function without 'end'",
                             MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
}

TEST("expected then")
{
    return compile_fails("fun f(x:Int32):Int32 if x > 0 return 1 end end\n",
                             "an if without 'then'",
                             MESSAGE_PARSER_EXPECTED_THEN);
}

TEST("while requires do")
{
    return compile_fails("while true\nend\n", "a while without 'do'",
                         MESSAGE_PARSER_EXPECTED_WHILE_DO);
}

TEST("expected rparen")
{
    return compile_fails("var n : Int32 = (1 + 2\n",
                             "a grouped expression without ')'",
                             MESSAGE_PARSER_EXPECTED_RPAREN);
}

TEST("expected type")
{
    return compile_fails("fun f(x:):Int32 return x end\n",
                             "a missing parameter type",
                             MESSAGE_PARSER_EXPECTED_TYPE);
}

TEST("expected function type lparen")
{
    return compile_fails("var f : Fun<Int32> = 1\n",
                             "a function type without '('",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_LPAREN);
}

TEST("expected function type separator")
{
    return compile_fails("var f : Fun<(Int32 Int32):Int32> = 1\n",
                             "function type parameters without a separator",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_SEPARATOR);
}

TEST("expected function type return")
{
    return compile_fails("var f : Fun<(Int32):> = 1\n",
                             "a function type with an incomplete return",
                             MESSAGE_PARSER_EXPECTED_TYPE);
}

TEST("expected function type gt")
{
    return compile_fails("var f : Fun<(Int32):Int32 = 1\n",
                             "a function type without '>'",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_GT);
}

TEST("type parameter name")
{
    return compile_fails("fun f<>(x:Int32):Int32\n  return x\nend\n",
                         "an empty type parameter list",
                         MESSAGE_PARSER_EXPECTED_TYPE_PARAMETER_NAME);
}

TEST("type parameter separator")
{
    return compile_fails("fun f<T U>(x:Int32):Int32\n  return x\nend\n",
                         "type parameters without a separator",
                         MESSAGE_PARSER_EXPECTED_TYPE_PARAMETER_SEPARATOR);
}

TEST("type parameter gt")
{
    return compile_fails("fun f<T", "a type parameter list without '>'",
                         MESSAGE_PARSER_EXPECTED_TYPE_PARAMETER_GT);
}

TEST("type argument")
{
    const char *source =
        "type Box<T>\n"
        "  value: T\n"
        "end\n"
        "var b: Box<> = 1\n";

    return compile_fails(source, "an empty type argument list",
                         MESSAGE_PARSER_EXPECTED_TYPE_ARGUMENT);
}

TEST("type argument separator")
{
    const char *source =
        "type Box<T>\n"
        "  value: T\n"
        "end\n"
        "var b: Box<Int32 Bool> = 1\n";

    return compile_fails(source, "type arguments without a separator",
                         MESSAGE_PARSER_EXPECTED_TYPE_ARGUMENT_SEPARATOR);
}

TEST("type argument gt")
{
    const char *source =
        "type Box<T>\n"
        "  value: T\n"
        "end\n"
        "var b: Box<Int32";

    return compile_fails(source, "a type argument list without '>'",
                         MESSAGE_PARSER_EXPECTED_TYPE_ARGUMENT_GT);
}

TEST("union alternative")
{
    return compile_fails("var x: Int32 | = 3\n",
                         "a union without its second alternative",
                         MESSAGE_PARSER_EXPECTED_UNION_ALTERNATIVE);
}

TEST("member name")
{
    return compile_fails("fun f(x:Int32):Bool\n  return x. end\nend\n",
                         "a dot without a member name",
                         MESSAGE_PARSER_EXPECTED_MEMBER_NAME);
}

TEST("string: a missing Text capacity")
{
    return compile_fails(
        "var s: String<> = \"\"\n",
        "a missing Text capacity",
        MESSAGE_PARSER_EXPECTED_STRING_LENGTH);
}

TEST("string: a Text capacity trailing comma")
{
    return compile_fails(
        "var s: String<4, 8> = \"\"\n",
        "a Text capacity trailing comma",
        MESSAGE_PARSER_EXPECTED_STRING_GT);
}

TEST("assignment: an assignment without a value")
{
    return compile_fails(
        "var n: Int32 = 1\nn =\n",
        "an assignment without a value",
        MESSAGE_PARSER_EXPECTED_EXPRESSION);
}

TEST("assignment: assignment as an expression")
{
    return compile_fails(
        "var n: Int32 = 1\nvar m: Int32 = (n = 2)\n",
        "assignment used as an expression",
        MESSAGE_PARSER_EXPECTED_RPAREN);
}

TEST("list: list missing capacity")
{
    return compile_fails(
        "var xs: Array<Int32,> = [1]\n",
        "list missing capacity",
        MESSAGE_PARSER_EXPECTED_ARRAY_CAPACITY);
}

TEST("list: list missing >")
{
    return compile_fails(
        "var xs: Array<Int32, 1 = [1]\n",
        "list missing >",
        MESSAGE_PARSER_EXPECTED_ARRAY_GT);
}

TEST("list: list missing ]")
{
    return compile_fails(
        "var xs: Array<Int32, 1> = [1\n",
        "list missing ]",
        MESSAGE_PARSER_EXPECTED_LIST_RBRACKET);
}

TEST("list: list missing comma")
{
    return compile_fails(
        "var xs: Array<Int32, 1> = [1 2]\n",
        "list missing comma",
        MESSAGE_PARSER_EXPECTED_LIST_SEPARATOR);
}

TEST("list: index missing ]")
{
    return compile_fails(
        "var n: Int32 = [1, 2][0\n",
        "index missing ]",
        MESSAGE_PARSER_EXPECTED_RBRACKET);
}

TEST("for missing in")
{
    return compile_fails(
        "for x 1 do\nend\n",
        "for missing in",
        MESSAGE_PARSER_EXPECTED_IN);
}

TEST("for missing do")
{
    return compile_fails(
        "var xs: Array<Int32, 2> = [1, 2]\nfor x in xs\nend\n",
        "for missing do",
        MESSAGE_PARSER_EXPECTED_DO);
}

TEST("list: dynamic list missing >")
{
    return compile_fails(
        "var xs: List<Int32 = []\n",
        "dynamic list missing >",
        MESSAGE_PARSER_EXPECTED_LIST_GT);
}

TEST("List capacity")
{
    return compile_fails(
        "var xs: List<Int32, 1> = [1]\n",
        "List capacity",
        MESSAGE_PARSER_LIST_CAPACITY);
}

TEST("Record missing capacity")
{
    return compile_fails(
        "var x: Record<String<8>, Bool> = {}\n",
        "Record missing capacity",
        MESSAGE_PARSER_EXPECTED_RECORD_CAPACITY);
}

TEST("Record missing >")
{
    return compile_fails(
        "var x: Record<String<8>, Bool, 2 = {}\n",
        "Record missing >",
        MESSAGE_PARSER_EXPECTED_RECORD_GT);
}

TEST("Table missing >")
{
    return compile_fails(
        "var x: Table<String<8>, Bool = arena.table<String<8>, Bool>(1)\n",
        "Table missing >",
        MESSAGE_PARSER_EXPECTED_TABLE_GT);
}

TEST("Record missing colon")
{
    return compile_fails(
        "var x: Record<String<8>, Bool, 2> = {\"a\" true}\n",
        "Record missing colon",
        MESSAGE_PARSER_EXPECTED_COLON);
}

TEST("Record missing }")
{
    return compile_fails(
        "var x: Record<String<8>, Bool, 2> = {\"a\": true\n",
        "Record missing }",
        MESSAGE_PARSER_EXPECTED_RBRACE);
}

TEST("typeof missing is")
{
    return compile_fails(
        "var b: Bool = typeof 1\n",
        "typeof missing is",
        MESSAGE_PARSER_EXPECTED_IS);
}

TEST("match head expression")
{
    return compile_accepts(
        "match 1 with 1 then else end\n",
        "match head expression");
}

TEST("match missing with")
{
    return compile_fails(
        "var x: Bool = true\nmatch x 1 then\nend\n",
        "match missing with",
        MESSAGE_PARSER_EXPECTED_WITH);
}

TEST("if expression missing else")
{
    return compile_fails(
        "var z: Int32 = if true then 1 end\n",
        "if expression missing else",
        MESSAGE_PARSER_EXPECTED_ELSE);
}

TEST("match expression missing else")
{
    return compile_fails(
        "var b: Bool = true\n"
        "var z: Int32 = match b with true then 1 end\n",
        "match expression missing else",
        MESSAGE_PARSER_MATCH_EXPECTED_ELSE);
}
