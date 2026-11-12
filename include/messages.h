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
#define MESSAGE_LEXER_UNTERMINATED_STRING \
    "lexer: unterminated string literal"
#define MESSAGE_LEXER_STRING_NEWLINE \
    "lexer: newline in string literal"
#define MESSAGE_LEXER_INVALID_ESCAPE \
    "lexer: invalid escape sequence '\\%c'"

/* Compiler */
#define MESSAGE_COMPILER_SOURCE_IS_NULL "compiler: source string is null"
#define MESSAGE_COMPILER_ERROR_CAPACITY \
    "compiler: Error %s capacity must be between 1 and %d"
#define MESSAGE_COMPILER_STRING_CAPACITY \
    "compiler: maximum string capacity must be between 1 and %d"
#define MESSAGE_COMPILER_PRINT_DEPTH \
    "compiler: max_print_depth must be in 1..%d"
#define MESSAGE_COMPILER_LIST_CAPACITY \
    "compiler: max_list_capacity must be between 1 and %d"
#define MESSAGE_COMPILER_SOURCE_TOO_LARGE \
    "compiler: source exceeds the configured limit of %d bytes"

/* Parser */
#define MESSAGE_PARSER_EXPECTED_VAR_TYPE_OR_FUN \
    "parser: expected 'global', 'var', 'type', or 'fun' to start a declaration"
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
#define MESSAGE_PARSER_EXPECTED_IN \
    "parser: expected 'in' after for bindings"
#define MESSAGE_PARSER_EXPECTED_DO \
    "parser: expected 'do' after for iterable"
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
#define MESSAGE_PARSER_MATCH_HEAD \
    "parser: match scrutinee must be a variable"
#define MESSAGE_PARSER_EXPECTED_WITH \
    "parser: expected 'with' after match scrutinee"
#define MESSAGE_PARSER_EXPECTED_ELSE \
    "parser: expression if needs an else branch"
#define MESSAGE_PARSER_EXPECTED_MEMBER_NAME \
    "parser: expected a member name after '.'"
#define MESSAGE_PARSER_EXPECTED_STRING_LENGTH \
    "parser: expected an integer length in String<N>"
#define MESSAGE_PARSER_EXPECTED_STRING_GT \
    "parser: expected '>' after String<N>"
#define MESSAGE_PARSER_LIST_CAPACITY \
    "parser: List<T> is dynamic; use Array<T,N> for fixed capacity"
#define MESSAGE_PARSER_EXPECTED_ARRAY_CAPACITY \
    "parser: expected Array element and capacity"
#define MESSAGE_PARSER_EXPECTED_ARRAY_GT \
    "parser: expected '>' after Array capacity"
#define MESSAGE_CHECKER_ARRAY_ARGS \
    "checker: Array needs an element type and a capacity"
#define MESSAGE_CHECKER_ARRAY_ELEMENT \
    "checker: Array element must be a single item"
#define MESSAGE_CHECKER_ARRAY_CAPACITY \
    "checker: Array capacity is outside the configured maximum"
#define MESSAGE_CHECKER_REDEFINES_ARRAY \
    "checker: cannot redefine the built-in type 'Array'"
#define MESSAGE_CHECKER_HANDLE_FIXED \
    "checker: Handle requires a fixed type; " \
    "allocator-backed types use their dedicated API"
#define MESSAGE_CHECKER_COLLECTION_ELEMENT \
    "checker: collection elements must be single items"
#define MESSAGE_PARSER_EXPECTED_LIST_GT \
    "parser: expected '>' after a list capacity"
#define MESSAGE_PARSER_EXPECTED_LIST_RBRACKET \
    "parser: expected ']' after a list literal"
#define MESSAGE_PARSER_EXPECTED_RBRACKET \
    "parser: expected ']' after an index"
#define MESSAGE_PARSER_EXPECTED_LIST_SEPARATOR \
    "parser: expected ',' or ']' in a list literal"

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
#define MESSAGE_CHECKER_LOCAL_STORE \
    "checker: Store declarations must be program-level"
#define MESSAGE_CHECKER_UNKNOWN_FIELD \
    "checker: type has no field '%s'"
#define MESSAGE_CHECKER_FIELD_LVALUE \
    "checker: field assignment requires a field of a variable"
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
#define MESSAGE_CHECKER_GLOBAL_DECLARATION \
    "checker: global '%s' must be declared with type Store"
