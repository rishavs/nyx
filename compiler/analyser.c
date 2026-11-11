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
    }

    for (i = 0; i < program->types.length; i++) {
        const TypedType *type = typed_program_type(program, i);

        if (type == NULL) {
            error_report(errors, MESSAGE_ANALYSER_INVALID_TYPE_INDEX, i);
            return false;
        }
        if (type->kind == NIPPY_TYPE_STRUCT &&
            type->name_index >= program->symbols.length) {
            error_report(errors, MESSAGE_ANALYSER_INVALID_NAME_INDEX, i);
            return false;
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
