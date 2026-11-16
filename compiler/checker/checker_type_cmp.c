#include "checker_internal.h"



static bool same_type_depth_left(size_t a, size_t b, unsigned int depth, const TypedProgram *program, const TypedType **left, const TypedType **right, bool *outcome)
{
    if (a == b) {
        do { *outcome = (true); return true; } while (0);
    }
    if (depth == 0) {
        do { *outcome = (false); return true; } while (0);
    }
    (*left) = typed_program_type(program, a);
    (*right) = typed_program_type(program, b);
    if ((*left) == NULL || (*right) == NULL || (*left)->kind != (*right)->kind) {
        do { *outcome = (false); return true; } while (0);
    }
    if (nippy_type_is_numeric((*left)->kind) ||
        (*left)->kind == NIPPY_TYPE_BOOL || (*left)->kind == NIPPY_TYPE_NIL ||
        (*left)->kind == NIPPY_TYPE_STORE ||
        (*left)->kind == NIPPY_TYPE_DYNAMIC_STRING ||
        (*left)->kind == NIPPY_TYPE_TEXT_BUILDER) {
        do { *outcome = (true); return true; } while (0);
    }
    if ((*left)->kind == NIPPY_TYPE_STRING) {
        do { *outcome = ((*left)->fixed_length == (*right)->fixed_length); return true; } while (0);
    }
    return false;
}

bool same_type_depth(const TypedProgram *program, size_t a, size_t b, unsigned depth);

static bool same_type_depth_left_element_left_element_left_arg(const TypedType *left, const TypedType *right, bool *outcome2, size_t *i, const TypedProgram *program, unsigned int depth)
{
    if (left->kind == NIPPY_TYPE_STRUCT) {
        if (left->name_index != right->name_index ||
            left->child_count != right->child_count) {
            do { *outcome2 = (false); return false; } while (0);
        }
        for ((*i) = 0; (*i) < left->child_count; (*i)++) {
            size_t left_arg = *(const size_t *)list_get(
                &program->type_children, left->first_child + (*i));
            size_t right_arg = *(const size_t *)list_get(
                &program->type_children, right->first_child + (*i));

            if (!same_type_depth(program, left_arg, right_arg, depth - 1)) {
                do { *outcome2 = (false); return false; } while (0);
            }
        }
        do { *outcome2 = (true); return false; } while (0);
    }
    return true;
}

static bool same_type_depth_left_element_left_element(const TypedType *left, const TypedType *right, bool *outcome2, const TypedProgram *program, unsigned int depth, size_t *i)
{
    if (left->kind == NIPPY_TYPE_HANDLE) {
        if (left->child_count != 1 || right->child_count != 1) {
            do { *outcome2 = (false); return false; } while (0);
        }
        {
            size_t left_element = *(const size_t *)list_get(
                &program->type_children, left->first_child);
            size_t right_element = *(const size_t *)list_get(
                &program->type_children, right->first_child);

            do { *outcome2 = (same_type_depth(program, left_element, right_element,
                                   depth - 1)); return false; } while (0);
        }
    }
    if (!same_type_depth_left_element_left_element_left_arg(left, right, outcome2, i, program, depth)) {
        return false;
    }
    return true;
}

static bool same_type_depth_left_element_same_type_depth_left_element_left_element(const TypedType *left, const TypedType *right, bool *outcome2, const TypedProgram *program, unsigned int depth, size_t *i)
{
    if (!same_type_depth_left_element_left_element(left, right, outcome2, program, depth, i)) {
        return false;
    }
    if (left->kind == NIPPY_TYPE_UNION) {
        if (left->child_count != right->child_count) {
            do { *outcome2 = (false); return false; } while (0);
        }
        for ((*i) = 0; (*i) < left->child_count; (*i)++) {
            size_t left_alt = *(const size_t *)list_get(
                &program->type_children, left->first_child + (*i));
            size_t right_alt = *(const size_t *)list_get(
                &program->type_children, right->first_child + (*i));

            if (!same_type_depth(program, left_alt, right_alt, depth - 1)) {
                do { *outcome2 = (false); return false; } while (0);
            }
        }
        do { *outcome2 = (true); return false; } while (0);
    }
    return true;
}

