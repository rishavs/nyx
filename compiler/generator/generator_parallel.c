#include "generator_internal.h"

/* The parallel block. Each arm becomes a record holding its arguments and its
   result, and a thread entry point that calls the function. The arguments are
   evaluated here, in the order written, before any thread starts; the first
   arm runs on the calling thread; the block ends when every arm has returned.
   A `with var` result is read from its record in the `then` section. */

/* The call an arm makes, under the temporaries that order its arguments. */
static const TypedNode *arm_call(const TypedProgram *program,
                                 const TypedNode *arm)
{
    const TypedNode *node = typed_program_node(program, arm->left_child);

    while (node != NULL && node->kind == TYPED_MATCH_BIND_EXPRESSION) {
        node = typed_program_node(program, node->right_child);
    }
    return node != NULL && node->kind == TYPED_FUNCTION_CALL ? node : NULL;
}

static size_t child_at(const TypedProgram *program, const TypedNode *node,
                       size_t i)
{
    return *(const size_t *)list_get(&program->children,
                                     node->children_start + i);
}

static bool field_type(const TypedProgram *program, size_t type_index,
                       char *out, size_t size, ErrorState *errors)
{
    const TypedType *type = typed_program_type(program, type_index);

    if (type != NULL && type->kind == NYX_TYPE_STORE) {
        snprintf(out, size, "nyx_store *");
        return true;
    }
    return format_c_type(program, type_index, out, size, errors);
}

/* The record an arm's thread is handed: its arguments, and its result. */
static bool emit_arm_record(const TypedProgram *program, GeneratorContext *gen,
                            const TypedNode *call, TextBuffer *source,
                            ErrorState *errors)
{
    char type[NYX_IDENTIFIER_MAX + 32];
    size_t i;

    buffer_append(source, "typedef struct {\n");
    for (i = 0; i < call->children_count; i++) {
        const TypedNode *argument =
            typed_program_node(program, child_at(program, call, i));

        if (!field_type(program, argument->type_index, type, sizeof(type),
                        errors)) {
            return false;
        }
        buffer_line(source, 1, "%s a%zu;\n", type, i);
    }
    if (analysis_has(gen->analysis, ANALYSIS_CHANNELS)) {
        buffer_append(source, "    nyx_chan_block *blk;\n");
    }
    return format_c_type(program, call->type_index, type, sizeof(type), errors);
}

/* The thread entry point: it calls the function and keeps the result. */
static void emit_arm_entry(GeneratorContext *gen, const TypedNode *call,
                           size_t arm_index, const char *callee,
                           const char *result_type, TextBuffer *source)
{
    size_t i;

    buffer_appendf(source,
                   "    %s result;\n} nyx_par_%zu;\n"
                   "static void *nyx_par_%zu_run(void *nyx_arg)\n{\n"
                   "    nyx_par_%zu *nyx_p = nyx_arg;\n"
                   "    nyx_p->result = %s(",
                   result_type, arm_index, arm_index, arm_index, callee);
    for (i = 0; i < call->children_count; i++) {
        buffer_appendf(source, "%snyx_p->a%zu", i == 0 ? "" : ", ", i);
    }
    buffer_append(source, ");\n");
    if (analysis_has(gen->analysis, ANALYSIS_CHANNELS)) {
        buffer_append(source,
                      "    /* The first instance runs on a thread already counted, so it has no\n"
                      "       block and does not report. */\n"
                      "    if (nyx_p->blk != NULL) nyx_chan_arm_finished(nyx_p->blk);\n");
    }
    buffer_append(source, "    return NULL;\n}\n\n");
}

