/* Generator stage defensive tests: typed programs the generator must refuse. */

#include "compile_support.h"

static size_t push_int32_type(TypedProgram *program)
{
    TypedType type = {0};
    size_t index = 0;

    type.kind = NIPPY_TYPE_INT32;
    type.name_index = TYPED_NO_NODE;
    type.signature_index = TYPED_NO_NODE;
    typed_program_add_type(program, &type, &index);
    return index;
}

static void make_int_var_program(TypedProgram *program)
{
    TypedNode literal = {0};
    TypedNode var = {0};
    size_t literal_index = 0;
    size_t var_index = 0;
    size_t name;
    size_t int32_type;

    typed_program_init(program);
    name = typed_program_copy_symbol(program, "x", 1);
    int32_type = push_int32_type(program);

    literal.kind = TYPED_INTEGER_LITERAL;
    literal.type_index = int32_type;
    literal.integer_value = 13;
    typed_program_push(program, &literal, &literal_index);

    var.kind = TYPED_DECLARATION;
    var.type_index = int32_type;
    var.name_index = name;
    var.left_child = literal_index;
    typed_program_push(program, &var, &var_index);
    list_push(&program->roots, &var_index);
}

TEST("unsupported root")
{
    TypedProgram program;
    ErrorState errors;
    TypedNode node = {0};
    char *c_source = NULL;
    char *h_source = NULL;

    typed_program_init(&program);
    error_init(&errors);
    node.kind = TYPED_FIELD;
    typed_program_push(&program, &node, &(size_t){0});
    list_push(&program.roots, &(size_t){0});

    if (generator_generate(&program, "ok.h", "ok.nip", &c_source, &h_source,
                           &errors) ||
        !error_contains(&errors, MESSAGE_GENERATOR_UNSUPPORTED_ROOT)) {
        fprintf(stderr, "  unsupported root did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("unsupported field")
{
    TypedProgram program;
    ErrorState errors;
    TypedNode field = {0};
    TypedNode type_decl = {0};
    TypedType struct_type = {0};
    size_t field_index = 0;
    size_t decl_index = 0;
    size_t struct_type_index = 0;
    size_t name;
    size_t int32_type;
    char *c_source = NULL;
    char *h_source = NULL;

    typed_program_init(&program);
    error_init(&errors);
    name = typed_program_copy_symbol(&program, "Point", 5);
    int32_type = push_int32_type(&program);

    field.kind = TYPED_INTEGER_LITERAL;
    field.type_index = int32_type;
    typed_program_push(&program, &field, &field_index);
    typed_program_push_child(&program, field_index);

    type_decl.kind = TYPED_TYPE_DECLARATION;
    type_decl.name_index = name;
    type_decl.children_start = 0;
    type_decl.children_count = 1;
    typed_program_push(&program, &type_decl, &decl_index);
    list_push(&program.roots, &decl_index);
    struct_type.kind = NIPPY_TYPE_STRUCT;
    struct_type.name_index = name;
    struct_type.declaration_node = decl_index;
    struct_type.source_name_index = name;
    typed_program_add_type(&program, &struct_type, &struct_type_index);

    if (generator_generate(&program, "ok.h", "ok.nip", &c_source, &h_source,
                           &errors) ||
        !error_contains(&errors, MESSAGE_GENERATOR_UNSUPPORTED_FIELD)) {
        fprintf(stderr, "  unsupported field did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("invalid identifier")
{
    TypedProgram program;
    ErrorState errors;
    TypedNode literal = {0};
    TypedNode var = {0};
    size_t literal_index = 0;
    size_t var_index = 0;
    size_t name;
    size_t int32_type;
    char *c_source = NULL;
    char *h_source = NULL;

    typed_program_init(&program);
    error_init(&errors);
    name = typed_program_copy_symbol(&program, "123", 3);
    int32_type = push_int32_type(&program);

    literal.kind = TYPED_INTEGER_LITERAL;
    literal.type_index = int32_type;
    literal.integer_value = 13;
    typed_program_push(&program, &literal, &literal_index);

    var.kind = TYPED_DECLARATION;
    var.type_index = int32_type;
    var.name_index = name;
    var.left_child = literal_index;
    typed_program_push(&program, &var, &var_index);
    list_push(&program.roots, &var_index);

    if (generator_generate(&program, "ok.h", "ok.nip", &c_source, &h_source,
                           &errors) ||
        !error_contains(&errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER)) {
        fprintf(stderr, "  invalid identifier did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("invalid header name")
{
    TypedProgram program;
    ErrorState errors;
    char *c_source = NULL;
    char *h_source = NULL;

    make_int_var_program(&program);
    error_init(&errors);

    if (generator_generate(&program, "bad name", "ok.nip", &c_source, &h_source,
                           &errors) ||
        !error_contains(&errors, MESSAGE_GENERATOR_INVALID_HEADER_NAME)) {
        fprintf(stderr, "  invalid header name did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("invalid line name")
{
    TypedProgram program;
    ErrorState errors;
    char *c_source = NULL;
    char *h_source = NULL;

    make_int_var_program(&program);
    error_init(&errors);

    if (generator_generate(&program, "ok.h", "bad\"name", &c_source, &h_source,
                           &errors) ||
        !error_contains(&errors, MESSAGE_GENERATOR_INVALID_LINE_NAME)) {
        fprintf(stderr, "  invalid #line name did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("unsupported expression")
{
    TypedProgram program;
    ErrorState errors;
    TypedNode bad = {0};
    TypedNode var = {0};
    size_t bad_index = 0;
    size_t var_index = 0;
    size_t name;
    size_t int32_type;
    char *c_source = NULL;
    char *h_source = NULL;

    typed_program_init(&program);
    error_init(&errors);
    name = typed_program_copy_symbol(&program, "x", 1);
    int32_type = push_int32_type(&program);

    bad.kind = TYPED_FIELD;
    bad.type_index = int32_type;
    typed_program_push(&program, &bad, &bad_index);

    var.kind = TYPED_DECLARATION;
    var.type_index = int32_type;
    var.name_index = name;
    var.left_child = bad_index;
    typed_program_push(&program, &var, &var_index);
    list_push(&program.roots, &var_index);

    if (generator_generate(&program, "ok.h", "ok.nip", &c_source, &h_source,
                           &errors) ||
        !error_contains(&errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION)) {
        fprintf(stderr, "  unsupported expression did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("unsupported function")
{
    TypedProgram program;
    ErrorState errors;
    TypedNode function = {0};
    size_t function_index = 0;
    size_t name;
    char *c_source = NULL;
    char *h_source = NULL;

    typed_program_init(&program);
    error_init(&errors);
    name = typed_program_copy_symbol(&program, "f", 1);

    function.kind = TYPED_FUNCTION_DECLARATION;
    function.type_index = TYPED_NO_NODE;
    function.name_index = name;
    typed_program_push(&program, &function, &function_index);
    list_push(&program.roots, &function_index);

    if (generator_generate(&program, "ok.h", "ok.nip", &c_source, &h_source,
                           &errors) ||
        !error_contains(&errors, MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION)) {
        fprintf(stderr, "  unsupported function did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("unsupported type")
{
    TypedProgram program;
    ErrorState errors;
    TypedNode literal = {0};
    TypedNode var = {0};
    size_t literal_index = 0;
    size_t var_index = 0;
    size_t name;
    char *c_source = NULL;
    char *h_source = NULL;

    typed_program_init(&program);
    error_init(&errors);
    name = typed_program_copy_symbol(&program, "x", 1);

    literal.kind = TYPED_INTEGER_LITERAL;
    literal.type_index = 0;
    typed_program_push(&program, &literal, &literal_index);

    var.kind = TYPED_DECLARATION;
    var.type_index = 0;
    var.name_index = name;
    var.left_child = literal_index;
    typed_program_push(&program, &var, &var_index);
    list_push(&program.roots, &var_index);

    if (generator_generate(&program, "ok.h", "ok.nip", &c_source, &h_source,
                           &errors) ||
        !error_contains(&errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE)) {
        fprintf(stderr, "  unsupported type did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("unsupported statement")
{
    TypedProgram program;
    ErrorState errors;
    TypedNode bad = {0};
    TypedNode block = {0};
    TypedNode function = {0};
    TypedFunctionSignature signature = {0};
    TypedType function_type = {0};
    size_t bad_index = 0;
    size_t block_index = 0;
    size_t function_index = 0;
    size_t signature_index = 0;
    size_t function_type_index = 0;
    size_t int32_type;
    size_t name;
    char *c_source = NULL;
    char *h_source = NULL;

    typed_program_init(&program);
    error_init(&errors);
    int32_type = push_int32_type(&program);
    name = typed_program_copy_symbol(&program, "f", 1);

    signature.first_parameter = 0;
    signature.parameter_count = 0;
    signature.return_type = int32_type;
    typed_program_add_signature(&program, &signature, &signature_index);
    function_type.kind = NIPPY_TYPE_FUNCTION;
    function_type.name_index = TYPED_NO_NODE;
    function_type.signature_index = signature_index;
    typed_program_add_type(&program, &function_type, &function_type_index);

    bad.kind = TYPED_FIELD;
    bad.type_index = int32_type;
    bad.left_child = TYPED_NO_NODE;
    bad.right_child = TYPED_NO_NODE;
    bad.name_index = name;
    typed_program_push(&program, &bad, &bad_index);

    block.kind = TYPED_BLOCK;
    block.type_index = TYPED_NO_NODE;
    block.children_start = 0;
    block.children_count = 1;
    block.left_child = TYPED_NO_NODE;
    block.right_child = TYPED_NO_NODE;
    typed_program_push(&program, &block, &block_index);
    typed_program_push_child(&program, bad_index);

    function.kind = TYPED_FUNCTION_DECLARATION;
    function.type_index = function_type_index;
    function.name_index = name;
    function.left_child = block_index;
    function.right_child = TYPED_NO_NODE;
    function.children_start = 0;
    function.children_count = 0;
    typed_program_push(&program, &function, &function_index);
    list_push(&program.roots, &function_index);

    if (generator_generate(&program, "ok.h", "ok.nip", &c_source, &h_source,
                           &errors) ||
        !error_contains(&errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT)) {
        fprintf(stderr, "  unsupported statement did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("store: runtime diagnostics emitted")
{
    CompilerConfig config = {.header_name = "test.h",
                             .source_name = "test.nip"};
    CompilerResult result = compiler_compile(
        "var s: Store = Store(16)\nvar p: Handle<Int32> = s.alloc<Int32>(1)\n",
        &config);
    int ok;

    if (!result.succeeded) {
        compiler_result_free(&result);
        return TEST_FAIL;
    }
    ok = result.c_source != NULL &&
         strstr(result.c_source, MESSAGE_RUNTIME_OUT_OF_MEMORY) != NULL &&
         strstr(result.c_source, MESSAGE_RUNTIME_STALE_HANDLE) != NULL &&
         strstr(result.c_source, MESSAGE_RUNTIME_HANDLE_BOUNDS) != NULL &&
         strstr(result.c_source, MESSAGE_RUNTIME_STORE_RANGE) != NULL;
    compiler_result_free(&result);
    if (!ok) {
        fprintf(stderr, "  generated store runtime lacks a diagnostic\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}