static bool same_type_depth_left_element(const TypedType *left, const TypedType *right, const TypedProgram *program, unsigned int depth, size_t *i, bool *outcome2)
{
    if (!same_type_depth_left_element_same_type_depth_left_element_left_element(left, right, outcome2, program, depth, i)) {
        return true;
    }
    return false;
}

bool same_type_depth(const TypedProgram *program, size_t a, size_t b, unsigned depth);

static bool same_type_depth_left_element5(const TypedType *left, const TypedType *right, const TypedProgram *program, unsigned int depth, bool *outcome3)
{
    if (left->kind == NIPPY_TYPE_LIST ||
        left->kind == NIPPY_TYPE_FIXED_LIST ||
        left->kind == NIPPY_TYPE_SLICE || left->kind == NIPPY_TYPE_RANGE) {
        size_t left_element;
        size_t right_element;

        if (left->child_count != 1 || right->child_count != 1) {
            do { *outcome3 = (false); return true; } while (0);
        }
        if (left->kind == NIPPY_TYPE_FIXED_LIST &&
            left->fixed_length != right->fixed_length) {
            do { *outcome3 = (false); return true; } while (0);
        }
        left_element = *(const size_t *)list_get(&program->type_children,
                                                 left->first_child);
        right_element = *(const size_t *)list_get(&program->type_children,
                                                  right->first_child);
        do { *outcome3 = (same_type_depth(program, left_element, right_element,
                               depth - 1)); return true; } while (0);
    }
    return false;
}

bool same_type_depth(const TypedProgram *program, size_t a, size_t b, unsigned depth);

static bool same_type_depth_outcome3_left_key(const TypedType *left, const TypedType *right, bool *outcome4, const TypedProgram *program, unsigned int depth)
{
    size_t left_key;
    size_t right_key;
    size_t left_value;
    size_t right_value;

    if (left->child_count != 2 || right->child_count != 2 ||
        (left->kind == NIPPY_TYPE_RECORD &&
         left->fixed_length != right->fixed_length)) {
        do { *outcome4 = (false); return false; } while (0);
    }
    left_key = *(const size_t *)list_get(&program->type_children,
                                         left->first_child);
    right_key = *(const size_t *)list_get(&program->type_children,
                                          right->first_child);
    left_value = *(const size_t *)list_get(&program->type_children,
                                           left->first_child + 1);
    right_value = *(const size_t *)list_get(&program->type_children,
                                            right->first_child + 1);
    do { *outcome4 = (same_type_depth(program, left_key, right_key, depth - 1) &&
           same_type_depth(program, left_value, right_value, depth - 1)); return false; } while (0);
    return true;
}

static bool same_type_depth_outcome3(const TypedType *left, const TypedType *right, const TypedProgram *program, unsigned int depth, size_t *i, bool *outcome4)
{
    {
        bool outcome3;

        if (same_type_depth_left_element5(left, right, program, depth, &outcome3)) {
            do { *outcome4 = (outcome3); return true; } while (0);
        }
    }
    if (left->kind == NIPPY_TYPE_RECORD ||
        left->kind == NIPPY_TYPE_TABLE) {
        if (!same_type_depth_outcome3_left_key(left, right, outcome4, program, depth)) {
        return true;
    }
    }
    {
        bool outcome2;

        if (same_type_depth_left_element(left, right, program, depth, i, &outcome2)) {
            do { *outcome4 = (outcome2); return true; } while (0);
        }
    }
    return false;
}