static bool emit_trampoline(const TypedProgram *program, GeneratorContext *gen,
                            size_t arm_index, TextBuffer *source,
                            ErrorState *errors)
{
    const TypedNode *call = arm_call(program, typed_program_node(program, arm_index));
    char callee_text[USER_NAME_CAPACITY];
    char result_type[NYX_IDENTIFIER_MAX + 32];
    const char *callee;

    if (call == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    callee = checked_user_name(program, call->name_index, callee_text,
                               sizeof(callee_text), errors);
    if (callee == NULL || !emit_arm_record(program, gen, call, source, errors) ||
        !format_c_type(program, call->type_index, result_type,
                       sizeof(result_type), errors)) {
        return false;
    }
    emit_arm_entry(gen, call, arm_index, callee, result_type, source);
    return true;
}

bool emit_parallel_trampolines(const TypedProgram *program, GeneratorContext *gen, size_t root_position,
                               TextBuffer *source, ErrorState *errors)
{
    size_t count, i;
    const size_t *arms;

    if (!analysis_has(gen->analysis, ANALYSIS_PARALLEL) ||
        root_position == NO_ROOT_POSITION) {
        return true;
    }
    arms = analysis_nodes_in(gen->analysis, TYPED_PARALLEL_ARM, root_position,
                             &count);
    for (i = 0; i < count; i++) {
        if (!emit_trampoline(program, gen, arms[i], source, errors)) {
            return false;
        }
    }
    return true;
}

/* The trampolines of every root that is not a function. */
bool emit_program_trampolines(const TypedProgram *program,
                              GeneratorContext *gen, TextBuffer *source,
                              ErrorState *errors)
{
    size_t position;

    for (position = 0; position < program->roots.length; position++) {
        const TypedNode *root = typed_program_node(
            program, *(const size_t *)list_get(&program->roots, position));

        if (root != NULL && root->kind != TYPED_FUNCTION_DECLARATION &&
            !emit_parallel_trampolines(program, gen, position, source,
                                       errors)) {
            return false;
        }
    }
    return true;
}

/* Worker threads that outlive a block, so a program that opens a parallel
   block every frame does not pay thread start-up every frame. A job goes to an
   idle worker if one is free and otherwise to a new worker, so an arm that
   blocks (on a channel, say) can never keep another arm waiting in a queue. */
bool emit_worker_runtime(GeneratorContext *gen, TextBuffer *source,
                         ErrorState *errors)
{
    (void)errors;
    if (!analysis_has(gen->analysis, ANALYSIS_PARALLEL)) {
        return true;
    }
    buffer_append(
               source,
               "typedef struct nyx_job {\n"
               "    void *(*fn)(void *);\n"
               "    void *arg;\n"
               "    int done;\n"
               "    struct nyx_job *next;\n"
               "} nyx_job;\n"
               "static pthread_mutex_t nyx_w_lock = PTHREAD_MUTEX_INITIALIZER;\n"
               "static pthread_cond_t nyx_w_work = PTHREAD_COND_INITIALIZER;\n"
               "static pthread_cond_t nyx_w_done = PTHREAD_COND_INITIALIZER;\n"
               "static nyx_job *nyx_w_head, *nyx_w_tail;\n"
               "static size_t nyx_w_idle, nyx_w_pending;\n"
               "static void *nyx_worker(void *unused) {\n"
               "    (void)unused;\n"
               "    pthread_mutex_lock(&nyx_w_lock);\n"
               "    for (;;) {\n"
               "        nyx_job *job;\n"
               "        while (nyx_w_head == NULL) {\n"
               "            nyx_w_idle++;\n"
               "            pthread_cond_wait(&nyx_w_work, &nyx_w_lock);\n"
               "            nyx_w_idle--;\n"
               "        }\n"
               "        job = nyx_w_head;\n"
               "        nyx_w_head = job->next;\n"
               "        if (nyx_w_head == NULL) nyx_w_tail = NULL;\n"
               "        nyx_w_pending--;\n"
               "        pthread_mutex_unlock(&nyx_w_lock);\n"
               "        job->fn(job->arg);\n"
               "        pthread_mutex_lock(&nyx_w_lock);\n"
               "        job->done = 1;\n"
               "        pthread_cond_broadcast(&nyx_w_done);\n"
               "    }\n"
               "    return NULL;\n"
               "}\n"
               "/* Returns 0 if a thread was needed and could not be started. */\n"
               "static int nyx_worker_submit(nyx_job *job, void *(*fn)(void *), void *arg) {\n"
               "    int spawn, ok = 1;\n"
               "    job->fn = fn; job->arg = arg; job->done = 0; job->next = NULL;\n"
               "    pthread_mutex_lock(&nyx_w_lock);\n"
               "    spawn = nyx_w_idle <= nyx_w_pending;\n"
               "    if (nyx_w_tail == NULL) nyx_w_head = job; else nyx_w_tail->next = job;\n"
               "    nyx_w_tail = job;\n"
               "    nyx_w_pending++;\n"
               "    if (spawn) {\n"
               "        pthread_t thread;\n"
               "        if (pthread_create(&thread, NULL, nyx_worker, NULL) == 0) pthread_detach(thread);\n"
               "        else ok = 0;\n"
               "    } else {\n"
               "        pthread_cond_signal(&nyx_w_work);\n"
               "    }\n"
               "    pthread_mutex_unlock(&nyx_w_lock);\n"
               "    return ok;\n"
               "}\n"
               "static void nyx_worker_wait(nyx_job *job) {\n"
               "    pthread_mutex_lock(&nyx_w_lock);\n"
               "    while (!job->done) pthread_cond_wait(&nyx_w_done, &nyx_w_lock);\n"
               "    pthread_mutex_unlock(&nyx_w_lock);\n"
               "}\n\n");
    return true;
}

static void emit_failure(TextBuffer *source, size_t depth, SourcePos pos,
                         const char *message)
{
    /* The position format is C text for the generated program to print, so it
       is appended as it is, not formatted here. */
    append_indent(source, depth);
    buffer_append(source,
                  "{ fflush(stdout); fprintf(stderr, \"" MESSAGE_RUNTIME_POSITION
                  "\\n\", __FILE__, ");
    buffer_appendf(source,
                   "(size_t)%zu, (size_t)%zu, (size_t)%zu, \"%s\" + "
                   "sizeof(\"" MESSAGE_RUNTIME_PREFIX "\") - 1); "
                   "exit(1); }\n",
                   pos.line, pos.column, pos.offset, message);
}

/* One statement: `prefix` is already written; the expression and the end of
   the statement follow. */
static bool emit_expression_statement(const TypedProgram *program, size_t node,
                                      TextBuffer *source, ErrorState *errors)
{
    bool ok = emit_expression(program, node, source, errors);

    buffer_append(source, ";\n");
    return ok;
}

/* Evaluates, in order, the temporaries that sequence an arm's arguments. */
static bool emit_argument_setup(const TypedProgram *program, GeneratorContext *gen,
                                const TypedNode *arm, const TypedNode *call,
                                size_t block_index, const char *target,
                                size_t depth, TextBuffer *source,
                                ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, arm->left_child);
    size_t i;
    bool ok = true;

    while (ok && node != NULL && node->kind == TYPED_MATCH_BIND_EXPRESSION) {
        buffer_line(source, depth, "nyx_match_%zu = ",
                    (size_t)node->integer_value_u);
        ok = emit_expression_statement(program, node->left_child, source, errors);
        node = typed_program_node(program, node->right_child);
    }
    for (i = 0; ok && i < call->children_count; i++) {
        buffer_line(source, depth, "%s.a%zu = ", target, i);
        ok = emit_expression_statement(program, child_at(program, call, i),
                                       source, errors);
    }
    if (analysis_has(gen->analysis, ANALYSIS_CHANNELS)) {
        buffer_line(source, depth, "%s.blk = &nyx_blk_%zu;\n", target,
                    block_index);
    }
    return ok;
}