#define MESSAGE_CHECKER_TYPE_AS_VALUE \
    "checker: type '%s' cannot be used as a value"
#define MESSAGE_CHECKER_ASSIGN_TARGET \
    "checker: '%s' is not an assignable variable"
#define MESSAGE_CHECKER_ASSIGN_TYPE \
    "checker: cannot assign a value of the wrong type to '%s'"
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
#define MESSAGE_CHECKER_IF_EXPR_MISMATCH \
    "checker: if expression branches must have the same type"
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
#define MESSAGE_CHECKER_STRING_CAPACITY \
    "checker: String capacity must be between 0 and %d"
#define MESSAGE_CHECKER_STRING_LITERAL_CONTEXT \
    "checker: string literal needs a String<N> context"
#define MESSAGE_CHECKER_STRING_TOO_LONG \
    "checker: string literal of %zu bytes does not fit String<%d>"
#define MESSAGE_CHECKER_STRING_COMPARE_CONTEXT \
    "checker: string comparison needs a fixed string type"
#define MESSAGE_CHECKER_STRING_AMBIGUOUS \
    "checker: string literal fits more than one String<N> alternative"
#define MESSAGE_CHECKER_STRING_INDEX \
    "checker: 'byte_at' expects an Int32 byte index"
#define MESSAGE_CHECKER_STRING_SLICE_TYPE \
    "checker: 'slice' expects Int32 start and length"
#define MESSAGE_CHECKER_CONCAT_TYPE \
    "checker: 'concat' expects dynamic String arguments"
#define MESSAGE_CHECKER_COPY_TYPE \
    "checker: 'copy' expects a dynamic String"
#define MESSAGE_CHECKER_STORE_TEXT_TYPE \
    "checker: 'text' expects a fixed String<N>"
#define MESSAGE_CHECKER_LIST_ARGS \
    "checker: List needs one element type and an optional capacity"
#define MESSAGE_CHECKER_LIST_LITERAL_CONTEXT \
    "checker: a list literal needs a fixed List<T,N> context"
#define MESSAGE_CHECKER_LIST_LITERAL_LENGTH \
    "checker: list literal has %zu elements but the fixed list capacity is %d"
#define MESSAGE_CHECKER_LIST_INDEX \
    "checker: list index must be Int32"
#define MESSAGE_CHECKER_LIST_VALUE \
    "checker: list value has the wrong element type"
#define MESSAGE_CHECKER_LIST_INFER \
    "checker: cannot infer one element type for this list literal"
#define MESSAGE_CHECKER_LIST_TYPE_ARG \
    "checker: 'list' needs one element type"
#define MESSAGE_CHECKER_LIST_CREATE_ARGS \
    "checker: 'list' expects one Int32 capacity"
#define MESSAGE_CHECKER_LIST_CAPACITY_ARG \
    "checker: 'list' capacity must be Int32"
#define MESSAGE_CHECKER_RECORD_ARGS \
    "checker: Record needs a key type, a value type, and a capacity"
#define MESSAGE_CHECKER_TABLE_ARGS \
    "checker: Table needs a key type and a value type"
#define MESSAGE_CHECKER_MAP_KEY \
    "checker: map keys must be fixed String<N> values"
#define MESSAGE_CHECKER_MAP_VALUE \
    "checker: map values must be single items"
#define MESSAGE_CHECKER_RECORD_CAPACITY \
    "checker: Record capacity is outside the configured maximum"
#define MESSAGE_CHECKER_RECORD_LITERAL_CONTEXT \
    "checker: Record literal needs a Record context"
#define MESSAGE_CHECKER_RECORD_DUPLICATE_KEY \
    "checker: duplicate Record literal key"
#define MESSAGE_CHECKER_REDEFINES_RECORD \
    "checker: cannot redefine built-in type 'Record'"
#define MESSAGE_CHECKER_REDEFINES_TABLE \
    "checker: cannot redefine built-in type 'Table'"
#define MESSAGE_PARSER_EXPECTED_IS \
    "parser: expected 'is' after typeof expression"
#define MESSAGE_PARSER_EXPECTED_COLON \
    "parser: expected ':' after Record key"
#define MESSAGE_PARSER_EXPECTED_RBRACE \
    "parser: expected '}' after Record literal"
#define MESSAGE_PARSER_EXPECTED_RECORD_CAPACITY \
    "parser: expected Record capacity"