bool same_type_depth(const TypedProgram *program, size_t a, size_t b,
                            unsigned depth)
{
    const TypedType *left = {0};
    const TypedType *right = {0};
    size_t i = {0};
    {
        bool outcome;

        if (same_type_depth_left(a, b, depth, program, &left, &right, &outcome)) {
            return outcome;
        }
    }
    {
        bool outcome4;

        if (same_type_depth_outcome3(left, right, program, depth, &i, &outcome4)) {
            return outcome4;
        }
    }
    if (left->kind == NIPPY_TYPE_FUNCTION) {
        const TypedFunctionSignature *left_signature =
            typed_program_signature(program, left->signature_index);
        const TypedFunctionSignature *right_signature =
            typed_program_signature(program, right->signature_index);

        if (left_signature == NULL || right_signature == NULL ||
            left_signature->parameter_count !=
                right_signature->parameter_count ||
            !same_type_depth(program, left_signature->return_type,
                             right_signature->return_type, depth - 1)) {
            return false;
        }
        for (i = 0; i < left_signature->parameter_count; i++) {
            size_t left_parameter = *(const size_t *)list_get(
                &program->signature_parameters,
                left_signature->first_parameter + i);
            size_t right_parameter = *(const size_t *)list_get(
                &program->signature_parameters,
                right_signature->first_parameter + i);

            if (!same_type_depth(program, left_parameter, right_parameter,
                                 depth - 1)) {
                return false;
            }
        }
        return true;
    }
    return false;
}



bool same_type(const TypedProgram *program, size_t a, size_t b)
{
    return same_type_depth(program, a, b, NIPPY_TYPE_DEPTH);
}



int type_rank(NippyType kind)
{
    /* The position of a kind is its rank; this order decides how a union's
       alternatives are sorted, so it must not change. */
    static const NippyType order[] = {
        NIPPY_TYPE_INT32, NIPPY_TYPE_BOOL, NIPPY_TYPE_NIL, NIPPY_TYPE_STRUCT,
        NIPPY_TYPE_UNION, NIPPY_TYPE_FUNCTION, NIPPY_TYPE_BYTE,
        NIPPY_TYPE_STORE, NIPPY_TYPE_HANDLE, NIPPY_TYPE_STRING,
        NIPPY_TYPE_DYNAMIC_STRING, NIPPY_TYPE_LIST, NIPPY_TYPE_FIXED_LIST,
        NIPPY_TYPE_SLICE, NIPPY_TYPE_TEXT_BUILDER, NIPPY_TYPE_STRING_LITERAL,
        NIPPY_TYPE_ERROR, NIPPY_TYPE_RECORD, NIPPY_TYPE_TABLE,
        NIPPY_TYPE_INT8, NIPPY_TYPE_INT16, NIPPY_TYPE_INT64,
        NIPPY_TYPE_UINT16, NIPPY_TYPE_UINT32, NIPPY_TYPE_UINT64,
        NIPPY_TYPE_FLOAT32, NIPPY_TYPE_FLOAT64, NIPPY_TYPE_RANGE,
    };
    size_t i;

    for (i = 0; i < NIPPY_COUNT_OF(order); i++) {
        if (order[i] == kind) {
            return (int)i;
        }
    }
    return (int)NIPPY_COUNT_OF(order);
}


int type_compare_depth(const TypedProgram *program, size_t a, size_t b, unsigned depth);

static bool type_compare_depth_step_left_name_step(const TypedType *left, const TypedType *right, int *outcome, size_t *i, unsigned int depth, const TypedProgram *program)
{
    if (left->child_count != right->child_count) {
        do { *outcome = (left->child_count < right->child_count ? -1 : 1); return false; } while (0);
    }
    for ((*i) = 0; (*i) < left->child_count && depth > 0; (*i)++) {
        size_t left_arg = *(const size_t *)list_get(
            &program->type_children, left->first_child + (*i));
        size_t right_arg = *(const size_t *)list_get(
            &program->type_children, right->first_child + (*i));
        int by_arg = type_compare_depth(program, left_arg, right_arg,
                                        depth - 1);

        if (by_arg != 0) {
            do { *outcome = (by_arg); return false; } while (0);
        }
    }
    do { *outcome = (0); return false; } while (0);
    return true;
}

static bool type_compare_depth_step_left_name(const TypedType *left, const TypedProgram *program, const TypedType *right, int *outcome, size_t *i, unsigned int depth)
{
    if (left->kind == NIPPY_TYPE_STRUCT) {
        const char *left_name = typed_program_symbol_text(program,
                                                          left->name_index);
        const char *right_name = typed_program_symbol_text(program,
                                                           right->name_index);
        int by_name = strcmp(left_name == NULL ? "" : left_name,
                             right_name == NULL ? "" : right_name);

        if (by_name != 0) {
            do { *outcome = (by_name); return false; } while (0);
        }
        if (!type_compare_depth_step_left_name_step(left, right, outcome, i, depth, program)) {
        return false;
    }
    }
    return true;
}