/* A single instance, on the stack. */
static bool emit_single_arm_setup(const TypedProgram *program,
                                  GeneratorContext *gen, const TypedNode *arm,
                                  const TypedNode *call, size_t arm_index,
                                  size_t block_index, size_t depth,
                                  TextBuffer *source, ErrorState *errors)
{
    char target[96];

    buffer_line(source, depth,
                "nyx_par_%zu nyx_par_%zu_s; nyx_par_%zu *nyx_par_%zu_v = "
                "&nyx_par_%zu_s; size_t nyx_par_%zu_n = 1;\n",
                arm_index, arm_index, arm_index, arm_index, arm_index,
                arm_index);
    snprintf(target, sizeof(target), "nyx_par_%zu_s", arm_index);
    return emit_argument_setup(program, gen, arm, call, block_index, target,
                               depth, source, errors);
}

/* The bounds of a repeated arm, evaluated once, and the records it needs. */
static bool emit_lane_bounds(const TypedProgram *program, const TypedNode *arm,
                             size_t arm_index, size_t depth, TextBuffer *source,
                             ErrorState *errors)
{
    bool ok;

    buffer_line(source, depth, "int32_t nyx_lo_%zu = ", arm_index);
    ok = emit_expression_statement(program, child_at(program, arm, 0), source,
                                   errors);
    buffer_line(source, depth, "int32_t nyx_hi_%zu = ", arm_index);
    ok = emit_expression_statement(program, child_at(program, arm, 1), source,
                                   errors) && ok;
    buffer_line(source, depth,
                "size_t nyx_par_%zu_n = nyx_hi_%zu > nyx_lo_%zu ? "
                "(size_t)((int64_t)nyx_hi_%zu - (int64_t)nyx_lo_%zu) : 0;\n",
                arm_index, arm_index, arm_index, arm_index, arm_index);
    buffer_line(source, depth,
                "nyx_par_%zu *nyx_par_%zu_v = nyx_par_%zu_n == 0 ? NULL : "
                "calloc(nyx_par_%zu_n, sizeof(nyx_par_%zu));\n",
                arm_index, arm_index, arm_index, arm_index, arm_index);
    buffer_line(source, depth,
                "if (nyx_par_%zu_n != 0 && nyx_par_%zu_v == NULL)\n", arm_index,
                arm_index);
    emit_failure(source, depth + 1, arm->pos, MESSAGE_RUNTIME_OUT_OF_MEMORY);
    return ok;
}

