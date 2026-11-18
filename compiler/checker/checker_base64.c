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
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const char *found = memchr(alphabet, character, 64);

    return found == NULL ? -1 : (int)(found - alphabet);
}

/* Four characters of the encoding: six bits each, the last one or two may be
   '=' padding. */
typedef struct {
    int value[4];
    size_t padding;
} Group;

/* Reads the group that starts `i` characters into the text. On failure *at is
   the offset of the offending character. */
/* What one character of a group is. */
typedef enum {
    CHAR_VALUE,
    CHAR_PADDING,
    CHAR_INVALID
} CharClass;

static CharClass classify_character(const unsigned char *character, int *value)
{
    if (character == NULL) {
        return CHAR_INVALID;
    }
    *value = base64_value(*character);
    if (*character == '=') {
        return CHAR_PADDING;
    }
    return *value < 0 ? CHAR_INVALID : CHAR_VALUE;
}

/* Reads the group that starts `i` characters into the text. On failure *at is
   the offset of the offending character. Padding is only the last one or two
   characters, and nothing but padding follows it. */
static Base64Problem read_group(const List *source, size_t offset, size_t i,
                                size_t length, Group *group, size_t *at)
{
    size_t k;

    *group = (Group){{0, 0, 0, 0}, 0};
    for (k = 0; k < 4; k++) {
        int value = 0;
        CharClass kind = classify_character(
            list_get(source, offset + i + k), &value);

        *at = i + k;
        if (kind == CHAR_PADDING ? (i + 4 != length || k < 2)
                                 : group->padding > 0) {
            return BASE64_PADDING;
        }
        if (kind == CHAR_INVALID) {
            return BASE64_CHARACTER;
        }
        group->padding += kind == CHAR_PADDING ? 1 : 0;
        group->value[k] = kind == CHAR_VALUE ? value : 0;
    }
    return BASE64_OK;
}

/* The unused low bits of a padded group must be zero, so one string has one
   spelling. */
static Base64Problem check_canonical(const Group *group, size_t i, size_t *at)
{
    if (group->padding == 1 && (group->value[2] & 0x03) != 0) {
        *at = i + 2;
        return BASE64_CANONICAL;
    }
    if (group->padding == 2 && (group->value[1] & 0x0F) != 0) {
        *at = i + 1;
        return BASE64_CANONICAL;
    }
    return BASE64_OK;
}

static bool push_group_bytes(const Group *group, List *out)
{
    unsigned char bytes[3];
    size_t k;

    bytes[0] = (unsigned char)((group->value[0] << 2) | (group->value[1] >> 4));
    bytes[1] = (unsigned char)(((group->value[1] & 0x0F) << 4) |
                               (group->value[2] >> 2));
    bytes[2] = (unsigned char)(((group->value[2] & 0x03) << 6) | group->value[3]);
    for (k = 0; k < 3 - group->padding; k++) {
        if (!list_push(out, &bytes[k])) {
            return false;
        }
    }
    return true;
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
        Group group;
        Base64Problem problem = read_group(source, offset, i, length, &group, at);

        if (problem == BASE64_OK) {
            problem = check_canonical(&group, i, at);
        }
        if (problem != BASE64_OK) {
            return problem;
        }
        if (!push_group_bytes(&group, out)) {
            return BASE64_MEMORY;
        }
    }
    return BASE64_OK;
}

/* The error for a bad encoding. */
static const char *base64_message(Base64Problem problem)
{
    if (problem == BASE64_CHARACTER) {
        return MESSAGE_CHECKER_BASE64_CHARACTER;
    }
    return problem == BASE64_PADDING ? MESSAGE_CHECKER_BASE64_PADDING
                                     : MESSAGE_CHECKER_BASE64_CANONICAL;
}

/* The node for decoded bytes: a static Text with its type already fixed, since
   it is bytes, not a literal for context to turn into a String<N>. */
static bool emit_decoded(CheckContext *ctx, const AstNode *node, size_t ast_index,
                         const List *decoded)
{
    TypedNode typed;
    size_t type_index;
    size_t i;

    if (!builtin_type(ctx, NYX_TYPE_DYNAMIC_STRING, &type_index)) {
        return false;
    }
    typed = new_node(TYPED_STRING_LITERAL, type_index, node->pos, TYPED_NO_NODE,
                     TYPED_NO_NODE);
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    typed.fixed_type = true;
    typed.string_offset = ctx->program->bytes.length;
    typed.string_length = decoded->length;
    for (i = 0; i < decoded->length; i++) {
        checker_ok(ctx->errors,
                   typed_program_push_byte(
                       ctx->program, *(const unsigned char *)list_get(decoded, i)),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

bool check_base64(CheckContext *ctx, const AstNode *node, size_t ast_index)
{
    const AstNode *argument =
        node->right_child != AST_NO_NODE || node->children_count != 1
            ? NULL
            : list_get(&ctx->ast->nodes,
                       *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start));
    List decoded;
    Base64Problem problem;
    size_t at = 0;
    bool ok;

    if (argument == NULL || argument->kind != AST_STRING_LITERAL) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_BASE64_ARGUMENT);
        return false;
    }
    list_init(&decoded, sizeof(unsigned char));
    problem = decode_base64(&ctx->ast->bytes, argument->string_offset,
                            argument->string_length, &decoded, &at);
    if (problem == BASE64_MEMORY) {
        error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    } else if (problem != BASE64_OK) {
        error_report_at(ctx->errors, argument->pos, base64_message(problem), at);
    }
    ok = problem == BASE64_OK && emit_decoded(ctx, node, ast_index, &decoded);
    list_free(&decoded);
    return ok;
}