static bool type_compare_depth_step(const TypedType *left, const TypedType *right, int *left_rank, int *right_rank, const TypedProgram *program, size_t *i, unsigned int depth, int *outcome)
{
    if (left == NULL || right == NULL) {
        do { *outcome = (left == NULL ? -1 : 1); return true; } while (0);
    }
    (*left_rank) = type_rank(left->kind);
    (*right_rank) = type_rank(right->kind);
    if ((*left_rank) != (*right_rank)) {
        do { *outcome = ((*left_rank) < (*right_rank) ? -1 : 1); return true; } while (0);
    }
    if (!type_compare_depth_step_left_name(left, program, right, outcome, i, depth)) {
        return true;
    }
    return false;
}

int type_compare_depth(const TypedProgram *program, size_t a, size_t b, unsigned depth);

static bool type_compare_depth_step4_step(int by_element, int *outcome2, const TypedType *left, const TypedType *right)
{
    if (by_element != 0) {
        do { *outcome2 = (by_element); return false; } while (0);
    }
    if (left->kind == NIPPY_TYPE_FIXED_LIST &&
        left->fixed_length != right->fixed_length) {
        do { *outcome2 = (left->fixed_length < right->fixed_length ? -1 : 1); return false; } while (0);
    }
    do { *outcome2 = (0); return false; } while (0);
    return true;
}

static bool type_compare_depth_step4_left_element(const TypedType *left, const TypedType *right, int *outcome2, const TypedProgram *program, unsigned int depth)
{
    size_t left_element;
    size_t right_element;
    int by_element;

    if (left->child_count != 1 || right->child_count != 1) {
        do { *outcome2 = (left->child_count < right->child_count ? -1 : 1); return false; } while (0);
    }
    left_element = *(const size_t *)list_get(&program->type_children,
                                             left->first_child);
    right_element = *(const size_t *)list_get(&program->type_children,
                                              right->first_child);
    by_element = type_compare_depth(program, left_element, right_element,
                                   depth - 1);
    if (!type_compare_depth_step4_step(by_element, outcome2, left, right)) {
    return false;
}
    return true;
}

static bool type_compare_depth_step4_step5(const TypedType *left, int *outcome2, const TypedType *right, const TypedProgram *program, unsigned int depth)
{
    if (left->kind == NIPPY_TYPE_DYNAMIC_STRING ||
        left->kind == NIPPY_TYPE_TEXT_BUILDER) {
        do { *outcome2 = (0); return false; } while (0);
    }
    if (left->kind == NIPPY_TYPE_LIST ||
        left->kind == NIPPY_TYPE_FIXED_LIST ||
        left->kind == NIPPY_TYPE_SLICE || left->kind == NIPPY_TYPE_RANGE) {
        if (!type_compare_depth_step4_left_element(left, right, outcome2, program, depth)) {
        return false;
    }
    }
    return true;
}

static bool type_compare_depth_step4(const TypedType *left, const TypedType *right, const TypedProgram *program, unsigned int depth, int *outcome2)
{
    if (!type_compare_depth_step4_step5(left, outcome2, right, program, depth)) {
        return true;
    }
    return false;
}

int type_compare_depth(const TypedProgram *program, size_t a, size_t b, unsigned depth);

static bool type_compare_depth_outcome2_left_key_left_key(const TypedType *left, const TypedType *right, int *outcome3)
{
    if (left->child_count != 2 || right->child_count != 2) {
        do { *outcome3 = (left->child_count < right->child_count ? -1 : 1); return false; } while (0);
    }
    if (left->kind == NIPPY_TYPE_RECORD &&
        left->fixed_length != right->fixed_length) {
        do { *outcome3 = (left->fixed_length < right->fixed_length ? -1 : 1); return false; } while (0);
    }
    return true;
}