/* A repeated arm: one record per lane, the lane variable bound in each. */
static bool emit_repeated_arm_setup(const TypedProgram *program,
                                    GeneratorContext *gen, const TypedNode *arm,
                                    const TypedNode *call, size_t arm_index,
                                    size_t block_index, size_t depth,
                                    TextBuffer *source, ErrorState *errors)
{
    char lane_text[USER_NAME_CAPACITY];
    char target[96];
    const char *lane = checked_user_name(program, arm->second_name_index,
                                         lane_text, sizeof(lane_text), errors);
    bool ok;

    if (lane == NULL ||
        !emit_lane_bounds(program, arm, arm_index, depth, source, errors)) {
        return false;
    }
    buffer_line(source, depth,
                "for (size_t nyx_j_%zu = 0; nyx_j_%zu < nyx_par_%zu_n; nyx_j_%zu++) {\n",
                arm_index, arm_index, arm_index, arm_index);
    buffer_line(source, depth + 1,
                "int32_t %s = (int32_t)((int64_t)nyx_lo_%zu + (int64_t)nyx_j_%zu); "
                "(void)%s;\n",
                lane, arm_index, arm_index, lane);
    snprintf(target, sizeof(target), "nyx_par_%zu_v[nyx_j_%zu]", arm_index,
             arm_index);
    ok = emit_argument_setup(program, gen, arm, call, block_index, target,
                             depth + 1, source, errors);
    buffer_line(source, depth, "}\n");
    return ok;
}

