#include "test_support.h"
#include "golden_data.h"


/* ---- lexer messages ---- */

TestResult test_lexer_integer_out_of_range(void)
{
    return compile_fails("var x : Int32 = 99999999999999999999999999\n",
                             "an out-of-range integer literal",
                             MESSAGE_LEXER_INTEGER_OUT_OF_RANGE);
}


TestResult test_lexer_identifier_too_long(void)
{
    return compile_fails(
        "var abcdefghijklmnopqrstuvwxyzABCDEFG : Int32 = 1\n",
        "an over-long identifier",
        MESSAGE_LEXER_IDENTIFIER_TOO_LONG, NIPPY_IDENTIFIER_MAX - 1);
}


TestResult test_lexer_unexpected_character(void)
{
    return compile_fails("var x : Int32 = 13 @\n",
                             "an unexpected printable character",
                             MESSAGE_LEXER_UNEXPECTED_CHARACTER, '@');
}


TestResult test_lexer_unexpected_byte(void)
{
    return compile_fails("var x : Int32 = 13 \x80\n",
                             "an unexpected non-printable byte",
                             MESSAGE_LEXER_UNEXPECTED_BYTE, 0x80);
}


/* ---- parser messages ---- */

TestResult test_parser_expected_var_type_or_fun(void)
{
    return compile_fails(")\n", "a token that cannot start a declaration",
                         MESSAGE_PARSER_EXPECTED_VAR_TYPE_OR_FUN);
}


TestResult test_parser_expected_variable_name(void)
{
    return compile_fails("var : Int32 = 1\n", "a missing variable name",
                             MESSAGE_PARSER_EXPECTED_VARIABLE_NAME);
}


TestResult test_parser_expected_type_name(void)
{
    return compile_fails("var x : = 1\n", "a missing type annotation",
                             MESSAGE_PARSER_EXPECTED_TYPE_NAME);
}


TestResult test_parser_expected_equals(void)
{
    return compile_fails("var x : Int32 13\n", "a declaration without '='",
                             MESSAGE_PARSER_EXPECTED_EQUALS);
}


TestResult test_parser_expected_expression(void)
{
    return compile_fails("var x : Int32 = +\n",
                             "an invalid initialiser expression",
                             MESSAGE_PARSER_EXPECTED_EXPRESSION);
}


TestResult test_parser_expected_type_name_after_type(void)
{
    return compile_fails("type 13 x: Int32 end\n",
                             "a type declaration without a name",
                             MESSAGE_PARSER_EXPECTED_TYPE_NAME_AFTER_TYPE);
}


TestResult test_parser_expected_at_least_one_field(void)
{
    return compile_fails("type Point end\n",
                             "a type declaration with no fields",
                             MESSAGE_PARSER_EXPECTED_AT_LEAST_ONE_FIELD);
}


TestResult test_parser_expected_field_name(void)
{
    return compile_fails("type Point 13 : Int32 end\n",
                             "a field without a name",
                             MESSAGE_PARSER_EXPECTED_FIELD_NAME);
}


TestResult test_parser_expected_field_colon(void)
{
    return compile_fails("type Point x Int32 end\n", "a field without ':'",
                             MESSAGE_PARSER_EXPECTED_FIELD_COLON);
}


TestResult test_parser_expected_field_type(void)
{
    return compile_fails("type Point x : 13 end\n", "a field without a type",
                             MESSAGE_PARSER_EXPECTED_FIELD_TYPE);
}


TestResult test_parser_expected_field_or_end(void)
{
    return compile_fails("type Point x : Int32 13 end\n",
                             "a field not followed by another field or 'end'",
                             MESSAGE_PARSER_EXPECTED_FIELD_OR_END);
}


TestResult test_parser_expected_argument(void)
{
    return compile_fails("type P x: Int32 end\nvar p = P(+)\n",
                             "a missing call argument",
                             MESSAGE_PARSER_EXPECTED_ARGUMENT);
}


TestResult test_parser_expected_comma_or_rparen(void)
{
    return compile_fails("type P x: Int32 end\nvar p = P(1 2)\n",
                             "constructor arguments without a separator",
                             MESSAGE_PARSER_EXPECTED_COMMA_OR_RPAREN);
}