#define MESSAGE_PARSER_EXPECTED_RECORD_GT \
    "parser: expected '>' after Record capacity"
#define MESSAGE_PARSER_EXPECTED_TABLE_GT \
    "parser: expected '>' after Table value type"
#define MESSAGE_RUNTIME_RECORD_FULL \
    "nippy: Record is at capacity"
#define MESSAGE_RUNTIME_MAP_MISSING \
    "nippy: key is not present"
#define MESSAGE_CHECKER_PRINT_STATEMENT \
    "checker: print is a statement and cannot be used as a value"
#define MESSAGE_CHECKER_PRINT_NAMED \
    "checker: print arguments cannot be named"
#define MESSAGE_CHECKER_PRINT_RESERVED \
    "checker: 'print' is a reserved builtin"
#define MESSAGE_CHECKER_FOR_ITERABLE \
    "checker: for loop requires a List, String, or Slice"
#define MESSAGE_CHECKER_LIST_LVALUE \
    "checker: list mutation requires a list variable"
#define MESSAGE_CHECKER_SLICE_DYNAMIC \
    "checker: slice requires a dynamic List<T>"
#define MESSAGE_CHECKER_SLICE_ARGS \
    "checker: Slice needs one element type"
#define MESSAGE_CHECKER_SLICE_ELEMENT \
    "checker: Slice element type must be a concrete-sized value"
#define MESSAGE_CHECKER_SLICE_READONLY \
    "checker: Slice<T> is read-only"
#define MESSAGE_CHECKER_BUILDER_CAPACITY \
    "checker: 'builder' expects an Int32 capacity"
#define MESSAGE_CHECKER_BUILDER_APPEND_TYPE \
    "checker: 'append' expects a dynamic String"
#define MESSAGE_CHECKER_BUILDER_BYTE_TYPE \
    "checker: 'append_byte' expects a Byte"
#define MESSAGE_CHECKER_STRING_ARGS \
    "checker: String needs a capacity; use String<N> or Text"
#define MESSAGE_CHECKER_REDEFINES_STRING \
    "checker: cannot redefine built-in type 'String'"
#define MESSAGE_CHECKER_REDEFINES_TEXT \
    "checker: cannot redefine built-in type 'Text'"
#define MESSAGE_CHECKER_REDEFINES_BUILDER \
    "checker: cannot redefine built-in type 'TextBuilder'"
#define MESSAGE_CHECKER_REDEFINES_LIST \
    "checker: cannot redefine built-in type 'List'"
#define MESSAGE_CHECKER_REDEFINES_SLICE \
    "checker: cannot redefine built-in type 'Slice'"
#define MESSAGE_CHECKER_REDEFINES_ERROR \
    "checker: cannot redefine built-in type 'Error'"
#define MESSAGE_CHECKER_ERROR_ARGS \
    "checker: Error takes header, description, filename, line, col, pos"
#define MESSAGE_CHECKER_ERROR_THROW_ARGS \
    "checker: Error.throw needs a header and a description"
#define MESSAGE_CHECKER_ERROR_ARG_TYPE \
    "checker: Error argument %zu has the wrong type"
#define MESSAGE_CHECKER_STORE_SIZE \
    "checker: store size must be a constant Int32 expression"
#define MESSAGE_CHECKER_STORE_INITIALIZER \
    "checker: store '%s' must be initialised with Store(bytes)"
#define MESSAGE_CHECKER_STORE_NOT_VALUE \
    "checker: store '%s' is not a value"
#define MESSAGE_CHECKER_STORE_TYPE \
    "checker: Store is only valid as a store declaration type"
#define MESSAGE_CHECKER_HANDLE_ELEMENT \
    "checker: Handle needs exactly one element type"
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
    "checker: a type test requires a single non-union type"
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
#define MESSAGE_ANALYSER_INVALID_STRING_LITERAL \
    "analyser: string literal bytes are out of range"
#define MESSAGE_ANALYSER_INVALID_ERROR \
    "analyser: node %zu has an invalid Error shape"

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
#define MESSAGE_RUNTIME_STRING_BOUNDS "nippy: string index out of bounds"
#define MESSAGE_RUNTIME_LIST_BOUNDS "nippy: list index out of bounds"
#define MESSAGE_RUNTIME_FOR_MUTATED \
    "nippy: iterated collection changed length during for loop"

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