static bool emit_arm_setup(const TypedProgram *program, GeneratorContext *gen,
                           size_t arm_index, size_t block_index, size_t depth,
                           TextBuffer *source, ErrorState *errors)
{
    const TypedNode *arm = typed_program_node(program, arm_index);
    const TypedNode *call = arm_call(program, arm);

    if (call == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    if (arm->children_count == 0) {
        return emit_single_arm_setup(program, gen, arm, call, arm_index,
                                     block_index, depth, source, errors);
    }
    return emit_repeated_arm_setup(program, gen, arm, call, arm_index,
                                   block_index, depth, source, errors);
}

/* The pool Store: its capacity is evaluated once, then it is entered like a
   scope so every exit releases it. */
static bool emit_pool_header(const TypedProgram *program, size_t node_index,
                             const TypedNode *node, size_t depth,
                             TextBuffer *source, ErrorState *errors)
{
    char name_text[USER_NAME_CAPACITY];
    const char *name = checked_user_name(program, node->name_index, name_text,
                                         sizeof(name_text), errors);
    bool ok;

    if (name == NULL) {
        return false;
    }
    buffer_line(source, depth, "int32_t nyx_pool_capacity_%zu = ", node_index);
    ok = emit_expression_statement(program, node->left_child, source, errors);
    buffer_line(source, depth,
                "nyx_pool_enter(&nyx_scope_top, &nyx_scope_frame_%zu, "
                "nyx_pool_capacity_%zu, __FILE__, %zu, %zu, %zu);\n",
                node_index, node_index, node->pos.line, node->pos.column,
                node->pos.offset);
    buffer_line(source, depth,
                "nyx_store *%s = &nyx_scope_frame_%zu.store; (void)%s;\n", name,
                node_index, name);
    return ok;
}

/* The record count, the thread table, and the first instance, which runs on
   the calling thread. */
static void emit_launch_prelude(const TypedProgram *program, size_t node_index,
                                const TypedNode *node, size_t depth,
                                TextBuffer *source)
{
    size_t i;

    buffer_line(source, depth, "size_t nyx_total_%zu = 0, nyx_started_%zu = 0;\n",
                node_index, node_index);
    buffer_line(source, depth,
                "void *(*nyx_inline_fn_%zu)(void *) = NULL; "
                "void *nyx_inline_arg_%zu = NULL; "
                "nyx_job *nyx_jobs_%zu = NULL;\n",
                node_index, node_index, node_index);
    for (i = 0; i < node->children_count; i++) {
        buffer_line(source, depth, "nyx_total_%zu += nyx_par_%zu_n;\n",
                    node_index, child_at(program, node, i));
    }
    buffer_line(source, depth,
                "if (nyx_total_%zu > 1) { nyx_jobs_%zu = malloc("
                "(nyx_total_%zu > 1 ? nyx_total_%zu - 1 : 1) * sizeof(nyx_job));\n",
                node_index, node_index, node_index, node_index);
    buffer_line(source, depth + 1, "if (nyx_jobs_%zu == NULL)\n", node_index);
    emit_failure(source, depth + 2, node->pos, MESSAGE_RUNTIME_OUT_OF_MEMORY);
    buffer_line(source, depth, "}\n");
}

/* Starts every instance of one arm, keeping the first for the caller. */
static void emit_arm_launch(size_t node_index, size_t arm, SourcePos pos,
                            size_t depth, bool track, TextBuffer *source)
{
    char started[64] = "";
    char inline_block[96] = "";

    if (track) {
        snprintf(started, sizeof(started), "nyx_chan_arm_started(&nyx_blk_%zu), ",
                 node_index);
        snprintf(inline_block, sizeof(inline_block),
                 " nyx_par_%zu_v[nyx_i].blk = NULL;", arm);
    }
    buffer_line(
        source, depth,
        "for (size_t nyx_i = 0; nyx_i < nyx_par_%zu_n; nyx_i++) {\n"
        "        if (nyx_inline_fn_%zu == NULL) { nyx_inline_fn_%zu = "
        "nyx_par_%zu_run; nyx_inline_arg_%zu = &nyx_par_%zu_v[nyx_i];%s }\n"
        "        else if ((%snyx_worker_submit(&nyx_jobs_%zu[nyx_started_%zu], "
        "nyx_par_%zu_run, &nyx_par_%zu_v[nyx_i]))) "
        "nyx_started_%zu++;\n        else\n",
        arm, node_index, node_index, arm, node_index, arm, inline_block,
        started, node_index, node_index, arm, arm, node_index);
    emit_failure(source, depth + 3, pos, MESSAGE_RUNTIME_THREAD_START);
    buffer_line(source, depth, "}\n");
}

/* Runs the inline instance, then waits for every thread that was started. */
static void emit_join(size_t node_index, size_t depth, bool track,
                      TextBuffer *source)
{
    buffer_line(source, depth,
                "if (nyx_inline_fn_%zu != NULL) "
                "nyx_inline_fn_%zu(nyx_inline_arg_%zu);\n",
                node_index, node_index, node_index);
    if (track) {
        buffer_line(source, depth, "nyx_chan_join_begin(&nyx_blk_%zu);\n",
                    node_index);
    }
    buffer_line(source, depth,
                "for (size_t nyx_i = 0; nyx_i < nyx_started_%zu; nyx_i++)\n"
                "        nyx_worker_wait(&nyx_jobs_%zu[nyx_i]);\n",
                node_index, node_index);
    if (track) {
        buffer_line(source, depth, "nyx_chan_join_end(&nyx_blk_%zu);\n",
                    node_index);
    }
    buffer_line(source, depth, "free(nyx_jobs_%zu);\n", node_index);
}

/* A repeated arm's results, copied into the Array it fills in lane order. */
static void emit_lane_results(const TypedNode *arm, size_t arm_index,
                              const char *name, size_t depth,
                              TextBuffer *source)
{
    buffer_line(source, depth, "if (nyx_par_%zu_n > %zuu) {\n", arm_index,
                (size_t)arm->integer_value_u);
    emit_failure(source, depth + 1, arm->pos, MESSAGE_RUNTIME_PARALLEL_RESULTS);
    buffer_line(source, depth, "}\n");
    buffer_line(source, depth,
                "for (size_t nyx_k_%zu = 0; nyx_k_%zu < nyx_par_%zu_n; "
                "nyx_k_%zu++) %s.data[nyx_k_%zu] = "
                "nyx_par_%zu_v[nyx_k_%zu].result;\n",
                arm_index, arm_index, arm_index, arm_index, name, arm_index,
                arm_index, arm_index);
    buffer_line(source, depth, "%s.length = (uint32_t)nyx_par_%zu_n;\n", name,
                arm_index);
}

/* After the join: copy each assigned result into its variable, then free a
   repeated arm's records. */
static bool emit_arm_finish(const TypedProgram *program, size_t arm_index,
                            size_t depth, TextBuffer *source,
                            ErrorState *errors)
{
    const TypedNode *arm = typed_program_node(program, arm_index);
    char name_text[USER_NAME_CAPACITY];
    const char *name = arm->integer_value == 1
                           ? checked_user_name(program, arm->name_index,
                                               name_text, sizeof(name_text),
                                               errors)
                           : NULL;
    bool repeated = arm->children_count != 0;

    if (arm->integer_value == 1 && name == NULL) {
        return false;
    }
    if (name != NULL && repeated) {
        emit_lane_results(arm, arm_index, name, depth, source);
    } else if (name != NULL) {
        buffer_line(source, depth, "%s = nyx_par_%zu_v[0].result;\n", name,
                    arm_index);
    }
    if (repeated) {
        buffer_line(source, depth, "free(nyx_par_%zu_v);\n", arm_index);
    }
    return true;
}

static bool emit_arm_loop(const TypedProgram *program, GeneratorContext *gen,
                          size_t node_index, const TypedNode *node, size_t depth,
                          TextBuffer *source, ErrorState *errors, bool launch)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        size_t arm = child_at(program, node, i);

        if (launch) {
            emit_arm_launch(node_index, arm, node->pos, depth,
                            analysis_has(gen->analysis, ANALYSIS_CHANNELS),
                            source);
        } else if (!emit_arm_finish(program, arm, depth, source, errors)) {
            return false;
        }
    }
    return true;
}

