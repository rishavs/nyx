/* The analyser's facts: which runtime features a typed program uses. */

#include "compile_support.h"
#include "lexer.h"
#include "parser.h"

/* Runs the whole front end on `source` and returns the analyser's facts. */
static bool analyse_source(const char *source, Analysis *analysis)
{
    LexerResult lexed = lexer_tokenize(source);
    Ast ast;
    TypedProgram program;
    ErrorState errors;
    bool ok;

    error_init(&errors);
    typed_program_init(&program);
    if (!lexed.succeeded ||
        !parser_parse(&lexed.tokens, &lexed.string_bytes, &ast, &errors)) {
        lexer_result_free(&lexed);
        typed_program_free(&program);
        return false;
    }
    ok = checker_check(&ast, &program, &errors);
    ast_free(&ast);
    lexer_result_free(&lexed);
    ok = ok && analyser_analyse(&program, analysis, &errors);
    typed_program_free(&program);
    return ok;
}

typedef struct {
    const char *what;
    const char *source;
    uint32_t required; /* features that must be set */
    uint32_t absent;   /* features that must not be set */
    size_t stores;
} FeatureCase;

static const FeatureCase feature_cases[] = {
    {"an integer", "var x: Int32 = 1\n", 0,
     ANALYSIS_STRING | ANALYSIS_LIST | ANALYSIS_PARALLEL | ANALYSIS_SCOPE |
         ANALYSIS_CHECKED_ARITHMETIC | ANALYSIS_STORE_OR_HANDLE, 0},
    {"print and a string literal", "print(\"hi\")\n",
     ANALYSIS_PRINT | ANALYSIS_STRING, ANALYSIS_NOW | ANALYSIS_SLEEP, 0},
    {"checked arithmetic", "var a: Int32 = 7\nvar b: Int32 = a % 2\n",
     ANALYSIS_CHECKED_ARITHMETIC, ANALYSIS_CONVERSION, 0},
    {"a Store",
     "global s: Store = Store(16)\nvar h: Handle<Int32> = s.alloc<Int32>(1)\n",
     ANALYSIS_STORE_OR_HANDLE, ANALYSIS_SCOPE, 1},
    {"a growable List",
     "global s: Store = Store(64)\nvar xs: List<Int32> = s.list<Int32>(4)\n",
     ANALYSIS_LIST | ANALYSIS_DYNAMIC_LIST | ANALYSIS_STORE_OR_HANDLE,
     ANALYSIS_SLICE, 1},
    {"a scope",
     "fun f(): Int32\n scope s = Store(16) do\n  return 1\n end\nend\n",
     ANALYSIS_SCOPE, ANALYSIS_PARALLEL | ANALYSIS_POOL, 0},
    {"a recursive function",
     "fun f(n: Int32): Int32\n if n > 0 then return f(n - 1) end\n return 0\nend\n", ANALYSIS_RECURSION, 0, 0},
    {"a function that does not recurse",
     "fun f(n: Int32): Int32 return n end\n", 0, ANALYSIS_RECURSION, 0},
    {"now", "var t: Int64 = now()\n", ANALYSIS_NOW,
     ANALYSIS_PARALLEL | ANALYSIS_SLEEP, 0},
    {"sleep", "sleep(1)\n", ANALYSIS_SLEEP, ANALYSIS_PARALLEL | ANALYSIS_NOW,
     0},
    {"an atomic", "global a: Atomic<Int32> = Atomic(0)\nvar n: Int32 = a.load()\n",
     ANALYSIS_ATOMICS, ANALYSIS_CHANNELS, 0},
    {"cores", "var n: Int32 = cores()\n", ANALYSIS_CORES, ANALYSIS_PARALLEL,
     0},
    {"a signed and unsigned comparison",
     "var a: Int32 = 5\nvar b: Uint32 = 5\nvar r: Bool = a == b\n",
     ANALYSIS_EQUALITY_SIGNED_UNSIGNED,
     ANALYSIS_EQUALITY_SIGNED_FLOAT | ANALYSIS_EQUALITY_UNSIGNED_FLOAT, 0},
    {"a signed and float comparison",
     "var a: Int32 = 5\nvar f: Float64 = 5.0\nvar r: Bool = a != f\n",
     ANALYSIS_EQUALITY_SIGNED_FLOAT,
     ANALYSIS_EQUALITY_SIGNED_UNSIGNED | ANALYSIS_EQUALITY_UNSIGNED_FLOAT, 0},
    {"an unsigned and float comparison",
     "var b: Uint32 = 5\nvar f: Float64 = 5.0\nvar r: Bool = f == b\n",
     ANALYSIS_EQUALITY_UNSIGNED_FLOAT,
     ANALYSIS_EQUALITY_SIGNED_UNSIGNED | ANALYSIS_EQUALITY_SIGNED_FLOAT, 0},
    {"a comparison within one domain",
     "var a: Int32 = 5\nvar b: Int32 = 5\nvar r: Bool = a == b\n", 0,
     ANALYSIS_EQUALITY_SIGNED_UNSIGNED | ANALYSIS_EQUALITY_SIGNED_FLOAT |
         ANALYSIS_EQUALITY_UNSIGNED_FLOAT, 0},
    {"a global channel", "global c: Channel<Int32> = Channel(1)\n",
     ANALYSIS_CHANNELS, ANALYSIS_CHANNEL_VALUES, 0},
    {"a channel made in a pool",
     "fun feed(c: Channel<Int64>) c.send(1) end\n"
     "parallel pool = Store(4096) do\n"
     "  var a = pool.channel<Int64>(4)\n"
     "  with feed(a)\n"
     "end\n",
     ANALYSIS_CHANNEL_VALUES | ANALYSIS_CHANNELS | ANALYSIS_POOL, 0, 0},
    {"a parallel block without a pool",
     "fun sq(n: Int32): Int32 return n * n end\nvar a: Int32 = 0\n"
     "parallel do\n with a = sq(3)\nend\n",
     ANALYSIS_PARALLEL, ANALYSIS_POOL | ANALYSIS_SCOPE, 0},
    {"a parallel block with a pool",
     "fun sq(n: Int32): Int32 return n * n end\n"
     "parallel pool = Store(64) do\n with sq(3)\nend\n",
     ANALYSIS_PARALLEL | ANALYSIS_POOL | ANALYSIS_SCOPE, 0, 0}
};

