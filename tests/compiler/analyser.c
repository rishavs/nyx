/* Analyser stage: typed-program validation, driven by hand-built malformed programs. */

#include "compile_support.h"

static TestResult analyser_numeric_rejects(const TypedNode *node,
                                           NippyType type_kind,
                                           const char *what)
{
    TypedProgram program;
    ErrorState errors;
    TypedType type = {0};
    TypedNode copy = *node;
    size_t type_index = 0;
    size_t node_index = 0;
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    type.kind = type_kind;
    type.name_index = TYPED_NO_NODE;
    type.signature_index = TYPED_NO_NODE;
    if (!typed_program_add_type(&program, &type, &type_index)) {
        return TEST_ERROR;
    }
    copy.type_index = type_index;
    copy.auxiliary_type = copy.kind == TYPED_NUMERIC_CONVERSION
                              ? type_index
                              : TYPED_NO_NODE;
    copy.name_index = TYPED_NO_NODE;
    copy.store_id = TYPED_NO_NODE;
    if (!typed_program_push(&program, &copy, &node_index) ||
        !list_push(&program.roots, &node_index)) {
        return TEST_ERROR;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_NUMERIC, node_index);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  %s did not produce its diagnostic\n", what);
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("root out of range")
{
    TypedProgram program;
    ErrorState errors;
    char expected[256];
    size_t bad_root = 999;

    typed_program_init(&program);
    error_init(&errors);
    list_push(&program.roots, &bad_root);

    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_ROOT_OUT_OF_RANGE, bad_root);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  out-of-range root did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("invalid node link")
{
    TypedProgram program;
    ErrorState errors;
    char expected[256];
    TypedNode node = {0};

    typed_program_init(&program);
    error_init(&errors);
    node.kind = TYPED_DECLARATION;
    node.left_child = 999;
    typed_program_push(&program, &node, &(size_t){0});

    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_NODE_LINK, (size_t)0);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid node link did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("invalid name index")
{
    TypedProgram program;
    ErrorState errors;
    char expected[256];
    TypedNode node = {0};

    typed_program_init(&program);
    error_init(&errors);
    node.kind = TYPED_DECLARATION;
    node.name_index = 999;
    typed_program_push(&program, &node, &(size_t){0});

    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_NAME_INDEX, (size_t)0);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid name index did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("invalid type index")
{
    TypedProgram program;
    ErrorState errors;
    TypedNode node = {0};
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    node.kind = TYPED_DECLARATION;
    node.type_index = 999;
    node.left_child = TYPED_NO_NODE;
    node.right_child = TYPED_NO_NODE;
    node.name_index = TYPED_NO_NODE;
    typed_program_push(&program, &node, &(size_t){0});

    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_TYPE_INDEX, (size_t)0);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid type index did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("invalid child index")
{
    TypedProgram program;
    ErrorState errors;
    TypedNode node = {0};
    char expected[256];
    size_t bad_child = 999;

    typed_program_init(&program);
    error_init(&errors);
    node.kind = TYPED_DECLARATION;
    node.type_index = TYPED_NO_NODE;
    node.left_child = TYPED_NO_NODE;
    node.right_child = TYPED_NO_NODE;
    node.name_index = TYPED_NO_NODE;
    node.children_start = 0;
    node.children_count = 1;
    typed_program_push(&program, &node, &(size_t){0});
    typed_program_push_child(&program, bad_child);

    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_CHILD_INDEX, (size_t)0);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid child index did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("invalid signature index")
{
    TypedProgram program;
    ErrorState errors;
    TypedType type = {0};
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    type.kind = NIPPY_TYPE_FUNCTION;
    type.name_index = TYPED_NO_NODE;
    type.signature_index = 999;
    typed_program_add_type(&program, &type, &(size_t){0});

    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_SIGNATURE_INDEX, (size_t)0);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid signature index did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("invalid signature link")
{
    TypedProgram program;
    ErrorState errors;
    TypedFunctionSignature signature = {0};
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    signature.first_parameter = 0;
    signature.parameter_count = 0;
    signature.return_type = 999;
    typed_program_add_signature(&program, &signature, &(size_t){0});

    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_SIGNATURE_LINK, (size_t)0);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid signature link did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("invalid type child")
{
    TypedProgram program;
    ErrorState errors;
    TypedType type = {0};
    size_t type_index = 0;
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    type.kind = NIPPY_TYPE_UNION;
    type.name_index = TYPED_NO_NODE;
    type.signature_index = TYPED_NO_NODE;
    type.first_child = 0;
    type.child_count = 2;
    if (!typed_program_add_type(&program, &type, &type_index)) {
        return TEST_ERROR;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_TYPE_CHILD, type_index);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid type child did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("invalid range element type")
{
    TypedProgram program;
    ErrorState errors;
    TypedType type = {0};
    size_t bool_index, range_index;
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    type.kind = NIPPY_TYPE_BOOL;
    type.name_index = TYPED_NO_NODE;
    type.signature_index = TYPED_NO_NODE;
    if (!typed_program_add_type(&program, &type, &bool_index) ||
        !typed_program_add_type_child(&program, bool_index)) return TEST_ERROR;
    type.kind = NIPPY_TYPE_RANGE;
    type.first_child = 0;
    type.child_count = 1;
    if (!typed_program_add_type(&program, &type, &range_index)) return TEST_ERROR;
    expect_message(expected, sizeof(expected), MESSAGE_ANALYSER_INVALID_TYPE_CHILD,
                   range_index);
    if (analyser_analyse(&program, &errors) || !error_contains(&errors, expected)) {
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("invalid declaration")
{
    TypedProgram program;
    ErrorState errors;
    TypedType type = {0};
    size_t type_index = 0;
    size_t name;
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    name = typed_program_copy_symbol(&program, "Point", 5);
    type.kind = NIPPY_TYPE_STRUCT;
    type.name_index = name;
    type.source_name_index = name;
    type.declaration_node = 999;
    if (!typed_program_add_type(&program, &type, &type_index)) {
        return TEST_ERROR;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_DECLARATION, type_index);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid declaration link did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("invalid auxiliary type")
{
    TypedProgram program;
    ErrorState errors;
    TypedType bool_type = {0};
    TypedNode node = {0};
    size_t bool_index = 0;
    size_t node_index = 0;
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    bool_type.kind = NIPPY_TYPE_BOOL;
    bool_type.name_index = TYPED_NO_NODE;
    bool_type.signature_index = TYPED_NO_NODE;
    if (!typed_program_add_type(&program, &bool_type, &bool_index)) {
        return TEST_ERROR;
    }
    node.kind = TYPED_IS;
    node.type_index = bool_index;
    node.auxiliary_type = 999;
    node.name_index = TYPED_NO_NODE;
    node.left_child = TYPED_NO_NODE;
    node.right_child = TYPED_NO_NODE;
    if (!typed_program_push(&program, &node, &node_index)) {
        return TEST_ERROR;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_AUXILIARY_TYPE, node_index);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid auxiliary type did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("invalid store id")
{
    TypedProgram program;
    ErrorState errors;
    TypedType store_type = {0};
    TypedNode declaration = {0};
    size_t type_index = 0;
    size_t node_index = 0;
    size_t name;
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    store_type.kind = NIPPY_TYPE_STORE;
    store_type.name_index = TYPED_NO_NODE;
    store_type.signature_index = TYPED_NO_NODE;
    if (!typed_program_add_type(&program, &store_type, &type_index)) {
        return TEST_ERROR;
    }
    name = typed_program_copy_symbol(&program, "s", 1);
    declaration.kind = TYPED_STORE_DECLARATION;
    declaration.type_index = type_index;
    declaration.store_id = 1;
    declaration.integer_value = 16;
    declaration.name_index = name;
    declaration.left_child = TYPED_NO_NODE;
    declaration.right_child = TYPED_NO_NODE;
    if (!typed_program_push(&program, &declaration, &node_index) ||
        !list_push(&program.roots, &node_index)) {
        return TEST_ERROR;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_STORE_ID, node_index);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid store id did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("invalid handle type")
{
    TypedProgram program;
    ErrorState errors;
    TypedType handle = {0};
    size_t type_index = 0;
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    handle.kind = NIPPY_TYPE_HANDLE;
    handle.name_index = TYPED_NO_NODE;
    handle.signature_index = TYPED_NO_NODE;
    handle.first_child = 0;
    handle.child_count = 0;
    if (!typed_program_add_type(&program, &handle, &type_index)) {
        return TEST_ERROR;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_HANDLE_TYPE, type_index);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid handle type did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("invalid string literal")
{
    TypedProgram program;
    ErrorState errors;
    TypedType string = {0};
    TypedNode literal = {0};
    size_t type_index = 0;
    size_t node_index = 0;
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    string.kind = NIPPY_TYPE_STRING;
    string.name_index = TYPED_NO_NODE;
    string.signature_index = TYPED_NO_NODE;
    string.fixed_length = 4;
    if (!typed_program_add_type(&program, &string, &type_index)) {
        return TEST_ERROR;
    }
    literal.kind = TYPED_STRING_LITERAL;
    literal.type_index = type_index;
    literal.name_index = TYPED_NO_NODE;
    literal.auxiliary_type = TYPED_NO_NODE;
    literal.store_id = TYPED_NO_NODE;
    literal.string_offset = 0;
    literal.string_length = 4;
    literal.left_child = TYPED_NO_NODE;
    literal.right_child = TYPED_NO_NODE;
    if (!typed_program_push(&program, &literal, &node_index) ||
        !list_push(&program.roots, &node_index)) {
        return TEST_ERROR;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_STRING_LITERAL);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid string literal did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("invalid Error shape")
{
    TypedProgram program;
    ErrorState errors;
    TypedType error = {0};
    TypedNode field = {0};
    size_t type_index = 0;
    size_t node_index = 0;
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    error.kind = NIPPY_TYPE_ERROR;
    error.name_index = TYPED_NO_NODE;
    error.signature_index = TYPED_NO_NODE;
    if (!typed_program_add_type(&program, &error, &type_index)) {
        return TEST_ERROR;
    }
    field.kind = TYPED_ERROR_FIELD;
    field.type_index = type_index;
    field.name_index = TYPED_NO_NODE;
    field.auxiliary_type = TYPED_NO_NODE;
    field.store_id = TYPED_NO_NODE;
    field.integer_value = 9;
    field.left_child = TYPED_NO_NODE;
    field.right_child = TYPED_NO_NODE;
    if (!typed_program_push(&program, &field, &node_index) ||
        !list_push(&program.roots, &node_index)) {
        return TEST_ERROR;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_ERROR, node_index);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid Error node did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("invalid integer literal")
{
    TypedNode node = {0};

    /* 300 does not fit the width of a Uint8. */
    node.kind = TYPED_INTEGER_LITERAL;
    node.integer_value_u = 300;
    node.left_child = TYPED_NO_NODE;
    node.right_child = TYPED_NO_NODE;
    return analyser_numeric_rejects(&node, NIPPY_TYPE_BYTE,
                                    "oversized integer literal");
}

TEST("invalid float literal")
{
    TypedNode node = {0};

    /* A float literal must have a float type. */
    node.kind = TYPED_FLOAT_LITERAL;
    node.float_value = 1.5;
    node.left_child = TYPED_NO_NODE;
    node.right_child = TYPED_NO_NODE;
    return analyser_numeric_rejects(&node, NIPPY_TYPE_INT32,
                                    "float literal with an integer type");
}

TEST("invalid numeric conversion")
{
    TypedNode node = {0};

    /* A conversion needs a child to convert. */
    node.kind = TYPED_NUMERIC_CONVERSION;
    node.left_child = TYPED_NO_NODE;
    node.right_child = TYPED_NO_NODE;
    return analyser_numeric_rejects(&node, NIPPY_TYPE_INT32,
                                    "conversion without a child");
}

TEST("fixed-type string literal")
{
    TypedProgram program;
    ErrorState errors;
    TypedType type = {0};
    TypedNode node = {0};
    size_t type_index = 0;
    size_t node_index = 0;

    typed_program_init(&program);
    error_init(&errors);
    /* A literal with a fixed type must be a Text; a fixed String<N> is wrong. */
    type.kind = NIPPY_TYPE_STRING;
    type.fixed_length = 8;
    type.name_index = TYPED_NO_NODE;
    type.signature_index = TYPED_NO_NODE;
    if (!typed_program_add_type(&program, &type, &type_index)) {
        return TEST_ERROR;
    }
    node.kind = TYPED_STRING_LITERAL;
    node.type_index = type_index;
    node.name_index = TYPED_NO_NODE;
    node.auxiliary_type = TYPED_NO_NODE;
    node.store_id = TYPED_NO_NODE;
    node.left_child = TYPED_NO_NODE;
    node.right_child = TYPED_NO_NODE;
    node.fixed_type = true;
    if (!typed_program_push(&program, &node, &node_index) ||
        !list_push(&program.roots, &node_index)) {
        return TEST_ERROR;
    }
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, MESSAGE_ANALYSER_INVALID_STRING_LITERAL)) {
        fprintf(stderr, "  fixed-type String literal was not rejected\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}