TestResult test_parser_mixed_args(void)
{
    return compile_fails("type P x: Int32 y: Int32 end\nvar p = P(1, y = 2)\n",
                             "mixed named and positional constructor arguments",
                             MESSAGE_PARSER_MIXED_ARGS);
}


TestResult test_parser_expected_function_name(void)
{
    return compile_fails("fun (x:Int32):Int32 return x end\n",
                             "a function without a name",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_NAME);
}


TestResult test_parser_expected_function_lparen(void)
{
    return compile_fails("fun f x:Int32):Int32 return x end\n",
                             "a function without '('",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_LPAREN);
}


TestResult test_parser_expected_parameter_name(void)
{
    return compile_fails("fun f(:Int32):Int32 return 1 end\n",
                             "a parameter without a name",
                             MESSAGE_PARSER_EXPECTED_PARAMETER_NAME);
}


TestResult test_parser_expected_parameter_colon(void)
{
    return compile_fails("fun f(x Int32):Int32 return x end\n",
                             "a parameter without ':'",
                             MESSAGE_PARSER_EXPECTED_PARAMETER_COLON);
}


TestResult test_parser_expected_parameter_separator(void)
{
    return compile_fails("fun f(x:Int32 y:Int32):Int32 return x end\n",
                             "parameters without a separator",
                             MESSAGE_PARSER_EXPECTED_PARAMETER_SEPARATOR);
}


TestResult test_parser_expected_return_type(void)
{
    return compile_fails("fun f(x:Int32) return x end\n",
                             "a function without a return type",
                             MESSAGE_PARSER_EXPECTED_RETURN_TYPE);
}