/* The statements of the setup block, then every arm's arguments. */
static bool emit_parallel_setup(const TypedProgram *program,
                                GeneratorContext *gen, size_t node_index,
                                const TypedNode *node, size_t depth,
                                const char *effective_source,
                                TextBuffer *source, ErrorState *errors)
{
    const TypedNode *setup = node->setup_block == TYPED_NO_NODE
                                 ? NULL
                                 : typed_program_node(program, node->setup_block);
    size_t count = setup == NULL ? 0 : setup->children_count;
    size_t i;

    for (i = 0; i < count; i++) {
        if (!emit_statement(program, gen, child_at(program, setup, i), depth,
                            effective_source, source, errors)) {
            return false;
        }
    }
    for (i = 0; i < node->children_count; i++) {
        if (!emit_arm_setup(program, gen, child_at(program, node, i),
                            node_index, depth, source, errors)) {
            return false;
        }
    }
    return true;
}

/* The launch, the join, then each result. */
static bool emit_parallel_run(const TypedProgram *program, GeneratorContext *gen,
                              size_t node_index, const TypedNode *node,
                              size_t depth, TextBuffer *source,
                              ErrorState *errors)
{
    bool track = analysis_has(gen->analysis, ANALYSIS_CHANNELS);

    emit_launch_prelude(program, node_index, node, depth, source);
    return emit_arm_loop(program, gen, node_index, node, depth, source, errors,
                         true) &&
           (emit_join(node_index, depth, track, source), true) &&
           emit_arm_loop(program, gen, node_index, node, depth, source, errors,
                         false);
}

