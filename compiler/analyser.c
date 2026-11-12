#include "analyser.h"

#include "messages.h"

/* STUB. The analyser validates the typed program's index links and returns the
   input as-is. Memory, ownership, and lifetime analysis do not exist yet, and
   Nippy has no storage-bearing constructs to analyse.

   It takes the typed program, not the syntax tree: by the time it runs the tree
   has been freed, so this stage cannot reach back even by accident. It still
   produces no artifact of its own; when ownership rules land they either refine
   the typed program or become a distinct analysis artifact. */
bool analyser_analyse(const TypedProgram *program, ErrorState *errors)
{
    size_t i;
    size_t store_count = 0;

    for (i = 0; i < program->roots.length; i++) {
        size_t root = *(const size_t *)list_get(&program->roots, i);
        const TypedNode *node = typed_program_node(program, root);

        if (node != NULL && node->kind == TYPED_STORE_DECLARATION) {
            store_count++;
        }
    }

    for (i = 0; i < program->roots.length; i++) {
        size_t root = *(const size_t *)list_get(&program->roots, i);

        if (root >= program->nodes.length) {
            error_report(errors, MESSAGE_ANALYSER_ROOT_OUT_OF_RANGE, root);
            return false;
        }
    }

    for (i = 0; i < program->nodes.length; i++) {
        const TypedNode *node = typed_program_node(program, i);
        size_t j;

        if (node == NULL) {
            error_report(errors, MESSAGE_ANALYSER_INVALID_NODE_LINK, i);
            return false;
        }
        if (node->left_child != TYPED_NO_NODE &&
            node->left_child >= program->nodes.length) {
            error_report_at(errors, node->pos, MESSAGE_ANALYSER_INVALID_NODE_LINK,
                            i);
            return false;
        }
        if (node->right_child != TYPED_NO_NODE &&
            node->right_child >= program->nodes.length) {
            error_report_at(errors, node->pos, MESSAGE_ANALYSER_INVALID_NODE_LINK,
                            i);
            return false;
        }
        if (node->children_count != 0 &&
            (node->children_start >= program->children.length ||
             node->children_start + node->children_count >
                 program->children.length)) {
            error_report_at(errors, node->pos,
                            MESSAGE_ANALYSER_INVALID_NODE_LINK, i);
            return false;
        }
        for (j = 0; j < node->children_count; j++) {
            size_t child = *(const size_t *)list_get(
                &program->children, node->children_start + j);

            if (child >= program->nodes.length) {
                error_report_at(errors, node->pos,
                                MESSAGE_ANALYSER_INVALID_CHILD_INDEX, i);
                return false;
            }
        }
        if (node->name_index != TYPED_NO_NODE &&
            node->name_index >= program->symbols.length) {
            error_report_at(errors, node->pos, MESSAGE_ANALYSER_INVALID_NAME_INDEX,
                            i);
            return false;
        }
        if (node->type_index != TYPED_NO_NODE &&
            node->type_index >= program->types.length) {
            error_report_at(errors, node->pos,
                            MESSAGE_ANALYSER_INVALID_TYPE_INDEX, i);
            return false;
        }
        if (node->auxiliary_type != TYPED_NO_NODE &&
            node->auxiliary_type >= program->types.length) {
            error_report_at(errors, node->pos,
                            MESSAGE_ANALYSER_INVALID_AUXILIARY_TYPE, i);
            return false;
        }
        if (node->kind == TYPED_STORE_DECLARATION ||
            node->kind == TYPED_STORE_ALLOC ||
            node->kind == TYPED_STORE_RESET ||
            node->kind == TYPED_HANDLE_READ ||
            node->kind == TYPED_HANDLE_WRITE ||
            node->kind == TYPED_STRING_CONCAT ||
            node->kind == TYPED_STRING_COPY ||
            node->kind == TYPED_STRING_FROM_FIXED ||
            node->kind == TYPED_BUILDER_CREATE ||
            node->kind == TYPED_LIST_CREATE) {
            if (node->store_id != TYPED_NO_NODE) {
                if (node->store_id >= store_count) {
                    error_report_at(errors, node->pos,
                                    MESSAGE_ANALYSER_INVALID_STORE_ID, i);
                    return false;
                }
            } else if (node->kind == TYPED_STORE_DECLARATION ||
                       node->kind == TYPED_STORE_ALLOC ||
                       node->kind == TYPED_STORE_RESET ||
                       node->kind == TYPED_STRING_CONCAT ||
                       node->kind == TYPED_STRING_COPY ||
                       node->kind == TYPED_STRING_FROM_FIXED ||
                       node->kind == TYPED_BUILDER_CREATE ||
                       node->kind == TYPED_LIST_CREATE) {
                error_report_at(errors, node->pos,
                                MESSAGE_ANALYSER_INVALID_STORE_ID, i);
                return false;
            }
            if (typed_program_type(program, node->type_index) == NULL) {
                error_report_at(errors, node->pos,
                                MESSAGE_ANALYSER_INVALID_TYPE_INDEX, i);
                return false;
            }
        }
        if (node->kind == TYPED_STRING_LITERAL) {
            const TypedType *type = typed_program_type(program,
                                                       node->type_index);

            if (node->string_offset > program->bytes.length ||
                node->string_length >
                    program->bytes.length - node->string_offset) {
                error_report_at(errors, node->pos,
                                MESSAGE_ANALYSER_INVALID_STRING_LITERAL);
                return false;
            }
            if (type == NULL ||
                (type->kind != NIPPY_TYPE_STRING &&
                 type->kind != NIPPY_TYPE_DYNAMIC_STRING &&
                 type->kind != NIPPY_TYPE_STRING_LITERAL)) {
                error_report_at(errors, node->pos,
                                MESSAGE_ANALYSER_INVALID_TYPE_INDEX, i);
                return false;
            }
            if (type->kind == NIPPY_TYPE_STRING &&
                node->string_length > type->fixed_length) {
                error_report_at(errors, node->pos,
                                MESSAGE_ANALYSER_INVALID_STRING_LITERAL);
                return false;
            }
        }
        if (node->kind == TYPED_ERROR_CONSTRUCT) {
            const TypedType *type = typed_program_type(program,
                                                       node->type_index);

            if (type == NULL || type->kind != NIPPY_TYPE_ERROR ||
                node->children_count != 6) {
                error_report_at(errors, node->pos,
                                MESSAGE_ANALYSER_INVALID_ERROR, i);
                return false;
            }
        }
        if (node->kind == TYPED_ERROR_FIELD) {
            if (node->integer_value < 0 || node->integer_value > 5) {
                error_report_at(errors, node->pos,
                                MESSAGE_ANALYSER_INVALID_ERROR, i);
                return false;
            }
        }
        if (node->kind == TYPED_SUM_INJECT ||
            node->kind == TYPED_SUM_PAYLOAD) {
            const TypedType *type = typed_program_type(program,
                                                       node->type_index);

            if (type == NULL || node->integer_value < 0) {
                error_report_at(errors, node->pos,
                                MESSAGE_ANALYSER_INVALID_TYPE_INDEX, i);
                return false;
            }
            if (node->kind == TYPED_SUM_INJECT &&
                (type->kind != NIPPY_TYPE_UNION ||
                 (size_t)node->integer_value >= type->child_count)) {
                error_report_at(errors, node->pos,
                                MESSAGE_ANALYSER_INVALID_TYPE_CHILD,
                                node->type_index);
                return false;
            }
        }
    }

    for (i = 0; i < program->types.length; i++) {
        const TypedType *type = typed_program_type(program, i);

        if (type == NULL) {
            error_report(errors, MESSAGE_ANALYSER_INVALID_TYPE_INDEX, i);
            return false;
        }
        if (type->kind == NIPPY_TYPE_STRING &&
            type->fixed_length > (size_t)program->max_string_capacity) {
            error_report(errors, MESSAGE_ANALYSER_INVALID_TYPE_CHILD, i);
            return false;
        }
        if (type->kind == NIPPY_TYPE_STRUCT &&
            type->name_index >= program->symbols.length) {
            error_report(errors, MESSAGE_ANALYSER_INVALID_NAME_INDEX, i);
            return false;
        }
        if (type->kind == NIPPY_TYPE_STRUCT) {
            size_t j;

            if (type->source_name_index != TYPED_NO_NODE &&
                type->source_name_index >= program->symbols.length) {
                error_report(errors, MESSAGE_ANALYSER_INVALID_NAME_INDEX, i);
                return false;
            }
            if (type->declaration_node == TYPED_NO_NODE ||
                type->declaration_node >= program->nodes.length) {
                error_report(errors, MESSAGE_ANALYSER_INVALID_DECLARATION, i);
                return false;
            }
            {
                const TypedNode *declaration = typed_program_node(
                    program, type->declaration_node);

                if (declaration == NULL ||
                    declaration->kind != TYPED_TYPE_DECLARATION) {
                    error_report(errors, MESSAGE_ANALYSER_INVALID_DECLARATION,
                                 i);
                    return false;
                }
            }
            if (type->child_count != 0 &&
                (type->first_child + type->child_count >
                     program->type_children.length)) {
                error_report(errors, MESSAGE_ANALYSER_INVALID_TYPE_CHILD, i);
                return false;
            }
            for (j = 0; j < type->child_count; j++) {
                size_t argument = *(const size_t *)list_get(
                    &program->type_children, type->first_child + j);

                if (argument >= program->types.length) {
                    error_report(errors, MESSAGE_ANALYSER_INVALID_TYPE_CHILD,
                                 i);
                    return false;
                }
            }
        }
        if (type->kind == NIPPY_TYPE_UNION) {
            size_t j;

            if (type->child_count < 2 ||
                type->first_child + type->child_count >
                    program->type_children.length) {
                error_report(errors, MESSAGE_ANALYSER_INVALID_TYPE_CHILD, i);
                return false;
            }
            for (j = 0; j < type->child_count; j++) {
                size_t alternative = *(const size_t *)list_get(
                    &program->type_children, type->first_child + j);

                if (alternative >= program->types.length) {
                    error_report(errors, MESSAGE_ANALYSER_INVALID_TYPE_CHILD,
                                 i);
                    return false;
                }
            }
        }
        if (type->kind == NIPPY_TYPE_HANDLE) {
            size_t element;

            if (type->child_count != 1 ||
                type->first_child >= program->type_children.length) {
                error_report(errors, MESSAGE_ANALYSER_INVALID_HANDLE_TYPE, i);
                return false;
            }
            element = *(const size_t *)list_get(&program->type_children,
                                                type->first_child);
            if (element >= program->types.length) {
                error_report(errors, MESSAGE_ANALYSER_INVALID_HANDLE_TYPE, i);
                return false;
            }
        }
        if (type->kind == NIPPY_TYPE_ERROR) {
            size_t j;

            if (type->child_count != 3 ||
                type->first_child + type->child_count >
                    program->type_children.length) {
                error_report(errors, MESSAGE_ANALYSER_INVALID_TYPE_CHILD, i);
                return false;
            }
            for (j = 0; j < type->child_count; j++) {
                size_t field = *(const size_t *)list_get(
                    &program->type_children, type->first_child + j);
                const TypedType *field_type = typed_program_type(program,
                                                                 field);

                if (field >= program->types.length || field_type == NULL ||
                    field_type->kind != NIPPY_TYPE_STRING) {
                    error_report(errors, MESSAGE_ANALYSER_INVALID_TYPE_CHILD,
                                 i);
                    return false;
                }
            }
        }
        if (type->kind == NIPPY_TYPE_FUNCTION &&
            type->signature_index >= program->signatures.length) {
            error_report(errors, MESSAGE_ANALYSER_INVALID_SIGNATURE_INDEX, i);
            return false;
        }
    }

    for (i = 0; i < program->signatures.length; i++) {
        const TypedFunctionSignature *signature =
            typed_program_signature(program, i);
        size_t j;

        if (signature == NULL ||
            signature->return_type >= program->types.length ||
            signature->first_parameter + signature->parameter_count >
                program->signature_parameters.length) {
            error_report(errors, MESSAGE_ANALYSER_INVALID_SIGNATURE_LINK, i);
            return false;
        }
        for (j = 0; j < signature->parameter_count; j++) {
            size_t parameter = *(const size_t *)list_get(
                &program->signature_parameters,
                signature->first_parameter + j);

            if (parameter >= program->types.length) {
                error_report(errors, MESSAGE_ANALYSER_INVALID_SIGNATURE_LINK, i);
                return false;
            }
        }
    }
    return true;
}