TestResult test_parser_expected_end(void)
{
    return compile_fails("fun f(x:Int32):Int32 return x\n",
                             "a function without 'end'",
                             MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
}


TestResult test_parser_expected_then(void)
{
    return compile_fails("fun f(x:Int32):Int32 if x > 0 return 1 end end\n",
                             "an if without 'then'",
                             MESSAGE_PARSER_EXPECTED_THEN);
}


TestResult test_parser_expected_rparen(void)
{
    return compile_fails("var n : Int32 = (1 + 2\n",
                             "a grouped expression without ')'",
                             MESSAGE_PARSER_EXPECTED_RPAREN);
}


TestResult test_parser_expected_type(void)
{
    return compile_fails("fun f(x:):Int32 return x end\n",
                             "a missing parameter type",
                             MESSAGE_PARSER_EXPECTED_TYPE);
}


TestResult test_parser_expected_function_type_lparen(void)
{
    return compile_fails("var f : Fun<Int32> = 1\n",
                             "a function type without '('",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_LPAREN);
}


TestResult test_parser_expected_function_type_separator(void)
{
    return compile_fails("var f : Fun<(Int32 Int32):Int32> = 1\n",
                             "function type parameters without a separator",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_SEPARATOR);
}


TestResult test_parser_expected_function_type_colon(void)
{
    return compile_fails("var f : Fun<(Int32)> = 1\n",
                             "a function type without a return type colon",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_COLON);
}


TestResult test_parser_expected_function_type_gt(void)
{
    return compile_fails("var f : Fun<(Int32):Int32 = 1\n",
                             "a function type without '>'",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_GT);
}



/* ---- new parser diagnostics ---- */

TestResult test_parser_expected_type_parameter_name(void)
{
    return compile_fails("fun f<>(x:Int32):Int32\n  return x\nend\n",
                         "an empty type parameter list",
                         MESSAGE_PARSER_EXPECTED_TYPE_PARAMETER_NAME);
}


TestResult test_parser_expected_type_parameter_separator(void)
{
    return compile_fails("fun f<T U>(x:Int32):Int32\n  return x\nend\n",
                         "type parameters without a separator",
                         MESSAGE_PARSER_EXPECTED_TYPE_PARAMETER_SEPARATOR);
}


TestResult test_parser_expected_type_parameter_gt(void)
{
    return compile_fails("fun f<T", "a type parameter list without '>'",
                         MESSAGE_PARSER_EXPECTED_TYPE_PARAMETER_GT);
}


TestResult test_parser_expected_type_argument(void)
{
    const char *source =
        "type Box<T>\n"
        "  value: T\n"
        "end\n"
        "var b: Box<> = 1\n";

    return compile_fails(source, "an empty type argument list",
                         MESSAGE_PARSER_EXPECTED_TYPE_ARGUMENT);
}


TestResult test_parser_expected_type_argument_separator(void)
{
    const char *source =
        "type Box<T>\n"
        "  value: T\n"
        "end\n"
        "var b: Box<Int32 Bool> = 1\n";

    return compile_fails(source, "type arguments without a separator",
                         MESSAGE_PARSER_EXPECTED_TYPE_ARGUMENT_SEPARATOR);
}


TestResult test_parser_expected_type_argument_gt(void)
{
    const char *source =
        "type Box<T>\n"
        "  value: T\n"
        "end\n"
        "var b: Box<Int32";

    return compile_fails(source, "a type argument list without '>'",
                         MESSAGE_PARSER_EXPECTED_TYPE_ARGUMENT_GT);
}


TestResult test_parser_expected_union_alternative(void)
{
    return compile_fails("var x: Int32 | = 3\n",
                         "a union without its second alternative",
                         MESSAGE_PARSER_EXPECTED_UNION_ALTERNATIVE);
}


TestResult test_parser_expected_member_name(void)
{
    return compile_fails("fun f(x:Int32):Bool\n  return x. end\nend\n",
                         "a dot without a member name",
                         MESSAGE_PARSER_EXPECTED_MEMBER_NAME);
}


TestResult test_lexer_unterminated_string(void)
{
    return compile_fails(
        "var s: String<4> = \"ab",
        "an unterminated string literal",
        MESSAGE_LEXER_UNTERMINATED_STRING);
}


TestResult test_lexer_string_newline(void)
{
    return compile_fails(
        "var s: String<8> = \"a\nb\"\n",
        "a newline inside a string literal",
        MESSAGE_LEXER_STRING_NEWLINE);
}


TestResult test_lexer_invalid_escape(void)
{
    return compile_fails(
        "var s: String<8> = \"a\\qb\"\n",
        "an invalid string escape",
        MESSAGE_LEXER_INVALID_ESCAPE, 'q');
}


TestResult test_parser_string_length(void)
{
    return compile_fails(
        "var s: String<> = \"\"\n",
        "a missing Text capacity",
        MESSAGE_PARSER_EXPECTED_STRING_LENGTH);
}


TestResult test_parser_string_gt(void)
{
    return compile_fails(
        "var s: String<4, 8> = \"\"\n",
        "a Text capacity trailing comma",
        MESSAGE_PARSER_EXPECTED_STRING_GT);
}


TestResult test_parser_assign_missing_value(void)
{
    return compile_fails(
        "var n: Int32 = 1\nn =\n",
        "an assignment without a value",
        MESSAGE_PARSER_EXPECTED_EXPRESSION);
}




TestResult test_parser_assignment_expression(void)
{
    return compile_fails(
        "var n: Int32 = 1\nvar m: Int32 = (n = 2)\n",
        "assignment used as an expression",
        MESSAGE_PARSER_EXPECTED_RPAREN);
}


TestResult test_parser_list_capacity(void)
{
    return compile_fails(
        "var xs: Array<Int32,> = [1]\n",
        "list missing capacity",
        MESSAGE_PARSER_EXPECTED_ARRAY_CAPACITY);
}


TestResult test_parser_list_gt(void)
{
    return compile_fails(
        "var xs: Array<Int32, 1 = [1]\n",
        "list missing >",
        MESSAGE_PARSER_EXPECTED_ARRAY_GT);
}


TestResult test_parser_list_rbracket(void)
{
    return compile_fails(
        "var xs: Array<Int32, 1> = [1\n",
        "list missing ]",
        MESSAGE_PARSER_EXPECTED_LIST_RBRACKET);
}


TestResult test_parser_list_separator(void)
{
    return compile_fails(
        "var xs: Array<Int32, 1> = [1 2]\n",
        "list missing comma",
        MESSAGE_PARSER_EXPECTED_LIST_SEPARATOR);
}


TestResult test_parser_index_rbracket(void)
{
    return compile_fails(
        "var n: Int32 = [1, 2][0\n",
        "index missing ]",
        MESSAGE_PARSER_EXPECTED_RBRACKET);
}


TestResult test_parser_for_in(void)
{
    return compile_fails(
        "for x 1 do\nend\n",
        "for missing in",
        MESSAGE_PARSER_EXPECTED_IN);
}


TestResult test_parser_for_do(void)
{
    return compile_fails(
        "var xs: Array<Int32, 2> = [1, 2]\nfor x in xs\nend\n",
        "for missing do",
        MESSAGE_PARSER_EXPECTED_DO);
}


TestResult test_parser_dynamic_list_gt(void)
{
    return compile_fails(
        "var xs: List<Int32 = []\n",
        "dynamic list missing >",
        MESSAGE_PARSER_EXPECTED_LIST_GT);
}


TestResult test_parser_list_capacity_split(void)
{
    return compile_fails(
        "var xs: List<Int32, 1> = [1]\n",
        "List capacity",
        MESSAGE_PARSER_LIST_CAPACITY);
}


TestResult test_parser_record_capacity(void)
{
    return compile_fails(
        "var x: Record<String<8>, Bool> = {}\n",
        "Record missing capacity",
        MESSAGE_PARSER_EXPECTED_RECORD_CAPACITY);
}


TestResult test_parser_record_gt(void)
{
    return compile_fails(
        "var x: Record<String<8>, Bool, 2 = {}\n",
        "Record missing >",
        MESSAGE_PARSER_EXPECTED_RECORD_GT);
}


TestResult test_parser_table_gt(void)
{
    return compile_fails(
        "var x: Table<String<8>, Bool = arena.table<String<8>, Bool>(1)\n",
        "Table missing >",
        MESSAGE_PARSER_EXPECTED_TABLE_GT);
}


TestResult test_parser_record_colon(void)
{
    return compile_fails(
        "var x: Record<String<8>, Bool, 2> = {\"a\" true}\n",
        "Record missing colon",
        MESSAGE_PARSER_EXPECTED_COLON);
}


TestResult test_parser_record_rbrace(void)
{
    return compile_fails(
        "var x: Record<String<8>, Bool, 2> = {\"a\": true\n",
        "Record missing }",
        MESSAGE_PARSER_EXPECTED_RBRACE);
}


TestResult test_parser_typeof_missing_is(void)
{
    return compile_fails(
        "var b: Bool = typeof 1\n",
        "typeof missing is",
        MESSAGE_PARSER_EXPECTED_IS);
}


TestResult test_parser_match_head(void)
{
    return compile_fails(
        "match 1 with 1 then\nend\n",
        "match head expression",
        MESSAGE_PARSER_MATCH_HEAD);
}


TestResult test_parser_match_with(void)
{
    return compile_fails(
        "var x: Bool = true\nmatch x 1 then\nend\n",
        "match missing with",
        MESSAGE_PARSER_EXPECTED_WITH);
}


TestResult test_parser_if_expression_else(void)
{
    return compile_fails(
        "var z: Int32 = if true then 1 end\n",
        "if expression missing else",
        MESSAGE_PARSER_EXPECTED_ELSE);
}


TestResult test_parser_match_expression_else(void)
{
    return compile_fails(
        "var b: Bool = true\n"
        "var z: Int32 = match b with true then 1 end\n",
        "match expression missing else",
        MESSAGE_PARSER_EXPECTED_ELSE);
}

const TestCase syntax_test_1_tests[] = {
    {"lexer: integer literal out of range", test_lexer_integer_out_of_range},
    {"lexer: identifier too long", test_lexer_identifier_too_long},
    {"lexer: unexpected character", test_lexer_unexpected_character},
    {"lexer: unexpected byte", test_lexer_unexpected_byte},
    {"parser: expected var, type, or fun", test_parser_expected_var_type_or_fun},
    {"parser: expected variable name", test_parser_expected_variable_name},
    {"parser: expected type name", test_parser_expected_type_name},
    {"parser: expected equals", test_parser_expected_equals},
    {"parser: expected expression", test_parser_expected_expression},
    {"parser: expected type name after type",
     test_parser_expected_type_name_after_type},
    {"parser: expected at least one field", test_parser_expected_at_least_one_field},
    {"parser: expected field name", test_parser_expected_field_name},
    {"parser: expected field colon", test_parser_expected_field_colon},
    {"parser: expected field type", test_parser_expected_field_type},
    {"parser: expected field or end", test_parser_expected_field_or_end},
    {"parser: expected argument", test_parser_expected_argument},
    {"parser: expected comma or rparen", test_parser_expected_comma_or_rparen},
    {"parser: mixed named and positional args", test_parser_mixed_args},
    {"parser: expected function name", test_parser_expected_function_name},
    {"parser: expected function lparen", test_parser_expected_function_lparen},
    {"parser: expected parameter name", test_parser_expected_parameter_name},
    {"parser: expected parameter colon", test_parser_expected_parameter_colon},
    {"parser: expected parameter separator", test_parser_expected_parameter_separator},
    {"parser: expected return type", test_parser_expected_return_type},
    {"parser: expected end", test_parser_expected_end},
    {"parser: expected then", test_parser_expected_then},
    {"parser: expected rparen", test_parser_expected_rparen},
    {"parser: expected type", test_parser_expected_type},
    {"parser: expected function type lparen",
     test_parser_expected_function_type_lparen},
    {"parser: expected function type separator",
     test_parser_expected_function_type_separator},
    {"parser: expected function type colon", test_parser_expected_function_type_colon},
    {"parser: expected function type gt", test_parser_expected_function_type_gt},
    {"parser: type parameter name", test_parser_expected_type_parameter_name},
    {"parser: type parameter separator", test_parser_expected_type_parameter_separator},
    {"parser: type parameter gt", test_parser_expected_type_parameter_gt},
    {"parser: type argument", test_parser_expected_type_argument},
    {"parser: type argument separator", test_parser_expected_type_argument_separator},
    {"parser: type argument gt", test_parser_expected_type_argument_gt},
    {"parser: union alternative", test_parser_expected_union_alternative},
    {"parser: member name", test_parser_expected_member_name},
    {"typeof missing is", test_parser_typeof_missing_is},
    {"match head expression", test_parser_match_head},
    {"match missing with", test_parser_match_with},
    {"if expression missing else", test_parser_if_expression_else},
    {"match expression missing else", test_parser_match_expression_else},
    {"Record missing colon", test_parser_record_colon},
    {"Record missing }", test_parser_record_rbrace},
    {"Record missing capacity", test_parser_record_capacity},
    {"Record missing >", test_parser_record_gt},
    {"Table missing >", test_parser_table_gt},
    {"string: an unterminated string literal", test_lexer_unterminated_string},
    {"string: a newline inside a string literal", test_lexer_string_newline},
    {"string: an invalid string escape", test_lexer_invalid_escape},
    {"string: a missing Text capacity", test_parser_string_length},
    {"string: a Text capacity trailing comma", test_parser_string_gt},
    {"list: list missing capacity", test_parser_list_capacity},
    {"list: list missing >", test_parser_list_gt},
    {"list: dynamic list missing >", test_parser_dynamic_list_gt},
    {"list: list missing ]", test_parser_list_rbracket},
    {"list: list missing comma", test_parser_list_separator},
    {"list: index missing ]", test_parser_index_rbracket},
    {"for missing in", test_parser_for_in},
    {"for missing do", test_parser_for_do},
    {"List capacity", test_parser_list_capacity_split},
    {"assignment: an assignment without a value", test_parser_assign_missing_value},
    {"assignment: assignment as an expression", test_parser_assignment_expression},
};

const size_t syntax_test_1_test_count =
    sizeof(syntax_test_1_tests) / sizeof(syntax_test_1_tests[0]);
