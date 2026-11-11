#ifndef NIPPY_MESSAGES_H
#define NIPPY_MESSAGES_H

/* Single source of truth for every message the compiler emits to a user,
   diagnostic or otherwise. Stage code and the driver reference these names
   instead of bare literals so wording stays consistent and can be reviewed in
   one place. Messages are lowercase and prefixed with their stage name. */

/* Fallback */
#define MESSAGE_UNKNOWN_ERROR "Unknown Error"

/* Lexer */
#define MESSAGE_LEXER_INTEGER_OUT_OF_RANGE \
    "lexer: integer literal out of range"
#define MESSAGE_LEXER_IDENTIFIER_TOO_LONG \
    "lexer: identifier longer than %d bytes"
#define MESSAGE_LEXER_UNEXPECTED_CHARACTER \
    "lexer: unexpected character '%c'"
#define MESSAGE_LEXER_UNEXPECTED_BYTE \
    "lexer: unexpected byte 0x%02x"
#define MESSAGE_LEXER_OUT_OF_MEMORY "lexer: out of memory"

/* Compiler */
#define MESSAGE_COMPILER_SOURCE_IS_NULL "compiler: source string is null"

/* Parser */
#define MESSAGE_PARSER_EXPECTED_VAR_TYPE_OR_FUN \
    "parser: expected 'var', 'type', or 'fun' to start a declaration"
#define MESSAGE_PARSER_EXPECTED_VARIABLE_NAME \
    "parser: expected a variable name after 'var'"
#define MESSAGE_PARSER_OUT_OF_MEMORY_RECORDING_NAME \
    "parser: out of memory while recording a name"
#define MESSAGE_PARSER_EXPECTED_TYPE_NAME \
    "parser: expected a type name after ':'"
#define MESSAGE_PARSER_EXPECTED_EQUALS \
    "parser: expected '=' before the initialiser"
#define MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE \
    "parser: out of memory while building syntax tree"
#define MESSAGE_PARSER_EXPECTED_TYPE_NAME_AFTER_TYPE \
    "parser: expected a type name after 'type'"
#define MESSAGE_PARSER_EXPECTED_AT_LEAST_ONE_FIELD \
    "parser: expected at least one field before 'end'"
#define MESSAGE_PARSER_EXPECTED_FIELD_NAME \
    "parser: expected a field name"
#define MESSAGE_PARSER_EXPECTED_FIELD_COLON \
    "parser: expected ':' after the field name"
#define MESSAGE_PARSER_EXPECTED_FIELD_TYPE \
    "parser: expected a field type name after ':'"
#define MESSAGE_PARSER_EXPECTED_FIELD_OR_END \
    "parser: expected a field or 'end'"
#define MESSAGE_PARSER_EXPECTED_EXPRESSION \
    "parser: expected an expression"
#define MESSAGE_PARSER_EXPECTED_ARGUMENT \
    "parser: expected an argument expression"
#define MESSAGE_PARSER_EXPECTED_COMMA_OR_RPAREN \
    "parser: expected ',' or ')' after an argument"
#define MESSAGE_PARSER_MIXED_ARGS \
    "parser: cannot mix named and positional arguments"
#define MESSAGE_PARSER_EXPECTED_FUNCTION_NAME \
    "parser: expected a function name after 'fun'"
#define MESSAGE_PARSER_EXPECTED_FUNCTION_LPAREN \
    "parser: expected '(' after the function name"
#define MESSAGE_PARSER_EXPECTED_PARAMETER_NAME \
    "parser: expected a parameter name"
#define MESSAGE_PARSER_EXPECTED_PARAMETER_COLON \
    "parser: expected ':' after the parameter name"
#define MESSAGE_PARSER_EXPECTED_PARAMETER_SEPARATOR \
    "parser: expected ',' or ')' after a parameter"
#define MESSAGE_PARSER_EXPECTED_RETURN_TYPE \
    "parser: expected ':' and a return type after the parameter list"
#define MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END \
    "parser: expected a statement or 'end'"
#define MESSAGE_PARSER_EXPECTED_THEN \
    "parser: expected 'then' after the if condition"
#define MESSAGE_PARSER_EXPECTED_RPAREN \
    "parser: expected ')' after an expression"
#define MESSAGE_PARSER_EXPECTED_TYPE "parser: expected a type"
#define MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_LPAREN \
    "parser: expected '(' in a function type"
#define MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_SEPARATOR \
    "parser: expected ',' or ')' in a function type"
#define MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_COLON \
    "parser: expected ':' before a function type's return type"
#define MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_GT \
    "parser: expected '>' after a function type"