static bool type_compare_depth_outcome2_left_key(const TypedType *left, const TypedType *right, int *outcome3, const TypedProgram *program, unsigned int depth)
{
    if (left->kind == NIPPY_TYPE_RECORD ||
        left->kind == NIPPY_TYPE_TABLE) {
        size_t left_key = {0};
        size_t right_key = {0};
        int by_key = {0};
        size_t left_value = {0};
        size_t right_value = {0};
        if (!type_compare_depth_outcome2_left_key_left_key(left, right, outcome3)) {
        return false;
    }
        left_key = *(const size_t *)list_get(&program->type_children,
                                             left->first_child);
        right_key = *(const size_t *)list_get(&program->type_children,
                                              right->first_child);
        by_key = type_compare_depth(program, left_key, right_key, depth - 1);
        if (by_key != 0) {
            do { *outcome3 = (by_key); return false; } while (0);
        }
        left_value = *(const size_t *)list_get(&program->type_children,
                                               left->first_child + 1);
        right_value = *(const size_t *)list_get(&program->type_children,
                                                right->first_child + 1);
        do { *outcome3 = (type_compare_depth(program, left_value, right_value,
                                  depth - 1)); return false; } while (0);
    }
    return true;
}

static bool type_compare_depth_outcome2(const TypedType *left, const TypedType *right, const TypedProgram *program, unsigned int depth, int *outcome3)
{
    {
        int outcome2;

        if (type_compare_depth_step4(left, right, program, depth, &outcome2)) {
            do { *outcome3 = (outcome2); return true; } while (0);
        }
    }
    if (!type_compare_depth_outcome2_left_key(left, right, outcome3, program, depth)) {
        return true;
    }
    if (left->kind == NIPPY_TYPE_HANDLE) {
        size_t left_element;
        size_t right_element;

        if (left->child_count != 1 || right->child_count != 1) {
            do { *outcome3 = (left->child_count < right->child_count ? -1 : 1); return true; } while (0);
        }
        left_element = *(const size_t *)list_get(&program->type_children,
                                                 left->first_child);
        right_element = *(const size_t *)list_get(&program->type_children,
                                                  right->first_child);
        do { *outcome3 = (type_compare_depth(program, left_element, right_element,
                                  depth - 1)); return true; } while (0);
    }
    return false;
}

int type_compare_depth(const TypedProgram *program, size_t a, size_t b, unsigned depth);

static bool type_compare_depth_step7_ls(const TypedType *left, const TypedProgram *program, const TypedType *right, int *outcome2, size_t *i, unsigned int depth)
{
    if (left->kind == NIPPY_TYPE_FUNCTION) {
        const TypedFunctionSignature *ls =
            typed_program_signature(program, left->signature_index);
        const TypedFunctionSignature *rs =
            typed_program_signature(program, right->signature_index);
        int by_return;

        if (ls == NULL || rs == NULL) {
            do { *outcome2 = (ls == NULL ? -1 : 1); return false; } while (0);
        }
        if (ls->parameter_count != rs->parameter_count) {
            do { *outcome2 = (ls->parameter_count < rs->parameter_count ? -1 : 1); return false; } while (0);
        }
        for ((*i) = 0; (*i) < ls->parameter_count && depth > 0; (*i)++) {
            size_t lp = *(const size_t *)list_get(
                &program->signature_parameters, ls->first_parameter + (*i));
            size_t rp = *(const size_t *)list_get(
                &program->signature_parameters, rs->first_parameter + (*i));
            int by_parameter = type_compare_depth(program, lp, rp, depth - 1);

            if (by_parameter != 0) {
                do { *outcome2 = (by_parameter); return false; } while (0);
            }
        }
        by_return = type_compare_depth(program, ls->return_type,
                                       rs->return_type, depth - 1);
        do { *outcome2 = (by_return); return false; } while (0);
    }
    return true;
}

static bool type_compare_depth_step7(const TypedType *left, const TypedType *right, const TypedProgram *program, unsigned int depth, size_t *i, int *outcome2)
{
    if (left->kind == NIPPY_TYPE_STRING) {
        if (left->fixed_length != right->fixed_length) {
            do { *outcome2 = (left->fixed_length < right->fixed_length ? -1 : 1); return true; } while (0);
        }
        do { *outcome2 = (0); return true; } while (0);
    }
    {
        int outcome3;

        if (type_compare_depth_outcome2(left, right, program, depth, &outcome3)) {
            do { *outcome2 = (outcome3); return true; } while (0);
        }
    }
    if (!type_compare_depth_step7_ls(left, program, right, outcome2, i, depth)) {
        return true;
    }
    return false;
}

