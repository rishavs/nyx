/* Checker stage defensive tests: malformed syntax trees no parsed program makes. */

#include "compile_support.h"

TEST("missing AST root")
{
    Ast ast;
    TypedProgram program;
    ErrorState errors;

    ast_init(&ast);
    typed_program_init(&program);
    error_init(&errors);

    if (checker_check(&ast, &program, &errors) ||
        !error_contains(&errors, MESSAGE_CHECKER_MISSING_AST_ROOT)) {
        fprintf(stderr, "  empty AST did not produce \"missing AST root\"\n");
        return TEST_FAIL;
    }
    ast_free(&ast);
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("missing AST node")
{
    Ast ast;
    TypedProgram program;
    ErrorState errors;
    char expected[256];
    size_t bad_root = 999;

    ast_init(&ast);
    typed_program_init(&program);
    error_init(&errors);
    list_push(&ast.roots, &bad_root);

    expect_message(expected, sizeof(expected), MESSAGE_CHECKER_MISSING_AST_NODE,
                   bad_root);
    if (checker_check(&ast, &program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  out-of-range root did not produce \"missing AST node\"\n");
        return TEST_FAIL;
    }
    ast_free(&ast);
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("unsupported AST node")
{
    Ast ast;
    TypedProgram program;
    ErrorState errors;
    AstNode node = {0};
    size_t root = 0;

    ast_init(&ast);
    typed_program_init(&program);
    error_init(&errors);
    node.kind = (AstNodeKind)999;
    list_push(&ast.nodes, &node);
    list_push(&ast.roots, &root);

    if (checker_check(&ast, &program, &errors) ||
        !error_contains(&errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE)) {
        fprintf(stderr,
                "  unknown node kind did not produce \"unsupported AST node\"\n");
        return TEST_FAIL;
    }
    ast_free(&ast);
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("missing name or type")
{
    Ast ast;
    TypedProgram program;
    ErrorState errors;
    AstNode node = {0};
    size_t root = 0;

    ast_init(&ast);
    typed_program_init(&program);
    error_init(&errors);
    node.kind = AST_DECLARATION;
    node.name_index = AST_NO_NODE;
    node.left_child = AST_NO_NODE;
    node.right_child = AST_NO_NODE;
    list_push(&ast.nodes, &node);
    list_push(&ast.roots, &root);

    if (checker_check(&ast, &program, &errors) ||
        !error_contains(&errors, MESSAGE_CHECKER_MISSING_NAME_OR_TYPE)) {
        fprintf(stderr, "  missing name did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    ast_free(&ast);
    typed_program_free(&program);
    return TEST_PASS;
}