#define MESSAGE_PARSER_EXPECTED_TYPE_PARAMETER_NAME \
    "parser: expected a type parameter name"
#define MESSAGE_PARSER_EXPECTED_TYPE_PARAMETER_SEPARATOR \
    "parser: expected ',' or '>' after a type parameter"
#define MESSAGE_PARSER_EXPECTED_TYPE_PARAMETER_GT \
    "parser: expected '>' after a type parameter list"
#define MESSAGE_PARSER_EXPECTED_TYPE_ARGUMENT \
    "parser: expected a type argument"
#define MESSAGE_PARSER_EXPECTED_TYPE_ARGUMENT_SEPARATOR \
    "parser: expected ',' or '>' after a type argument"
#define MESSAGE_PARSER_EXPECTED_TYPE_ARGUMENT_GT \
    "parser: expected '>' after a type argument list"
#define MESSAGE_PARSER_EXPECTED_UNION_ALTERNATIVE \
    "parser: expected a type alternative after '|'"
#define MESSAGE_PARSER_EXPECTED_IS_LT \
    "parser: expected '<' after 'is'"
#define MESSAGE_PARSER_EXPECTED_IS_GT \
    "parser: expected '>' after the 'is' type"
#define MESSAGE_PARSER_EXPECTED_IS_LPAREN \
    "parser: expected '(' after the 'is' type"
#define MESSAGE_PARSER_EXPECTED_IS_RPAREN \
    "parser: expected ')' after 'is<T>'"
#define MESSAGE_PARSER_EXPECTED_MEMBER_NAME \
    "parser: expected a member name after '.'"

/* Checker */
#define MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM \
    "checker: out of memory while building the program"
#define MESSAGE_CHECKER_MISSING_NAME_OR_TYPE \
    "checker: declaration is missing a name or type"
#define MESSAGE_CHECKER_TYPE_HAS_NO_FIELDS \
    "checker: type declaration has no fields"
#define MESSAGE_CHECKER_UNKNOWN_TYPE \
    "checker: unknown type '%s' for '%s'"
#define MESSAGE_CHECKER_NO_INITIALISER \
    "checker: declaration of '%s' has no initialiser"
#define MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH \
    "checker: initialiser type does not match '%s' of type %s"
#define MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME \
    "checker: out of memory while copying a name"
#define MESSAGE_CHECKER_MISSING_AST_NODE \
    "checker: missing AST node at index %zu"
#define MESSAGE_CHECKER_UNSUPPORTED_AST_NODE \
    "checker: unsupported AST node"
#define MESSAGE_CHECKER_MISSING_AST_ROOT \
    "checker: missing AST root"
#define MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING \
    "checker: out of memory while typing nodes"
#define MESSAGE_CHECKER_REDEFINES_INT32 \
    "checker: cannot redefine built-in type 'Int32'"
#define MESSAGE_CHECKER_REDEFINES_BOOL \
    "checker: cannot redefine built-in type 'Bool'"
#define MESSAGE_CHECKER_DUPLICATE_TYPE \
    "checker: duplicate type '%s'"
#define MESSAGE_CHECKER_DUPLICATE_NAME \
    "checker: duplicate declaration of '%s'"
#define MESSAGE_CHECKER_DUPLICATE_PARAMETER \
    "checker: duplicate parameter '%s'"
#define MESSAGE_CHECKER_CONSTRUCTOR_NOT_STRUCT \
    "checker: '%s' is not a struct type"
#define MESSAGE_CHECKER_CONSTRUCTOR_ARG_COUNT \
    "checker: constructor for '%s' expects %zu argument(s), got %zu"
#define MESSAGE_CHECKER_MISSING_CONSTRUCTOR_ARG \
    "checker: missing constructor argument for field '%s'"
#define MESSAGE_CHECKER_DUPLICATE_CONSTRUCTOR_ARG \
    "checker: duplicate constructor argument for field '%s'"
#define MESSAGE_CHECKER_UNKNOWN_CONSTRUCTOR_FIELD \
    "checker: constructor argument names unknown field '%s'"
#define MESSAGE_CHECKER_CONSTRUCTOR_ARG_TYPE \
    "checker: argument %zu of constructor '%s' has the wrong type"
#define MESSAGE_CHECKER_UNKNOWN_NAME \
    "checker: unknown name '%s'"
#define MESSAGE_CHECKER_UNKNOWN_CALL \
    "checker: unknown function or type '%s'"
#define MESSAGE_CHECKER_GLOBAL_IN_FUNCTION \
    "checker: cannot use program-level variable '%s' inside a function"
#define MESSAGE_CHECKER_TYPE_AS_VALUE \
    "checker: type '%s' cannot be used as a value"
