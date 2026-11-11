#include "generator.h"

#include "alloc.h"
#include "messages.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool valid_header_name(const char *name)
{
    const unsigned char *cursor = (const unsigned char *)name;

    if (name == NULL || *name == '\0') {
        return false;
    }
    while (*cursor != '\0') {
        if (!isalnum(*cursor) && *cursor != '_' && *cursor != '-' && *cursor != '.') {
            return false;
        }
        cursor++;
    }
    return true;
}

/* A #line filename becomes a C string literal, so it must be printable and
   contain no quote or backslash. Unlike a header name it may contain '/'
   because source paths do. */
static bool valid_line_name(const char *name)
{
    const unsigned char *cursor = (const unsigned char *)name;

    if (name == NULL || *name == '\0') {
        return false;
    }
    while (*cursor != '\0') {
        if (*cursor == '"' || *cursor == '\\' || *cursor < 32 ||
            *cursor == 127) {
            return false;
        }
        cursor++;
    }
    return true;
}

/* The checker only emits identifier-shaped names, but the generator does not
   trust an earlier stage: an unvalidated name would be pasted into C source. */
static bool valid_identifier(const char *name)
{
    const unsigned char *cursor = (const unsigned char *)name;

    if (name == NULL || *cursor == '\0') {
        return false;
    }
    if (!isalpha(*cursor) && *cursor != '_') {
        return false;
    }
    cursor++;
    while (*cursor != '\0') {
        if (!isalnum(*cursor) && *cursor != '_') {
            return false;
        }
        cursor++;
    }
    return true;
}

/* A growable string used to compose generated C with a variable number of
   declarations and fields. */
typedef struct {
    char *text;
    size_t length;
    size_t capacity;
} TextBuffer;

static void buffer_init(TextBuffer *buffer)
{
    buffer->text = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}

static void buffer_free(TextBuffer *buffer)
{
    free(buffer->text);
    buffer->text = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}

static bool buffer_reserve(TextBuffer *buffer, size_t required)
{
    size_t capacity;
    char *grown;

    if (required <= buffer->capacity) {
        return true;
    }
    capacity = buffer->capacity == 0 ? 64 : buffer->capacity;
    while (capacity < required) {
        if (capacity > (size_t)-1 / 2) {
            return false;
        }
        capacity *= 2;
    }
    grown = nippy_realloc(buffer->text, capacity);
    if (grown == NULL) {
        return false;
    }
    buffer->text = grown;
    buffer->capacity = capacity;
    return true;
}

static bool buffer_append(TextBuffer *buffer, const char *text)
{
    size_t length = strlen(text);

    if (!buffer_reserve(buffer, buffer->length + length + 1)) {
        return false;
    }
    memcpy(buffer->text + buffer->length, text, length);
    buffer->length += length;
    buffer->text[buffer->length] = '\0';
    return true;
}

static bool buffer_appendf(TextBuffer *buffer, const char *format, ...)
{
    va_list args;
    va_list measure;
    int needed;

    va_start(args, format);
    va_copy(measure, args);
    needed = vsnprintf(NULL, 0, format, measure);
    va_end(measure);
    if (needed < 0) {
        va_end(args);
        return false;
    }
    if (!buffer_reserve(buffer, buffer->length + (size_t)needed + 1)) {
        va_end(args);
        return false;
    }
    vsnprintf(buffer->text + buffer->length, buffer->capacity - buffer->length,
              format, args);
    va_end(args);
    buffer->length += (size_t)needed;
    return true;
}

/* Build the C spelling of a resolved type. Structs use their typedef name and
   function types use a generated function-pointer typedef, so every declaration
   is a simple `type name` pair. */
static bool format_c_type(const TypedProgram *program, size_t type_index,
                          char *out, size_t out_size, ErrorState *errors)
{
    const TypedType *type = typed_program_type(program, type_index);

    if (type == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    }
    if (type->kind == NIPPY_TYPE_INT32) {
        snprintf(out, out_size, "int32_t");
        return true;
    }
    if (type->kind == NIPPY_TYPE_BOOL) {
        snprintf(out, out_size, "bool");
        return true;
    }
    if (type->kind == NIPPY_TYPE_STRUCT) {
        const char *name = typed_program_symbol_text(program, type->name_index);

        if (!valid_identifier(name)) {
            error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
            return false;
        }
        snprintf(out, out_size, "%s", name);
        return true;
    }
    if (type->kind == NIPPY_TYPE_FUNCTION) {
        snprintf(out, out_size, "nippy_fun_%zu", type->signature_index);
        return true;
    }
    error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
    return false;
}

