#include "test_cases.h"

#include <stdio.h>

int main(void)
{
    const TestCase tests[] = {
        {"var declaration of 13 compiles and prints 13", test_13_compiles_and_runs},
        {"type declaration compiles to a C struct", test_type_declaration_compiles},
        {"struct values with named and inferred args", test_struct_values_and_inference},
        {"function compiles and runs", test_function_compiles_and_runs},
        {"function type and higher-order call", test_function_type_and_higher_order},
        {"function type annotation and indirect call", test_function_type_annotation_and_struct_params},
        {"recursive function compiles", test_recursive_function_compiles},
        {"Bool and if/elseif/else", test_bool_and_if_elseif_else},
        {"while with break and continue", test_while_break_continue},
        {"Bool type and logical operators", test_bool_type_and_logical_operators},
        {"Bool struct field", test_bool_struct_field},
        {"program is newline and whitespace agnostic", test_program_is_newline_agnostic},

        {"lexer: integer literal out of range", test_lexer_integer_out_of_range},
        {"lexer: identifier too long", test_lexer_identifier_too_long},
        {"lexer: unexpected character", test_lexer_unexpected_character},
        {"lexer: unexpected byte", test_lexer_unexpected_byte},

        {"parser: expected var, type, or fun", test_parser_expected_var_type_or_fun},
        {"parser: expected variable name", test_parser_expected_variable_name},
        {"parser: expected type name", test_parser_expected_type_name},
        {"parser: expected equals", test_parser_expected_equals},
        {"parser: expected expression", test_parser_expected_expression},
        {"parser: expected type name after type", test_parser_expected_type_name_after_type},
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
        {"parser: expected statement or end", test_parser_expected_statement_or_end},
        {"parser: expected end", test_parser_expected_end},
        {"parser: expected then", test_parser_expected_then},
        {"parser: expected rparen", test_parser_expected_rparen},
        {"parser: expected type", test_parser_expected_type},
        {"parser: expected function type lparen", test_parser_expected_function_type_lparen},
        {"parser: expected function type separator", test_parser_expected_function_type_separator},
        {"parser: expected function type colon", test_parser_expected_function_type_colon},
        {"parser: expected function type gt", test_parser_expected_function_type_gt},

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
        {"checker: field type", test_checker_field_type},
        {"checker: duplicate name", test_checker_duplicate_name},
        {"checker: duplicate parameter", test_checker_duplicate_parameter},
        {"checker: condition type", test_checker_condition_type},
        {"checker: break outside loop", test_checker_break_outside_loop},
        {"checker: continue outside loop", test_checker_continue_outside_loop},
        {"checker: function may not return", test_checker_function_may_not_return},
        {"checker: redefines Bool", test_checker_redefines_bool},

        {"driver: could not open source", test_driver_could_not_open_source},
        {"driver: source too large", test_driver_source_too_large},
        {"driver: source contains NUL", test_driver_source_contains_nul},
        {"driver: failed to read source", test_driver_failed_to_read},
        {"driver: usage", test_driver_usage},
        {"driver: could not write header", test_driver_could_not_write_header},

        {"compiler: source string is null", test_compiler_source_is_null},
        {"fallback: unknown error", test_unknown_error_fallback},
        {"checker: missing AST root", test_checker_missing_ast_root},
        {"checker: missing AST node", test_checker_missing_ast_node},
        {"checker: unsupported AST node", test_checker_unsupported_ast_node},
        {"checker: no initialiser", test_checker_no_initialiser},
        {"checker: missing name or type", test_checker_missing_name_or_type},
        {"checker: type has no fields", test_checker_type_has_no_fields},
        {"checker: missing return", test_checker_missing_return},
        {"analyser: root out of range", test_analyser_root_out_of_range},
        {"analyser: invalid node link", test_analyser_invalid_node_link},
        {"analyser: invalid name index", test_analyser_invalid_name_index},
        {"analyser: invalid type index", test_analyser_invalid_type_index},
        {"analyser: invalid child index", test_analyser_invalid_child_index},
        {"analyser: invalid signature index", test_analyser_invalid_signature_index},
        {"analyser: invalid signature link", test_analyser_invalid_signature_link},
        {"generator: unsupported root", test_generator_unsupported_root},
        {"generator: unsupported field", test_generator_unsupported_field},
        {"generator: invalid identifier", test_generator_invalid_identifier},
        {"generator: invalid header name", test_generator_invalid_header_name},
        {"generator: invalid line name", test_generator_invalid_line_name},
        {"generator: unsupported expression", test_generator_unsupported_expression},
        {"generator: unsupported function", test_generator_unsupported_function},
        {"generator: unsupported type", test_generator_unsupported_type},
        {"generator: unsupported statement", test_generator_unsupported_statement},

        {"allocation failures fail closed", test_allocation_failures_fail_closed},
        {"allocation failure messages", test_allocation_failure_messages},
        {"compiler: injected out of memory", test_compiler_out_of_memory_is_reported},
        {"driver: could not write source", test_driver_could_not_write_source},
        {"driver: C compiler failures", test_driver_cc_failures}
    };
    const size_t test_count = sizeof(tests) / sizeof(tests[0]);
    size_t i;
    size_t passed = 0;
    size_t failed = 0;
    size_t todo = 0;
    size_t errors = 0;

    for (i = 0; i < test_count; i++) {
        TestResult result = tests[i].run();

        switch (result) {
        case TEST_PASS:
            printf("PASS: %s\n", tests[i].name);
            passed++;
            break;
        case TEST_FAIL:
            fprintf(stderr, "FAIL: %s\n", tests[i].name);
            failed++;
            break;
        case TEST_TODO:
            printf("TODO: %s\n", tests[i].name);
            todo++;
            break;
        case TEST_ERROR:
        default:
            fprintf(stderr, "ERROR: %s\n", tests[i].name);
            errors++;
            break;
        }
    }

    printf("%zu tests: %zu PASS, %zu FAIL, %zu TODO, %zu Error\n",
           test_count, passed, failed, todo, errors);
    return failed == 0 && errors == 0 ? 0 : 1;
}