TEST("the analyser reports the features a program uses")
{
    size_t i;

    for (i = 0; i < sizeof(feature_cases) / sizeof(feature_cases[0]); i++) {
        const FeatureCase *row = &feature_cases[i];
        Analysis analysis;

        if (!analyse_source(row->source, &analysis)) {
            fprintf(stderr, "  %s: did not analyse\n", row->what);
            return TEST_FAIL;
        }
        if ((analysis.features & row->required) != row->required ||
            (analysis.features & row->absent) != 0 ||
            analysis.store_count != row->stores) {
            fprintf(stderr, "  %s: features 0x%x, stores %zu\n", row->what,
                    (unsigned)analysis.features, analysis.store_count);
            analysis_free(&analysis);
            return TEST_FAIL;
        }
        analysis_free(&analysis);
    }
    return TEST_PASS;
}

TEST("analysis_collect agrees with the analyser")
{
    const char *source =
        "global s: Store = Store(64)\nvar xs: List<Int32> = s.list<Int32>(4)\n";
    LexerResult lexed = lexer_tokenize(source);
    Ast ast;
    TypedProgram program;
    ErrorState errors;
    Analysis from_analyser, collected;
    bool ok;

    error_init(&errors);
    typed_program_init(&program);
    if (!lexed.succeeded ||
        !parser_parse(&lexed.tokens, &lexed.string_bytes, &ast, &errors)) {
        lexer_result_free(&lexed);
        typed_program_free(&program);
        return TEST_ERROR;
    }
    ok = checker_check(&ast, &program, &errors);
    ast_free(&ast);
    lexer_result_free(&lexed);
    ok = ok && analyser_analyse(&program, &from_analyser, &errors);
    ok = ok && analysis_collect(&program, &collected, &errors) &&
         from_analyser.features == collected.features &&
         from_analyser.store_count == collected.store_count &&
         memcmp(from_analyser.kind_start, collected.kind_start,
                sizeof(collected.kind_start)) == 0;
    if (ok) {
        size_t total = collected.kind_start[TYPED_NODE_KIND_COUNT];

        ok = memcmp(from_analyser.node_order, collected.node_order,
                    total * sizeof(size_t)) == 0;
    }
    analysis_free(&from_analyser);
    analysis_free(&collected);
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("the node index lists every node once, by kind and in order")
{
    const char *source =
        "global s: Store = Store(64)\n"
        "var xs: List<Int32> = s.list<Int32>(4)\n"
        "xs.push(1)\n"
        "var first = xs.pop()\n"
        "var t: Text = \"a\"\n"
        "print(first, t)\n";
    LexerResult lexed = lexer_tokenize(source);
    Ast ast;
    TypedProgram program;
    ErrorState errors;
    Analysis analysis;
    bool ok;
    size_t kind, seen = 0;

    error_init(&errors);
    typed_program_init(&program);
    if (!lexed.succeeded ||
        !parser_parse(&lexed.tokens, &lexed.string_bytes, &ast, &errors)) {
        lexer_result_free(&lexed);
        typed_program_free(&program);
        return TEST_ERROR;
    }
    ok = checker_check(&ast, &program, &errors);
    ast_free(&ast);
    lexer_result_free(&lexed);
    ok = ok && analyser_analyse(&program, &analysis, &errors);
    if (!ok) {
        typed_program_free(&program);
        return TEST_ERROR;
    }
    for (kind = 0; kind < TYPED_NODE_KIND_COUNT; kind++) {
        size_t count, i;
        const size_t *nodes = analysis_nodes(&analysis, (TypedNodeKind)kind,
                                             &count);

        for (i = 0; i < count; i++) {
            const TypedNode *node = typed_program_node(&program, nodes[i]);

            if (node == NULL || (size_t)node->kind != kind ||
                (i > 0 && nodes[i] <= nodes[i - 1])) {
                ok = false;
            }
        }
        seen += count;
    }
    ok = ok && seen == program.nodes.length;
    {
        /* The type index lists every type once, by kind, in order. */
        size_t type_kind, type_seen = 0;

        for (type_kind = 0; type_kind < NYX_TYPE_KIND_COUNT; type_kind++) {
            size_t type_count, t;
            const size_t *types = analysis_types(&analysis,
                                                 (NyxType)type_kind,
                                                 &type_count);

            for (t = 0; t < type_count; t++) {
                const TypedType *type = typed_program_type(&program, types[t]);

                if (type == NULL || (size_t)type->kind != type_kind ||
                    (t > 0 && types[t] <= types[t - 1])) {
                    ok = false;
                }
            }
            type_seen += type_count;
        }
        ok = ok && type_seen == program.types.length;
    }
    {
        size_t pops, prints, none;

        analysis_nodes(&analysis, TYPED_LIST_POP, &pops);
        analysis_nodes(&analysis, TYPED_PRINT, &prints);
        analysis_nodes(&analysis, TYPED_CHANNEL_OP, &none);
        ok = ok && pops == 1 && prints == 1 && none == 0;
    }
    analysis_free(&analysis);
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("an allocation failure while indexing is reported and leaks nothing")
{
    TypedProgram program;
    ErrorState errors;
    Analysis analysis;
    TypedNode node = {0};
    size_t index;
    bool ok;

    typed_program_init(&program);
    error_init(&errors);
    node.kind = TYPED_BREAK;
    node.name_index = TYPED_NO_NODE;
    node.type_index = TYPED_NO_NODE;
    node.auxiliary_type = TYPED_NO_NODE;
    node.left_child = TYPED_NO_NODE;
    node.right_child = TYPED_NO_NODE;
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    nyx_alloc_fail_after(0);
    ok = !analysis_collect(&program, &analysis, &errors) &&
         error_contains(&errors, MESSAGE_ANALYSER_OUT_OF_MEMORY) &&
         analysis.node_order == NULL;
    nyx_alloc_reset();
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("a rejected program leaves the analysis untouched")
{
    Analysis analysis;
    TypedProgram program;
    ErrorState errors;
    TypedNode node = {0};
    size_t index;
    bool ok;

    memset(&analysis, 0x5a, sizeof(analysis));
    typed_program_init(&program);
    error_init(&errors);
    node.kind = TYPED_IF_EXPRESSION;
    node.name_index = TYPED_NO_NODE;
    node.type_index = TYPED_NO_NODE;
    node.auxiliary_type = TYPED_NO_NODE;
    node.left_child = TYPED_NO_NODE;
    node.right_child = TYPED_NO_NODE;
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    ok = !analyser_analyse(&program, &analysis, &errors) &&
         analysis.features == 0x5a5a5a5au;
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("a conversion to the same type needs no helper")
{
    Analysis analysis;

    if (!analyse_source("var a: Int32 = 5\nvar b: Int64 = Int64(a)\n", &analysis)) {
        return TEST_ERROR;
    }
    if (!analysis_has(&analysis, ANALYSIS_CONVERSION)) {
        fprintf(stderr, "  widening did not set the conversion feature\n");
        return TEST_FAIL;
    }
    if (!analyse_source("var a: Int32 = 5\nvar b: Int32 = Int32(a)\n", &analysis)) {
        return TEST_ERROR;
    }
    return analysis_has(&analysis, ANALYSIS_CONVERSION) ? TEST_FAIL
                                                        : TEST_PASS;
}

TEST("the node index finds the nodes of one root")
{
    const char *source =
        "fun first(): Int32\n"
        " scope a = Store(16) do\n  return 1\n end\n"
        "end\n"
        "fun second(n: Int32): Int32\n"
        " if n > 0 then return second(n - 1) end\n"
        " scope b = Store(16) do\n  scope c = Store(8) do\n   return 2\n  end\n end\n"
        "end\n"
        "scope top = Store(4) do\n print(1)\nend\n";
    LexerResult lexed = lexer_tokenize(source);
    Ast ast;
    TypedProgram program;
    ErrorState errors;
    Analysis analysis;
    size_t scopes[3] = {0, 0, 0}; /* first, second, the statement */
    size_t calls_in_second = 0;
    size_t position;
    bool ok;

    error_init(&errors);
    typed_program_init(&program);
    if (!lexed.succeeded ||
        !parser_parse(&lexed.tokens, &lexed.string_bytes, &ast, &errors)) {
        lexer_result_free(&lexed);
        typed_program_free(&program);
        return TEST_ERROR;
    }
    ok = checker_check(&ast, &program, &errors);
    ast_free(&ast);
    lexer_result_free(&lexed);
    ok = ok && analyser_analyse(&program, &analysis, &errors);
    if (!ok) {
        typed_program_free(&program);
        return TEST_ERROR;
    }
    for (position = 0; position < program.roots.length; position++) {
        const TypedNode *root = typed_program_node(
            &program, *(const size_t *)list_get(&program.roots, position));
        size_t count, i;
        const size_t *found = analysis_nodes_in(&analysis, TYPED_SCOPE,
                                                position, &count);

        for (i = 0; i < count; i++) {
            /* Every node listed for a root is owned by that root. */
            if (analysis.node_owner[found[i]] != position) ok = false;
        }
        if (root->kind == TYPED_FUNCTION_DECLARATION) {
            size_t function_index = position == 0 ? 0 : 1;

            scopes[function_index] = count;
            if (function_index == 1) {
                analysis_nodes_in(&analysis, TYPED_FUNCTION_CALL, position,
                                  &calls_in_second);
            }
        } else {
            scopes[2] += count;
        }
    }
    ok = ok && scopes[0] == 1 && scopes[1] == 2 && scopes[2] == 1 &&
         calls_in_second == 1 && analysis_has(&analysis, ANALYSIS_RECURSION);
    {
        size_t none;

        analysis_nodes_in(&analysis, TYPED_SCOPE, program.roots.length + 5,
                          &none);
        ok = ok && none == 0;
    }
    analysis_free(&analysis);
    typed_program_free(&program);
    if (!ok) fprintf(stderr, "  scopes %zu %zu %zu, calls %zu\n", scopes[0], scopes[1], scopes[2], calls_in_second);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("the type index finds Nil, and says so when there is none")
{
    Analysis with_nil, without;
    size_t nil;

    if (!analyse_source("global s: Store = Store(16)\n"
                        "var xs: List<Int32> = s.list<Int32>(2)\n"
                        "var first = xs.pop()\n", &with_nil) ||
        !analyse_source("var n: Int32 = 1\n", &without)) {
        return TEST_ERROR;
    }
    nil = analysis_nil_type(&with_nil);
    /* The optional result of pop is a union with Nil, so Nil is a type. */
    if (nil == TYPED_NO_NODE || analysis_nil_type(&without) != TYPED_NO_NODE) {
        analysis_free(&with_nil);
        analysis_free(&without);
        return TEST_FAIL;
    }
    analysis_free(&with_nil);
    analysis_free(&without);
    return TEST_PASS;
}

static void bare_node(TypedNode *node, TypedNodeKind kind)
{
    memset(node, 0, sizeof(*node));
    node->kind = kind;
    node->name_index = TYPED_NO_NODE;
    node->type_index = TYPED_NO_NODE;
    node->auxiliary_type = TYPED_NO_NODE;
    node->left_child = TYPED_NO_NODE;
    node->right_child = TYPED_NO_NODE;
    node->store_id = TYPED_NO_NODE;
}

TEST("a type that holds itself by value cannot be ordered")
{
    TypedProgram program;
    ErrorState errors;
    Analysis analysis;
    TypedType type = {0};
    TypedNode declaration, field;
    size_t type_index = 0, declaration_index = 0, field_index = 0;
    char expected[256];
    bool ok;

    typed_program_init(&program);
    error_init(&errors);
    type.kind = NYX_TYPE_STRUCT;
    type.name_index = TYPED_NO_NODE;
    type.signature_index = TYPED_NO_NODE;
    type.declaration_node = TYPED_NO_NODE;
    if (!typed_program_add_type(&program, &type, &type_index)) return TEST_ERROR;
    bare_node(&field, TYPED_FIELD);
    field.type_index = type_index;
    if (!typed_program_push(&program, &field, &field_index) ||
        !typed_program_push_child(&program, field_index)) return TEST_ERROR;
    bare_node(&declaration, TYPED_TYPE_DECLARATION);
    declaration.children_start = program.children.length - 1;
    declaration.children_count = 1;
    if (!typed_program_push(&program, &declaration, &declaration_index)) {
        return TEST_ERROR;
    }
    ((TypedType *)list_get(&program.types, type_index))->declaration_node =
        declaration_index;
    expect_message(expected, sizeof(expected), MESSAGE_ANALYSER_TYPE_ORDER,
                   type_index);
    ok = !analysis_collect(&program, &analysis, &errors) &&
         error_contains(&errors, expected) && analysis.node_order == NULL;
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("a print of a type that cannot be printed is rejected")
{
    TypedProgram program;
    ErrorState errors;
    Analysis analysis;
    TypedType store_type = {0};
    TypedNode literal, print;
    size_t type_index = 0, literal_index = 0, print_index = 0;
    char expected[256];
    bool ok;

    typed_program_init(&program);
    error_init(&errors);
    store_type.kind = NYX_TYPE_STORE;
    store_type.name_index = TYPED_NO_NODE;
    store_type.signature_index = TYPED_NO_NODE;
    store_type.declaration_node = TYPED_NO_NODE;
    if (!typed_program_add_type(&program, &store_type, &type_index)) {
        return TEST_ERROR;
    }
    bare_node(&literal, TYPED_INTEGER_LITERAL);
    literal.type_index = type_index;
    if (!typed_program_push(&program, &literal, &literal_index) ||
        !typed_program_push_child(&program, literal_index)) return TEST_ERROR;
    bare_node(&print, TYPED_PRINT);
    print.children_start = program.children.length - 1;
    print.children_count = 1;
    if (!typed_program_push(&program, &print, &print_index)) return TEST_ERROR;
    expect_message(expected, sizeof(expected), MESSAGE_ANALYSER_UNSUPPORTED_TYPE,
                   type_index);
    ok = !analysis_collect(&program, &analysis, &errors) &&
         error_contains(&errors, expected);
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("the analyser lists the types the generator emits from")
{
    const char *source =
        "type Point x: Int32 y: Int32 end\n"
        "type Segment from: Point to: Point end\n"
        "var a: Segment = Segment(Point(1, 2), Point(3, 4))\n"
        "var b: Segment = Segment(Point(1, 2), Point(3, 4))\n"
        "var f: Float64 = 1.5\n"
        "var s: String<4> = \"ab\"\n"
        "var t: Text = \"ab\"\n"
        "print(a, f)\n"
        "print(a == b, s == t)\n";
    LexerResult lexed = lexer_tokenize(source);
    Ast ast;
    TypedProgram program;
    ErrorState errors;
    Analysis analysis;
    size_t i, point = TYPED_NO_NODE, segment = TYPED_NO_NODE;
    size_t point_at = 0, segment_at = 0;
    bool ok;

    error_init(&errors);
    typed_program_init(&program);
    if (!lexed.succeeded ||
        !parser_parse(&lexed.tokens, &lexed.string_bytes, &ast, &errors)) {
        lexer_result_free(&lexed);
        typed_program_free(&program);
        return TEST_ERROR;
    }
    ok = checker_check(&ast, &program, &errors);
    ast_free(&ast);
    lexer_result_free(&lexed);
    ok = ok && analyser_analyse(&program, &analysis, &errors);
    if (!ok) {
        typed_program_free(&program);
        return TEST_ERROR;
    }
    /* Every type is defined once, and a struct after the structs it holds. */
    ok = analysis.type_emit_order.length == program.types.length;
    for (i = 0; i < program.types.length; i++) {
        const TypedType *type = typed_program_type(&program, i);
        const char *name = type->source_name_index == TYPED_NO_NODE ? ""
            : typed_program_symbol_text(&program, type->source_name_index);

        if (type->kind == NYX_TYPE_STRUCT && strcmp(name, "Point") == 0) point = i;
        if (type->kind == NYX_TYPE_STRUCT && strcmp(name, "Segment") == 0) segment = i;
    }
    for (i = 0; i < analysis.type_emit_order.length; i++) {
        size_t type = *(const size_t *)list_get(&analysis.type_emit_order, i);

        if (type == point) point_at = i;
        if (type == segment) segment_at = i;
    }
    ok = ok && point != TYPED_NO_NODE && segment != TYPED_NO_NODE &&
         point_at < segment_at;
    /* Printing a Segment and a Float64 needs helpers for Segment, Point, and
       the Float64, and the float one is flagged. */
    ok = ok && analysis_has(&analysis, ANALYSIS_PRINT_FLOAT) &&
         analysis.print_types.length >= 3;
    /* Comparing Segments needs an equality helper; comparing String<4> with Text
       needs one pair helper. */
    ok = ok && analysis.equal_types.length >= 2 &&
         analysis.equal_pairs.length == 2 &&
         !analysis_has(&analysis, ANALYSIS_EQUALITY_HANDLE);
    analysis_free(&analysis);
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}