int type_compare_depth(const TypedProgram *program, size_t a, size_t b, unsigned depth);

static bool type_compare_depth_outcome(const TypedType *left, const TypedType *right, int *left_rank, int *right_rank, const TypedProgram *program, size_t *i, unsigned int depth, int *outcome3)
{
    {
        int outcome;

        if (type_compare_depth_step(left, right, left_rank, right_rank, program, i, depth, &outcome)) {
            do { *outcome3 = (outcome); return true; } while (0);
        }
    }
    if (left->kind == NIPPY_TYPE_UNION) {
        if (left->child_count != right->child_count) {
            do { *outcome3 = (left->child_count < right->child_count ? -1 : 1); return true; } while (0);
        }
        for ((*i) = 0; (*i) < left->child_count && depth > 0; (*i)++) {
            size_t left_alt = *(const size_t *)list_get(
                &program->type_children, left->first_child + (*i));
            size_t right_alt = *(const size_t *)list_get(
                &program->type_children, right->first_child + (*i));
            int by_alt = type_compare_depth(program, left_alt, right_alt,
                                            depth - 1);

            if (by_alt != 0) {
                do { *outcome3 = (by_alt); return true; } while (0);
            }
        }
        do { *outcome3 = (0); return true; } while (0);
    }
    {
        int outcome2;

        if (type_compare_depth_step7(left, right, program, depth, i, &outcome2)) {
            do { *outcome3 = (outcome2); return true; } while (0);
        }
    }
    return false;
}

int type_compare_depth(const TypedProgram *program, size_t a, size_t b,
                              unsigned depth)
{
    const TypedType *left = typed_program_type(program, a);
    const TypedType *right = typed_program_type(program, b);
    int left_rank;
    int right_rank;
    size_t i;

    if (a == b) {
        return 0;
    }
    {
        int outcome3;

        if (type_compare_depth_outcome(left, right, &left_rank, &right_rank, program, &i, depth, &outcome3)) {
            return outcome3;
        }
    }
    return 0;
}



int type_compare(const TypedProgram *program, size_t a, size_t b)
{
    return type_compare_depth(program, a, b, NIPPY_TYPE_DEPTH);
}



bool type_in_list(const TypedProgram *program, const List *indices,
                         size_t type_index)
{
    size_t i;

    for (i = 0; i < indices->length; i++) {
        size_t existing = *(const size_t *)list_get(indices, i);

        if (same_type(program, existing, type_index)) {
            return true;
        }
    }
    return false;
}



