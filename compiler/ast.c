#include "ast.h"

#include <string.h>

void ast_init(Ast *ast)
{
    list_init(&ast->nodes, sizeof(AstNode));
    list_init(&ast->symbols, sizeof(AstSymbol));
    list_init(&ast->roots, sizeof(size_t));
    list_init(&ast->children, sizeof(size_t));
    list_init(&ast->type_parameters, sizeof(size_t));
}

void ast_free(Ast *ast)
{
    if (ast == NULL) {
        return;
    }
    list_free(&ast->nodes);
    list_free(&ast->symbols);
    list_free(&ast->roots);
    list_free(&ast->children);
    list_free(&ast->type_parameters);
}

const AstNode *ast_get_node(const Ast *ast, size_t index)
{
    return list_get(&ast->nodes, index);
}

const char *ast_symbol_text(const Ast *ast, size_t index)
{
    const AstSymbol *symbol = list_get(&ast->symbols, index);

    return symbol == NULL ? NULL : symbol->text;
}

size_t ast_intern_symbol(Ast *ast, const char *text, size_t length)
{
    size_t i;

    if (text == NULL || length >= NIPPY_IDENTIFIER_MAX) {
        return AST_NO_NODE;
    }

    /* Re-interning the same spelling keeps the symbol table small and makes
       emitted names stable across repeated parses. */
    for (i = 0; i < ast->symbols.length; i++) {
        const AstSymbol *existing = list_get(&ast->symbols, i);
        if (existing != NULL && strlen(existing->text) == length &&
            memcmp(existing->text, text, length) == 0) {
            return i;
        }
    }

    {
        AstSymbol symbol;
        memset(&symbol, 0, sizeof(symbol));
        memcpy(symbol.text, text, length);
        if (!list_push(&ast->symbols, &symbol)) {
            return AST_NO_NODE;
        }
        return ast->symbols.length - 1;
    }
}

bool ast_push_child(Ast *ast, size_t index)
{
    return list_push(&ast->children, &index);
}

bool ast_push_type_parameter(Ast *ast, size_t index)
{
    return list_push(&ast->type_parameters, &index);
}