static bool emit_expression(const TypedProgram *program, size_t node_index,
                            TextBuffer *buffer, ErrorState *errors);

static bool emit_children_expressions(const TypedProgram *program,
                                      const TypedNode *node,
                                      TextBuffer *buffer, ErrorState *errors)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&program->children,
                                                 node->children_start + i);

        if (i > 0 && !buffer_append(buffer, ", ")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!emit_expression(program, child, buffer, errors)) {
            return false;
        }
    }
    return true;
}

static const char *binary_operator_text(NippyOperator op)
{
    switch (op) {
    case NIPPY_OPERATOR_EQ:
        return "==";
    case NIPPY_OPERATOR_NE:
        return "!=";
    case NIPPY_OPERATOR_LT:
        return "<";
    case NIPPY_OPERATOR_LE:
        return "<=";
    case NIPPY_OPERATOR_GT:
        return ">";
    case NIPPY_OPERATOR_GE:
        return ">=";
    case NIPPY_OPERATOR_AND:
        return "&&";
    case NIPPY_OPERATOR_OR:
        return "||";
    default:
        return NULL;
    }
}

static bool emit_expression(const TypedProgram *program, size_t node_index,
                            TextBuffer *buffer, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    char type_name[NIPPY_IDENTIFIER_MAX + 32];

    if (node == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    if (node->kind == TYPED_INTEGER_LITERAL) {
        if (!buffer_appendf(buffer, "%d", node->integer_value)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_BOOL_LITERAL) {
        if (!buffer_append(buffer, node->integer_value ? "true" : "false")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_IDENTIFIER) {
        const char *name = typed_program_symbol_text(program, node->name_index);

        if (!valid_identifier(name)) {
            error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
            return false;
        }
        if (!buffer_append(buffer, name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_BINARY) {
        if (node->op == NIPPY_OPERATOR_ADD) {
            if (!buffer_append(buffer, "(int32_t)((uint32_t)(") ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, ") + (uint32_t)(") ||
                !emit_expression(program, node->right_child, buffer, errors) ||
                !buffer_append(buffer, "))")) {
                if (!errors->reported) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
        }
        {
            const char *op = binary_operator_text(node->op);

            if (op == NULL) {
                error_report(errors,
                             MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
                return false;
            }
            if (!buffer_append(buffer, "(") ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_appendf(buffer, " %s ", op) ||
                !emit_expression(program, node->right_child, buffer, errors) ||
                !buffer_append(buffer, ")")) {
                if (!errors->reported) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
        }
    }
    if (node->kind == TYPED_UNARY) {
        if (node->op != NIPPY_OPERATOR_NOT) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_append(buffer, "(!(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, "))")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_STRUCT_CONSTRUCTOR) {
        if (!format_c_type(program, node->type_index, type_name,
                           sizeof(type_name), errors) ||
            !buffer_appendf(buffer, "(%s){ ", type_name) ||
            !emit_children_expressions(program, node, buffer, errors) ||
            !buffer_append(buffer, " }")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_FUNCTION_CALL) {
        const char *name = typed_program_symbol_text(program, node->name_index);

        if (!valid_identifier(name)) {
            error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
            return false;
        }
        if (!buffer_appendf(buffer, "%s(", name) ||
            !emit_children_expressions(program, node, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
    return false;
}

static bool emit_type_decl(const TypedProgram *program, const TypedNode *node,
                           const char *effective_source, TextBuffer *header,
                           ErrorState *errors)
{
    const char *name = typed_program_symbol_text(program, node->name_index);
    size_t i;

    if (!valid_identifier(name)) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
        return false;
    }
    if (!buffer_appendf(header, "#line %zu \"%s\"\n", node->pos.line,
                        effective_source) ||
        !buffer_append(header, "typedef struct {\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&program->children,
                                                 node->children_start + i);
        const TypedNode *field = typed_program_node(program, child);
        const char *field_name;
        char field_type_name[NIPPY_IDENTIFIER_MAX + 32];

        if (field == NULL || field->kind != TYPED_FIELD) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_FIELD);
            return false;
        }
        if (!format_c_type(program, field->type_index, field_type_name,
                           sizeof(field_type_name), errors)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_FIELD);
            }
            return false;
        }
        field_name = typed_program_symbol_text(program, field->name_index);
        if (!valid_identifier(field_name)) {
            error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
            return false;
        }
        if (!buffer_appendf(header, "    %s %s;\n", field_type_name,
                            field_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    if (!buffer_appendf(header, "} %s;\n\n", name)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_function_typedefs(const TypedProgram *program,
                                   TextBuffer *header, ErrorState *errors)
{
    size_t i;

    if (program->signatures.length == 0) {
        return true;
    }
    for (i = 0; i < program->signatures.length; i++) {
        const TypedFunctionSignature *signature =
            typed_program_signature(program, i);
        char return_name[NIPPY_IDENTIFIER_MAX + 32];
        size_t j;

        if (signature == NULL ||
            !format_c_type(program, signature->return_type, return_name,
                           sizeof(return_name), errors)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
            }
            return false;
        }
        if (!buffer_appendf(header, "typedef %s (*nippy_fun_%zu)(",
                            return_name, i)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (signature->parameter_count == 0) {
            if (!buffer_append(header, "void")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else {
            for (j = 0; j < signature->parameter_count; j++) {
                size_t parameter_type = *(const size_t *)list_get(
                    &program->signature_parameters,
                    signature->first_parameter + j);
                char parameter_name[NIPPY_IDENTIFIER_MAX + 32];

                if (!format_c_type(program, parameter_type, parameter_name,
                                   sizeof(parameter_name), errors)) {
                    if (!errors->reported) {
                        error_report(errors,
                                     MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
                    }
                    return false;
                }
                if (j > 0 && !buffer_append(header, ", ")) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                    return false;
                }
                if (!buffer_append(header, parameter_name)) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                    return false;
                }
            }
        }
        if (!buffer_append(header, ");\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    if (!buffer_append(header, "\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool append_indent(TextBuffer *source, size_t depth)
{
    size_t i;

    for (i = 0; i < depth; i++) {
        if (!buffer_append(source, "    ")) {
            return false;
        }
    }
    return true;
}

static bool emit_statement(const TypedProgram *program, size_t node_index,
                           size_t depth, const char *effective_source,
                           TextBuffer *source, ErrorState *errors);

static bool emit_block(const TypedProgram *program, size_t block_index,
                       size_t depth, const char *effective_source,
                       TextBuffer *source, ErrorState *errors)
{
    const TypedNode *block = typed_program_node(program, block_index);
    size_t i;

    if (block == NULL || block->kind != TYPED_BLOCK) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    for (i = 0; i < block->children_count; i++) {
        size_t child = *(const size_t *)list_get(&program->children,
                                                 block->children_start + i);

        if (!emit_statement(program, child, depth, effective_source, source,
                            errors)) {
            return false;
        }
    }
    return true;
}

static bool emit_statement(const TypedProgram *program, size_t node_index,
                           size_t depth, const char *effective_source,
                           TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);

    if (node == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    if (!buffer_appendf(source, "#line %zu \"%s\"\n", node->pos.line,
                        effective_source)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }

    if (node->kind == TYPED_RETURN) {
        if (!append_indent(source, depth) ||
            !buffer_append(source, "return ") ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, ";\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_BREAK || node->kind == TYPED_CONTINUE) {
        if (!append_indent(source, depth) ||
            !buffer_append(source,
                           node->kind == TYPED_BREAK ? "break;\n"
                                                     : "continue;\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_IF) {
        size_t then_block;
        size_t else_block = TYPED_NO_NODE;

        if (node->children_count == 0) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
            return false;
        }
        then_block = *(const size_t *)list_get(&program->children,
                                               node->children_start);
        if (node->children_count > 1) {
            else_block = *(const size_t *)list_get(&program->children,
                                                   node->children_start + 1);
        }
        if (!append_indent(source, depth) ||
            !buffer_append(source, "if (") ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, ") {\n") ||
            !emit_block(program, then_block, depth + 1, effective_source,
                        source, errors)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        if (else_block != TYPED_NO_NODE) {
            if (!append_indent(source, depth) ||
                !buffer_append(source, "} else {\n") ||
                !emit_block(program, else_block, depth + 1, effective_source,
                            source, errors)) {
                if (!errors->reported) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
        }
        if (!append_indent(source, depth) || !buffer_append(source, "}\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_WHILE) {
        if (!append_indent(source, depth) ||
            !buffer_append(source, "while (") ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, ") {\n") ||
            !emit_block(program, node->right_child, depth + 1,
                        effective_source, source, errors) ||
            !append_indent(source, depth) ||
            !buffer_append(source, "}\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_BLOCK) {
        return emit_block(program, node_index, depth, effective_source,
                          source, errors);
    }

    error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
    return false;
}

static bool emit_function(const TypedProgram *program, const TypedNode *node,
                          const char *effective_source, TextBuffer *source,
                          ErrorState *errors)
{
    const char *name = typed_program_symbol_text(program, node->name_index);
    const TypedType *type = typed_program_type(program, node->type_index);
    const TypedFunctionSignature *signature;
    char return_name[NIPPY_IDENTIFIER_MAX + 32];
    size_t i;

    if (!valid_identifier(name) || type == NULL ||
        type->kind != NIPPY_TYPE_FUNCTION) {
        error_report(errors, valid_identifier(name)
                                 ? MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION
                                 : MESSAGE_GENERATOR_INVALID_IDENTIFIER);
        return false;
    }
    signature = typed_program_signature(program, type->signature_index);
    if (signature == NULL ||
        !format_c_type(program, signature->return_type, return_name,
                       sizeof(return_name), errors)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION);
        }
        return false;
    }

    if (!buffer_appendf(source, "#line %zu \"%s\"\n", node->pos.line,
                        effective_source) ||
        !buffer_appendf(source, "%s %s(", return_name, name)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }

    if (node->children_count == 0) {
        if (!buffer_append(source, "void")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    } else {
        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(&program->children,
                                                     node->children_start + i);
            const TypedNode *parameter = typed_program_node(program, child);
            const char *parameter_name;
            char parameter_type[NIPPY_IDENTIFIER_MAX + 32];

            if (parameter == NULL || parameter->kind != TYPED_PARAMETER ||
                !format_c_type(program, parameter->type_index, parameter_type,
                               sizeof(parameter_type), errors)) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION);
                }
                return false;
            }
            parameter_name = typed_program_symbol_text(program,
                                                       parameter->name_index);
            if (!valid_identifier(parameter_name)) {
                error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
                return false;
            }
            if (i > 0 && !buffer_append(source, ", ")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            if (!buffer_appendf(source, "%s %s", parameter_type,
                                parameter_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
    }

    if (!buffer_append(source, ")\n{\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&program->children,
                                                 node->children_start + i);
        const TypedNode *parameter = typed_program_node(program, child);
        const char *parameter_name = typed_program_symbol_text(
            program, parameter->name_index);

        if (!buffer_appendf(source, "    (void)%s;\n", parameter_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }

    if (!emit_block(program, node->left_child, 1, effective_source, source,
                    errors) ||
        !buffer_append(source, "}\n\n")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_var_to_main(const TypedProgram *program, const TypedNode *node,
                             const char *effective_source, TextBuffer *source,
                             ErrorState *errors)
{
    const char *name = typed_program_symbol_text(program, node->name_index);
    char type_name[NIPPY_IDENTIFIER_MAX + 32];

    if (!valid_identifier(name) ||
        !format_c_type(program, node->type_index, type_name,
                       sizeof(type_name), errors)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_ROOT);
        }
        return false;
    }
    if (!buffer_appendf(source, "#line %zu \"%s\"\n", node->pos.line,
                        effective_source) ||
        !buffer_appendf(source, "    %s %s = ", type_name, name) ||
        !emit_expression(program, node->left_child, source, errors) ||
        !buffer_append(source, ";\n") ||
        !buffer_appendf(source, "    (void)%s;\n", name)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool append_print_program(const TypedProgram *program,
                                 const TypedNode *var,
                                 const char *effective_source,
                                 TextBuffer *source, ErrorState *errors)
{
    const char *name = typed_program_symbol_text(program, var->name_index);

    if (!valid_identifier(name)) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
        return false;
    }

    if (!buffer_append(source, "int32_t nippy_program_value(void)\n{\n") ||
        !buffer_appendf(source, "#line %zu \"%s\"\n", var->pos.line,
                        effective_source) ||
        !buffer_appendf(source, "    int32_t %s = ", name) ||
        !emit_expression(program, var->left_child, source, errors) ||
        !buffer_append(source, ";\n") ||
        !buffer_appendf(source, "    return %s;\n", name) ||
        !buffer_append(source, "}\n\nint main(void)\n{\n") ||
        !buffer_append(source,
                       "    printf(\"%d\\n\", (int)nippy_program_value());\n") ||
        !buffer_append(source, "    return 0;\n}\n")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

bool generator_generate(const TypedProgram *program, const char *header_name,
                        const char *source_name, char **c_source,
                        char **h_source, ErrorState *errors)
{
    const char *effective_header = header_name == NULL ? "nippy.h" : header_name;
    const char *effective_source = source_name == NULL ? "nippy.nip" : source_name;
    TextBuffer header;
    TextBuffer source;
    size_t var_count = 0;
    const TypedNode *single_var = NULL;
    bool print_program = false;
    size_t i;
    bool ok = false;

    *c_source = NULL;
    *h_source = NULL;

    if (!valid_header_name(effective_header)) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_HEADER_NAME);
        return false;
    }
    if (!valid_line_name(effective_source)) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_LINE_NAME);
        return false;
    }

    buffer_init(&header);
    buffer_init(&source);

    if (!buffer_append(&header,
                       "#ifndef NIPPY_GENERATED_H\n"
                       "#define NIPPY_GENERATED_H\n\n"
                       "#include <stdbool.h>\n"
                       "#include <stdint.h>\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }

    for (i = 0; i < program->roots.length; i++) {
        size_t root = *(const size_t *)list_get(&program->roots, i);
        const TypedNode *node = typed_program_node(program, root);

        if (node == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_ROOT);
            goto done;
        }
        if (node->kind == TYPED_TYPE_DECLARATION) {
            if (!emit_type_decl(program, node, effective_source, &header,
                                errors)) {
                goto done;
            }
        } else if (node->kind == TYPED_DECLARATION) {
            var_count++;
            single_var = node;
        } else if (node->kind != TYPED_FUNCTION_DECLARATION) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_ROOT);
            goto done;
        }
    }

    if (!emit_function_typedefs(program, &header, errors)) {
        goto done;
    }

    if (var_count == 1 && typed_program_type(program, single_var->type_index) !=
                              NULL &&
        typed_program_type(program, single_var->type_index)->kind ==
            NIPPY_TYPE_INT32) {
        print_program = true;
    }

    if (print_program &&
        !buffer_append(&header, "int32_t nippy_program_value(void);\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }
    if (!buffer_append(&header, "#endif\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }

    if (!buffer_appendf(&source, "#include \"%s\"\n\n", effective_header)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }
    if (print_program && !buffer_append(&source, "#include <stdio.h>\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }

    for (i = 0; i < program->roots.length; i++) {
        size_t root = *(const size_t *)list_get(&program->roots, i);
        const TypedNode *node = typed_program_node(program, root);

        if (node->kind == TYPED_FUNCTION_DECLARATION &&
            !emit_function(program, node, effective_source, &source, errors)) {
            goto done;
        }
    }

    if (print_program) {
        if (!append_print_program(program, single_var, effective_source,
                                  &source, errors)) {
            goto done;
        }
    } else {
        if (!buffer_append(&source, "int main(void)\n{\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            goto done;
        }
        for (i = 0; i < program->roots.length; i++) {
            size_t root = *(const size_t *)list_get(&program->roots, i);
            const TypedNode *node = typed_program_node(program, root);

            if (node->kind == TYPED_DECLARATION &&
                !emit_var_to_main(program, node, effective_source, &source,
                                  errors)) {
                goto done;
            }
        }
        if (!buffer_append(&source, "    return 0;\n}\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            goto done;
        }
    }

    *h_source = header.text;
    header.text = NULL;
    *c_source = source.text;
    source.text = NULL;
    ok = true;

done:
    buffer_free(&header);
    buffer_free(&source);
    return ok;
}
