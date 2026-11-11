#ifndef NIPPY_TEST_CASES_H
#define NIPPY_TEST_CASES_H

typedef enum {
    TEST_PASS,
    TEST_FAIL,
    TEST_TODO,
    TEST_ERROR
} TestResult;

typedef struct {
    const char *name;
    TestResult (*run)(void);
} TestCase;

/* positive */
TestResult test_13_compiles_and_runs(void);
TestResult test_type_declaration_compiles(void);
TestResult test_struct_values_and_inference(void);
TestResult test_function_compiles_and_runs(void);
TestResult test_function_type_and_higher_order(void);
TestResult test_function_type_annotation_and_struct_params(void);
TestResult test_recursive_function_compiles(void);
TestResult test_bool_and_if_elseif_else(void);
TestResult test_while_break_continue(void);
TestResult test_bool_type_and_logical_operators(void);
TestResult test_bool_struct_field(void);
TestResult test_program_is_newline_agnostic(void);

/* lexer */
TestResult test_lexer_integer_out_of_range(void);
TestResult test_lexer_identifier_too_long(void);
TestResult test_lexer_unexpected_character(void);
TestResult test_lexer_unexpected_byte(void);

/* parser */
TestResult test_parser_expected_var_type_or_fun(void);
TestResult test_parser_expected_variable_name(void);
TestResult test_parser_expected_type_name(void);
TestResult test_parser_expected_equals(void);
TestResult test_parser_expected_expression(void);
TestResult test_parser_expected_type_name_after_type(void);
TestResult test_parser_expected_at_least_one_field(void);
TestResult test_parser_expected_field_name(void);
TestResult test_parser_expected_field_colon(void);
TestResult test_parser_expected_field_type(void);
TestResult test_parser_expected_field_or_end(void);
TestResult test_parser_expected_argument(void);
TestResult test_parser_expected_comma_or_rparen(void);
TestResult test_parser_mixed_args(void);
TestResult test_parser_expected_function_name(void);
TestResult test_parser_expected_function_lparen(void);
TestResult test_parser_expected_parameter_name(void);
TestResult test_parser_expected_parameter_colon(void);
TestResult test_parser_expected_parameter_separator(void);
TestResult test_parser_expected_return_type(void);
TestResult test_parser_expected_statement_or_end(void);
TestResult test_parser_expected_end(void);
TestResult test_parser_expected_then(void);
TestResult test_parser_expected_rparen(void);
TestResult test_parser_expected_type(void);
TestResult test_parser_expected_function_type_lparen(void);
TestResult test_parser_expected_function_type_separator(void);
TestResult test_parser_expected_function_type_colon(void);
TestResult test_parser_expected_function_type_gt(void);

/* checker (user-reachable) */
TestResult test_checker_unknown_type(void);
TestResult test_checker_unknown_call(void);
TestResult test_checker_constructor_not_struct(void);
TestResult test_checker_constructor_arg_count(void);
TestResult test_checker_missing_constructor_arg(void);
TestResult test_checker_duplicate_constructor_arg(void);
TestResult test_checker_unknown_constructor_field(void);
TestResult test_checker_constructor_arg_type(void);
TestResult test_checker_initialiser_type_mismatch(void);
TestResult test_checker_duplicate_type(void);
TestResult test_checker_redefines_int32(void);
TestResult test_checker_unknown_name(void);
TestResult test_checker_global_in_function(void);
TestResult test_checker_type_as_value(void);
TestResult test_checker_not_callable(void);
TestResult test_checker_named_function_arg(void);
TestResult test_checker_function_arg_count(void);
TestResult test_checker_function_arg_type(void);
TestResult test_checker_return_type_mismatch(void);
TestResult test_checker_operator_type(void);
TestResult test_checker_field_type(void);
TestResult test_checker_duplicate_name(void);
TestResult test_checker_duplicate_parameter(void);
TestResult test_checker_condition_type(void);
TestResult test_checker_break_outside_loop(void);
TestResult test_checker_continue_outside_loop(void);
TestResult test_checker_function_may_not_return(void);
TestResult test_checker_redefines_bool(void);

/* driver */
TestResult test_driver_could_not_open_source(void);
TestResult test_driver_source_too_large(void);
TestResult test_driver_source_contains_nul(void);
TestResult test_driver_failed_to_read(void);
TestResult test_driver_usage(void);
TestResult test_driver_could_not_write_header(void);

/* internal / defensive */
TestResult test_compiler_source_is_null(void);
TestResult test_unknown_error_fallback(void);
TestResult test_checker_missing_ast_root(void);
TestResult test_checker_missing_ast_node(void);
TestResult test_checker_unsupported_ast_node(void);
TestResult test_checker_no_initialiser(void);
TestResult test_checker_missing_name_or_type(void);
TestResult test_checker_type_has_no_fields(void);
TestResult test_checker_missing_return(void);
TestResult test_analyser_root_out_of_range(void);
TestResult test_analyser_invalid_node_link(void);
TestResult test_analyser_invalid_name_index(void);
TestResult test_analyser_invalid_type_index(void);
TestResult test_analyser_invalid_child_index(void);
TestResult test_analyser_invalid_signature_index(void);
TestResult test_analyser_invalid_signature_link(void);
TestResult test_generator_unsupported_root(void);
TestResult test_generator_unsupported_field(void);
TestResult test_generator_invalid_identifier(void);
TestResult test_generator_invalid_header_name(void);
TestResult test_generator_invalid_line_name(void);
TestResult test_generator_unsupported_expression(void);
TestResult test_generator_unsupported_function(void);
TestResult test_generator_unsupported_type(void);
TestResult test_generator_unsupported_statement(void);

/* allocation failure + remaining driver paths */
TestResult test_allocation_failures_fail_closed(void);
TestResult test_allocation_failure_messages(void);
TestResult test_compiler_out_of_memory_is_reported(void);
TestResult test_driver_could_not_write_source(void);
TestResult test_driver_cc_failures(void);

#endif