static bool intern_union_list_free(List *unique, CheckContext *ctx, size_t *out_index, size_t **items, size_t *i, bool *outcome)
{
    if ((*unique).length == 0) {
        list_free(unique);
        error_report(ctx->errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        do { *outcome = (false); return true; } while (0);
    }
    if ((*unique).length == 1) {
        *out_index = *(const size_t *)list_get(unique, 0);
        list_free(unique);
        do { *outcome = (true); return true; } while (0);
    }

    (*items) = nippy_malloc((*unique).length * sizeof(size_t));
    if ((*items) == NULL) {
        list_free(unique);
        error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        do { *outcome = (false); return true; } while (0);
    }
    for ((*i) = 0; (*i) < (*unique).length; (*i)++) {
        (*items)[(*i)] = *(const size_t *)list_get(unique, (*i));
    }
    for ((*i) = 1; (*i) < (*unique).length; (*i)++) {
        size_t key = (*items)[(*i)];
        size_t j = (*i);

        while (j > 0 && type_compare(ctx->program, (*items)[j - 1], key) > 0) {
            (*items)[j] = (*items)[j - 1];
            j--;
        }
        (*items)[j] = key;
    }

    /* Reuse an existing union with the same canonical alternatives. */
    for ((*i) = 0; (*i) < ctx->program->types.length; (*i)++) {
        const TypedType *type = typed_program_type(ctx->program, (*i));
        size_t j;
        bool match = true;

        if (type == NULL || type->kind != NIPPY_TYPE_UNION ||
            type->child_count != (*unique).length) {
            continue;
        }
        for (j = 0; j < (*unique).length; j++) {
            size_t existing = *(const size_t *)list_get(
                &ctx->program->type_children, type->first_child + j);

            if (!same_type(ctx->program, existing, (*items)[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            *out_index = (*i);
            free((*items));
            list_free(unique);
            do { *outcome = (true); return true; } while (0);
        }
    }
    return false;
}

/* Flattened alternatives are deduplicated and sorted before interning. */
bool intern_union(CheckContext *ctx, const List *alternatives,
                         size_t *out_index)
{
    List unique;
    size_t i;
    size_t *items;

    list_init(&unique, sizeof(size_t));
    for (i = 0; i < alternatives->length; i++) {
        size_t alternative = *(const size_t *)list_get(alternatives, i);
        const TypedType *type = typed_program_type(ctx->program, alternative);

        if (type == NULL) {
            error_report(ctx->errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            list_free(&unique);
            return false;
        }
        if (type->kind == NIPPY_TYPE_UNION) {
            size_t j;

            for (j = 0; j < type->child_count; j++) {
                size_t nested = *(const size_t *)list_get(
                    &ctx->program->type_children, type->first_child + j);

                if (!type_in_list(ctx->program, &unique, nested) &&
                    !list_push(&unique, &nested)) {
                    error_report_fatal(ctx->errors,
                                 MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                    list_free(&unique);
                    return false;
                }
            }
        } else if (!type_in_list(ctx->program, &unique, alternative) &&
                   !list_push(&unique, &alternative)) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            list_free(&unique);
            return false;
        }
    }
    {
        bool outcome;

        if (intern_union_list_free(&unique, ctx, out_index, &items, &i, &outcome)) {
            return outcome;
        }
    }

    {
        TypedType type = {0};
        size_t first = ctx->program->type_children.length;

        for (i = 0; i < unique.length; i++) {
            if (!typed_program_add_type_child(ctx->program, items[i])) {
                free(items);
                list_free(&unique);
                error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                return false;
            }
        }
        type.kind = NIPPY_TYPE_UNION;
        type.name_index = TYPED_NO_NODE;
        type.signature_index = TYPED_NO_NODE;
        type.first_child = first;
        type.child_count = unique.length;
        type.declaration_node = TYPED_NO_NODE;
        type.source_name_index = TYPED_NO_NODE;
        if (!typed_program_add_type(ctx->program, &type, out_index)) {
            free(items);
            list_free(&unique);
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
    }
    free(items);
    list_free(&unique);
    return true;
}



bool union_has_alternative(const TypedProgram *program,
                                  const TypedType *union_type,
                                  size_t wanted, size_t *out_tag)
{
    size_t i;

    for (i = 0; i < union_type->child_count; i++) {
        size_t alternative = *(const size_t *)list_get(
            &program->type_children, union_type->first_child + i);

        if (same_type(program, alternative, wanted)) {
            if (out_tag != NULL) {
                *out_tag = i;
            }
            return true;
        }
    }
    return false;
}



bool type_ast_text(const Ast *ast, size_t ast_index, char *buffer, size_t size);

static bool type_ast_text_name_name(const AstNode *node, const Ast *ast, char *buffer, size_t size, size_t *used)
{
    if (node->kind == AST_TYPE_NAME) {
        const char *name = ast_symbol_text(ast, node->name_index);

        type_ast_append(name == NULL ? "value" : name, buffer, size, used);
        return false;
    }
    if (node->kind == AST_TYPE_FUNCTION) {
        type_ast_append("Fun", buffer, size, used);
        return false;
    }
    if (node->kind == AST_TYPE_FIXED_STRING) {
        char number[32];

        snprintf(number, sizeof(number), "String<%d>", node->integer_value);
        type_ast_append(number, buffer, size, used);
        return false;
    }
    if (node->kind == AST_TYPE_RECORD || node->kind == AST_TYPE_TABLE) {
        char left_text[128];
        char right_text[128];

        type_ast_append(node->kind == AST_TYPE_RECORD ? "Record" : "Table",
                        buffer, size, used);
        type_ast_append("<", buffer, size, used);
        type_ast_text(ast, node->left_child, left_text, sizeof(left_text));
        type_ast_text(ast, node->right_child, right_text, sizeof(right_text));
        type_ast_append(left_text, buffer, size, used);
        type_ast_append(", ", buffer, size, used);
        type_ast_append(right_text, buffer, size, used);
        if (node->kind == AST_TYPE_RECORD) {
            char number[32];

            snprintf(number, sizeof(number), ", %d", node->integer_value);
            type_ast_append(number, buffer, size, used);
        }
        type_ast_append(">", buffer, size, used);
        return false;
    }
    return true;
}

static bool type_ast_text_name(const AstNode *node, const Ast *ast, char *buffer, size_t size, size_t *used)
{
    if (!type_ast_text_name_name(node, ast, buffer, size, used)) {
        return false;
    }
    if (node->kind == AST_TYPE_LIST ||
        node->kind == AST_TYPE_FIXED_LIST ||
        node->kind == AST_TYPE_SLICE) {
        char element_text[128];

        type_ast_append(node->kind == AST_TYPE_SLICE
                            ? "Slice"
                            : (node->kind == AST_TYPE_FIXED_LIST ? "Array"
                                                                 : "List"),
                        buffer, size, used);
        type_ast_append("<", buffer, size, used);
        type_ast_text(ast, node->left_child, element_text,
                      sizeof(element_text));
        type_ast_append(element_text, buffer, size, used);
        if (node->kind == AST_TYPE_FIXED_LIST) {
            char number[32];

            snprintf(number, sizeof(number), ", %d", node->integer_value);
            type_ast_append(number, buffer, size, used);
        }
        type_ast_append(">", buffer, size, used);
        return false;
    }
    return true;
}

bool type_ast_text(const Ast *ast, size_t ast_index, char *buffer, size_t size);

static bool type_ast_text_type_ast_text_name(const AstNode *node, const Ast *ast, char *buffer, size_t size, size_t *used)
{
    if (!type_ast_text_name(node, ast, buffer, size, used)) {
        return false;
    }
    if (node->kind == AST_TYPE_APPLICATION) {
        const char *name = ast_symbol_text(ast, node->name_index);
        size_t i;

        type_ast_append(name == NULL ? "value" : name, buffer, size, used);
        type_ast_append("<", buffer, size, used);
        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &ast->children, node->children_start + i);
            char child_text[128];

            if (i > 0) {
                type_ast_append(", ", buffer, size, used);
            }
            type_ast_text(ast, child, child_text, sizeof(child_text));
            type_ast_append(child_text, buffer, size, used);
        }
        type_ast_append(">", buffer, size, used);
        return false;
    }
    if (node->kind == AST_TYPE_UNION) {
        size_t i;

        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &ast->children, node->children_start + i);
            char child_text[128];

            if (i > 0) {
                type_ast_append(" | ", buffer, size, used);
            }
            type_ast_text(ast, child, child_text, sizeof(child_text));
            type_ast_append(child_text, buffer, size, used);
        }
        return false;
    }
    return true;
}

bool type_ast_text(const Ast *ast, size_t ast_index, char *buffer,
                          size_t size)
{
    const AstNode *node = ast_get_node(ast, ast_index);
    size_t used = 0;

    buffer[0] = '\0';
    if (node == NULL) {
        type_ast_append("value", buffer, size, &used);
        return true;
    }
    if (!type_ast_text_type_ast_text_name(node, ast, buffer, size, &used)) {
        return true;
    }
    type_ast_append("value", buffer, size, &used);
    return true;
}



void type_ast_append(const char *text, char *buffer, size_t size,
                            size_t *used)
{
    size_t length = strlen(text);

    if (*used < size) {
        size_t available = size - *used - 1;

        if (length > available) {
            length = available;
        }
        memcpy(buffer + *used, text, length);
        *used += length;
        buffer[*used] = '\0';
    }
}