#define MESSAGE_CHECKER_NOT_CALLABLE \
    "checker: '%s' is not a function"
#define MESSAGE_CHECKER_NAMED_FUNCTION_ARG \
    "checker: function call arguments must be positional"
#define MESSAGE_CHECKER_FUNCTION_ARG_COUNT \
    "checker: function '%s' expects %zu argument(s), got %zu"
#define MESSAGE_CHECKER_FUNCTION_ARG_TYPE \
    "checker: argument %zu of '%s' has the wrong type"
#define MESSAGE_CHECKER_RETURN_TYPE_MISMATCH \
    "checker: return expression of '%s' has the wrong type"
#define MESSAGE_CHECKER_MISSING_RETURN \
    "checker: function '%s' has no return expression"
#define MESSAGE_CHECKER_OPERATOR_TYPE \
    "checker: operator '%s' has invalid operand types"
#define MESSAGE_CHECKER_CONDITION_TYPE \
    "checker: condition must be Bool"
#define MESSAGE_CHECKER_BREAK_OUTSIDE_LOOP \
    "checker: 'break' outside a loop"
#define MESSAGE_CHECKER_CONTINUE_OUTSIDE_LOOP \
    "checker: 'continue' outside a loop"
#define MESSAGE_CHECKER_FUNCTION_MAY_NOT_RETURN \
    "checker: function '%s' may reach the end without returning"
#define MESSAGE_CHECKER_FIELD_TYPE \
    "checker: field '%s' has an unsupported type"
#define MESSAGE_CHECKER_UNION_FUNCTION \
    "checker: union alternative has an unsupported type"
#define MESSAGE_CHECKER_BYTE_RANGE \
    "checker: byte literal %d is outside 0..255"
#define MESSAGE_CHECKER_STORE_SIZE \
    "checker: store size must be a constant Int32 expression"
#define MESSAGE_CHECKER_STORE_INITIALIZER \
    "checker: store '%s' must be initialised with Store(bytes)"
#define MESSAGE_CHECKER_STORE_NOT_VALUE \
    "checker: store '%s' is not a value"
#define MESSAGE_CHECKER_STORE_TYPE \
    "checker: Store is only valid as a store declaration type"
#define MESSAGE_CHECKER_HANDLE_ELEMENT \
    "checker: Handle element type must be a single sized type"
#define MESSAGE_CHECKER_ALLOC_ARG_COUNT \
    "checker: alloc expects exactly one value"
#define MESSAGE_CHECKER_ALLOC_ARG_TYPE \
    "checker: alloc<%s> value has the wrong type"
#define MESSAGE_CHECKER_UNKNOWN_MEMBER \
    "checker: unknown member '%s'"
#define MESSAGE_CHECKER_MEMBER_RECEIVER \
    "checker: '%s' must be called on the right kind of value"
#define MESSAGE_CHECKER_MEMBER_ARGS \
    "checker: '%s' has the wrong number of arguments"
#define MESSAGE_CHECKER_MEMBER_TYPE_ARG \
    "checker: '%s' does not take a type argument"
#define MESSAGE_CHECKER_WRITE_TYPE \
    "checker: write value has the wrong type"
#define MESSAGE_CHECKER_UNKNOWN_STORE \
    "checker: unknown store '%s'"
#define MESSAGE_CHECKER_ALLOC_TYPE_ARG \
    "checker: alloc requires a type argument"
#define MESSAGE_CHECKER_EXPRESSION_STATEMENT \
    "checker: expression statement must be a call"
#define MESSAGE_CHECKER_REDEFINES_NIL \
    "checker: cannot redefine built-in type 'Nil'"
#define MESSAGE_CHECKER_DUPLICATE_TYPE_PARAMETER \
    "checker: duplicate type parameter '%s'"
#define MESSAGE_CHECKER_TYPE_ARGUMENT_COUNT \
    "checker: type '%s' expects %zu type argument(s), got %zu"
#define MESSAGE_CHECKER_TYPE_NOT_GENERIC \
    "checker: type '%s' takes no type arguments"
#define MESSAGE_CHECKER_MISSING_TYPE_ARGUMENTS \
    "checker: type '%s' needs type arguments"
#define MESSAGE_CHECKER_CANNOT_INFER_TYPE \
    "checker: cannot infer type parameter '%s' of '%s'"
#define MESSAGE_CHECKER_GENERIC_AS_VALUE \
    "checker: generic function '%s' must be called"
#define MESSAGE_CHECKER_IS_TYPE_IS_UNION \
    "checker: '.is<T>()' requires a single non-union type"
