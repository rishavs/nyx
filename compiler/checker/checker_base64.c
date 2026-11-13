#include "checker_internal.h"


/* base64("...") decodes a string literal at compile time into the static Text
   of its bytes. The decoder is strict, so one string has one spelling and a
   mistake is caught where it is written: the standard RFC 4648 alphabet, '='
   padding that completes the final group, and zero bits in the unused tail.
   Nothing else, including whitespace, is accepted. */

typedef enum {
    BASE64_OK,
    BASE64_CHARACTER,
    BASE64_PADDING,
    BASE64_CANONICAL,
    BASE64_MEMORY
} Base64Problem;


static int base64_value(unsigned char character)
{
    if (character >= 'A' && character <= 'Z') {
        return character - 'A';
    }
    if (character >= 'a' && character <= 'z') {
        return character - 'a' + 26;
    }
    if (character >= '0' && character <= '9') {
        return character - '0' + 52;
    }
    if (character == '+') {
        return 62;
    }
    if (character == '/') {
        return 63;
    }
    return -1;
}


/* Decodes `length` bytes of `text` into `out`. On failure *at is the byte
   offset of the offending character, or the length for a bad length. */
static Base64Problem decode_base64(const List *source, size_t offset,
                                   size_t length, List *out, size_t *at)
{
    size_t i;

    if (length % 4 != 0) {
        *at = length;
        return BASE64_PADDING;
    }
    for (i = 0; i < length; i += 4) {
        int value[4] = {0, 0, 0, 0};
        size_t padding = 0;
        size_t k;
        unsigned char bytes[3];
        size_t produced;

        for (k = 0; k < 4; k++) {
            const unsigned char *character =
                list_get(source, offset + i + k);

            if (character == NULL) {
                *at = i + k;
                return BASE64_CHARACTER;
            }
            if (*character == '=') {
                /* Padding is only the last one or two characters. */
                if (i + 4 != length || k < 2) {
                    *at = i + k;
                    return BASE64_PADDING;
                }
                padding++;
            } else {
                int decoded = base64_value(*character);

                if (padding > 0) {
                    *at = i + k;
                    return BASE64_PADDING;
                }
                if (decoded < 0) {
                    *at = i + k;
                    return BASE64_CHARACTER;
                }
                value[k] = decoded;
            }
        }
        if (padding == 1 && (value[2] & 0x03) != 0) {
            *at = i + 2;
            return BASE64_CANONICAL;
        }
        if (padding == 2 && (value[1] & 0x0F) != 0) {
            *at = i + 1;
            return BASE64_CANONICAL;
        }
        bytes[0] = (unsigned char)((value[0] << 2) | (value[1] >> 4));
        bytes[1] = (unsigned char)(((value[1] & 0x0F) << 4) | (value[2] >> 2));
        bytes[2] = (unsigned char)(((value[2] & 0x03) << 6) | value[3]);
        produced = 3 - padding;
        for (k = 0; k < produced; k++) {
            if (!list_push(out, &bytes[k])) {
                return BASE64_MEMORY;
            }
        }
    }
    return BASE64_OK;
}


bool check_base64(CheckContext *ctx, const AstNode *node, size_t ast_index)
{
    size_t argument_index;
    const AstNode *argument;
    List decoded;
    Base64Problem problem;
    size_t at = 0;
    size_t type_index;
    size_t offset;
    size_t i;
    TypedNode typed = {0};

    if (node->right_child != AST_NO_NODE || node->children_count != 1) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_BASE64_ARGUMENT);
        return false;
    }
    argument_index = *(const size_t *)list_get(&ctx->ast->children,
                                               node->children_start);
    argument = list_get(&ctx->ast->nodes, argument_index);
    if (argument == NULL || argument->kind != AST_STRING_LITERAL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_BASE64_ARGUMENT);
        return false;
    }

    list_init(&decoded, sizeof(unsigned char));
    problem = decode_base64(&ctx->ast->bytes, argument->string_offset,
                            argument->string_length, &decoded, &at);
    if (problem == BASE64_MEMORY) {
        list_free(&decoded);
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    if (problem != BASE64_OK) {
        list_free(&decoded);
        error_report_at(ctx->errors, argument->pos,
                        problem == BASE64_CHARACTER
                            ? MESSAGE_CHECKER_BASE64_CHARACTER
                        : problem == BASE64_PADDING
                            ? MESSAGE_CHECKER_BASE64_PADDING
                            : MESSAGE_CHECKER_BASE64_CANONICAL,
                        at);
        return false;
    }

    offset = ctx->program->bytes.length;
    for (i = 0; i < decoded.length; i++) {
        if (!typed_program_push_byte(
                ctx->program, *(const unsigned char *)list_get(&decoded, i))) {
            list_free(&decoded);
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
    }
    typed.string_length = decoded.length;
    list_free(&decoded);
    /* The result is a static Text with its type already fixed: it is bytes,
       not a literal for context to turn into a String<N>. */
    if (!builtin_type(ctx, NIPPY_TYPE_DYNAMIC_STRING, &type_index)) {
        return false;
    }
    typed.kind = TYPED_STRING_LITERAL;
    typed.type_index = type_index;
    typed.pos = node->pos;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    typed.fixed_type = true;
    typed.string_offset = offset;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}
