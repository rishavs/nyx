/* Scope syntax, and how a Store capability reference is lowered. */

#include "compile_support.h"
#include "../../compiler/checker/checker_internal.h"
#include "../../compiler/generator/generator_internal.h"

static bool add_region_type(TypedProgram *program, NippyType kind,
                            size_t first, size_t count, size_t declaration)
{
    TypedType type = {0};
    size_t index;

    type.kind = kind;
    type.first_child = first;
    type.child_count = count;
    type.declaration_node = declaration;
    return typed_program_add_type(program, &type, &index);
}

TEST("Store capability references preserve their binding")
{
    Ast ast;
    TypedProgram program;
    ErrorState errors;
    CheckContext ctx = {0};
    List stores, locals;
    AstNode receiver = {0}, call = {0};
    VariableEntry parameter = {0};
    StoreEntry store;
    TypedNode typed = {0};
    TextBuffer generated;
    size_t symbol;
    bool ok;

    ast_init(&ast);
    typed_program_init(&program);
    error_init(&errors);
    list_init(&stores, sizeof(StoreEntry));
    list_init(&locals, sizeof(VariableEntry));
    buffer_init(&generated);
    ctx.ast = &ast;
    ctx.program = &program;
    ctx.errors = &errors;
    ctx.stores = &stores;
    ctx.locals = &locals;
    symbol = ast_intern_symbol(&ast, "output", 6);
    receiver.kind = AST_IDENTIFIER;
    receiver.name_index = symbol;
    call.left_child = 0;
    memcpy(parameter.name, "output", 7);
    parameter.type_index = 0;
    ok = symbol != AST_NO_NODE && list_push(&ast.nodes, &receiver) &&
         add_region_type(&program, NIPPY_TYPE_STORE, 0, 0, TYPED_NO_NODE) &&
         list_push(&locals, &parameter) &&
         member_store(&ctx, &call, "copy", &store) &&
         set_store_reference(&ctx, &typed, &store) &&
         typed.store_id == TYPED_NO_NODE &&
         strcmp(typed_program_symbol_text(&program, typed.name_index), "output") == 0 &&
         emit_store_reference(&program, &typed, &generated, &errors) &&
         strcmp(generated.text, "nyx_u_output") == 0;
    buffer_free(&generated);
    list_free(&stores);
    list_free(&locals);
    typed_program_free(&program);
    ast_free(&ast);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("internal scope requires a Store constructor")
{
    return compile_fails("scope s = 1 do end\n", "missing Store constructor",
                         MESSAGE_PARSER_SCOPE_STORE);
}

TEST("internal scope requires do")
{
    return compile_fails("scope s = Store(1) end\n", "missing scope do",
                         MESSAGE_PARSER_SCOPE_DO);
}

TEST("internal scope name is not visible in its capacity")
{
    return compile_fails("scope s = Store(s) do end\n", "scope in capacity",
                         MESSAGE_CHECKER_UNKNOWN_NAME, "s");
}

TEST("internal scope rejects noninteger capacity")
{
    return compile_fails("scope s = Store(true) do end\n", "noninteger capacity",
                         MESSAGE_CHECKER_SCOPE_CAPACITY);
}

TEST("internal scope rejects negative capacity")
{
    return compile_fails("scope s = Store(-1) do end\n", "negative scope capacity",
                         MESSAGE_CHECKER_NEGATIVE_CAPACITY);
}


TEST("scope is a reserved word")
{
    return compile_fails("var scope: Int32 = 1\n", "scope as an identifier",
                         MESSAGE_PARSER_EXPECTED_VARIABLE_NAME);
}