/* The statements the block runs: its setup, its arms, then its then section,
   and the Store the pool releases. */
static bool emit_parallel_body(const TypedProgram *program,
                               GeneratorContext *gen, size_t node_index,
                               const TypedNode *node, size_t depth,
                               const char *effective_source,
                               TextBuffer *source, ErrorState *errors)
{
    bool pool = node->integer_value == 1;

    return (!pool || emit_pool_header(program, node_index, node, depth, source,
                                      errors)) &&
           emit_parallel_setup(program, gen, node_index, node, depth,
                               effective_source, source, errors) &&
           emit_parallel_run(program, gen, node_index, node, depth, source,
                             errors) &&
           (node->right_child == TYPED_NO_NODE ||
            emit_block_in_scope(program, gen, node->right_child, depth,
                                effective_source, source, errors,
                                pool ? node_index : TYPED_NO_NODE));
}

bool emit_parallel_statement(const TypedProgram *program, GeneratorContext *gen,
                             size_t node_index, size_t depth,
                             const char *effective_source, TextBuffer *source,
                             ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    bool pool = node->integer_value == 1;
    bool ok;

    buffer_line(source, depth, "{\n");
    depth++;
    if (analysis_has(gen->analysis, ANALYSIS_CHANNELS)) {
        buffer_line(source, depth, "nyx_chan_block nyx_blk_%zu = {0, 0};\n",
                    node_index);
    }
    ok = emit_parallel_body(program, gen, node_index, node, depth,
                            effective_source, source, errors);
    if (ok && pool) {
        buffer_line(source, depth,
                    "nyx_scope_cleanup(&nyx_scope_top, "
                    "nyx_scope_frame_%zu.previous);\n",
                    node_index);
    }
    buffer_line(source, depth - 1, "}\n");
    return ok;
}