#define MESSAGE_CHECKER_RECURSIVE_TYPE \
    "checker: type '%s' is defined recursively"

/* Analyser */
#define MESSAGE_ANALYSER_ROOT_OUT_OF_RANGE \
    "analyser: program root index %zu is out of range"
#define MESSAGE_ANALYSER_INVALID_NODE_LINK \
    "analyser: program contains an invalid node link at index %zu"
#define MESSAGE_ANALYSER_INVALID_NAME_INDEX \
    "analyser: program contains an invalid name index at index %zu"
#define MESSAGE_ANALYSER_INVALID_TYPE_INDEX \
    "analyser: program contains an invalid type index at node %zu"
#define MESSAGE_ANALYSER_INVALID_CHILD_INDEX \
    "analyser: program contains an invalid child index at node %zu"
#define MESSAGE_ANALYSER_INVALID_SIGNATURE_INDEX \
    "analyser: program contains an invalid signature index at type %zu"
#define MESSAGE_ANALYSER_INVALID_SIGNATURE_LINK \
    "analyser: program contains an invalid signature link at signature %zu"
#define MESSAGE_ANALYSER_INVALID_TYPE_CHILD \
    "analyser: program contains an invalid type child at type %zu"
#define MESSAGE_ANALYSER_INVALID_DECLARATION \
    "analyser: struct type %zu has no declaration"
#define MESSAGE_ANALYSER_INVALID_AUXILIARY_TYPE \
    "analyser: program contains an invalid auxiliary type at node %zu"
#define MESSAGE_ANALYSER_INVALID_HANDLE_TYPE \
    "analyser: handle type %zu has no element type"
#define MESSAGE_ANALYSER_INVALID_STORE_ID \
    "analyser: node %zu has an invalid store id"

/* Generator */
#define MESSAGE_GENERATOR_UNSUPPORTED_ROOT \
    "generator: unsupported or unchecked program root"
#define MESSAGE_GENERATOR_INVALID_IDENTIFIER \
    "generator: invalid generated identifier"
#define MESSAGE_GENERATOR_UNSUPPORTED_FIELD \
    "generator: unsupported or unchecked field"
#define MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION \
    "generator: unsupported or unchecked expression"
#define MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION \
    "generator: unsupported or unchecked function declaration"
#define MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT \
    "generator: unsupported or unchecked statement"
#define MESSAGE_GENERATOR_UNSUPPORTED_TYPE \
    "generator: unsupported type in generated declaration"
#define MESSAGE_GENERATOR_INVALID_HEADER_NAME \
    "generator: invalid generated header filename"
#define MESSAGE_GENERATOR_INVALID_LINE_NAME \
    "generator: invalid generated #line filename"
#define MESSAGE_GENERATOR_OUT_OF_MEMORY \
    "generator: out of memory while generating source"

/* Runtime diagnostics embedded in generated C. */
#define MESSAGE_RUNTIME_OUT_OF_MEMORY "nippy: out of memory"
#define MESSAGE_RUNTIME_STALE_HANDLE "nippy: stale handle"
#define MESSAGE_RUNTIME_HANDLE_BOUNDS "nippy: handle is out of range"
#define MESSAGE_RUNTIME_STORE_RANGE "nippy: invalid store id"

/* Driver */
#define MESSAGE_DRIVER_COULD_NOT_OPEN_SOURCE \
    "%s: could not open source file"
#define MESSAGE_DRIVER_COULD_NOT_ALLOCATE_BUFFER \
    "could not allocate source buffer"
#define MESSAGE_DRIVER_FAILED_TO_READ \
    "%s: failed to read source file"
#define MESSAGE_DRIVER_SOURCE_TOO_LARGE \
    "%s: source is larger than %d bytes"
#define MESSAGE_DRIVER_SOURCE_CONTAINS_NUL \
    "%s: source contains a NUL byte"
#define MESSAGE_DRIVER_COULD_NOT_ALLOCATE_COMMAND \
    "could not allocate C compiler command"
#define MESSAGE_DRIVER_COULD_NOT_START_CC \
    "could not start the C compiler"
#define MESSAGE_DRIVER_CC_FAILED \
    "the C compiler failed"
#define MESSAGE_DRIVER_USAGE \
    "usage: %s <source.nip> <output-binary>"
#define MESSAGE_DRIVER_COULD_NOT_ALLOCATE_PATHS \
    "could not allocate generated file paths"
#define MESSAGE_DRIVER_COULD_NOT_WRITE_HEADER \
    "%s: could not write generated header"
#define MESSAGE_DRIVER_COULD_NOT_WRITE_SOURCE \
    "%s: could not write generated C source"

#endif
