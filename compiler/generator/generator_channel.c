#include "generator_internal.h"

/* Channels. Every channel is static storage: a ring buffer, and a record in one
   table. All channel state is guarded by a single process-wide lock and one
   condition variable, so a `when` can wait on several channels at once and the
   deadlock accounting sees every thread. A timer is a channel with an interval
   and no buffer: it is ready when its next tick is due. */

static bool out_of_memory(ErrorState *errors)
{
    if (!errors->reported) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
    }
    return false;
}

static const TypedNode *channel_declaration(const TypedProgram *program,
                                            size_t id)
{
    size_t i;

    for (i = 0; i < program->roots.length; i++) {
        const TypedNode *node = typed_program_node(
            program, *(const size_t *)list_get(&program->roots, i));

        if (node != NULL && node->kind == TYPED_CHANNEL_DECLARATION &&
            node->store_id == id) {
            return node;
        }
    }
    return NULL;
}

static size_t channel_count(const TypedProgram *program)
{
    size_t i, count = 0;

    for (i = 0; i < program->roots.length; i++) {
        const TypedNode *node = typed_program_node(
            program, *(const size_t *)list_get(&program->roots, i));

        if (node != NULL && node->kind == TYPED_CHANNEL_DECLARATION) count++;
    }
    return count;
}

/* The tags of the value and Nil alternatives of `element | Nil`. */
static bool optional_tags(const TypedProgram *program, size_t optional,
                          size_t *value_tag, size_t *nil_tag)
{
    const TypedType *type = typed_program_type(program, optional);
    size_t i;

    if (type == NULL || type->kind != NYX_TYPE_UNION || type->child_count != 2) {
        return false;
    }
    for (i = 0; i < type->child_count; i++) {
        const TypedType *alternative = union_alternative_type(program, type, i);

        if (alternative == NULL) return false;
        if (alternative->kind == NYX_TYPE_NIL) {
            *nil_tag = i;
        } else {
            *value_tag = i;
        }
    }
    return true;
}

/* The ring size of a channel declaration. */
static int channel_capacity(const TypedNode *declaration)
{
    return declaration->integer_value > 0 ? declaration->integer_value : 1;
}

static bool emit_channel_buffers(const TypedProgram *program,
                                 TextBuffer *source, size_t count,
                                 ErrorState *errors)
{
    size_t id;

    for (id = 0; id < count; id++) {
        const TypedNode *declaration = channel_declaration(program, id);
        char type[NYX_IDENTIFIER_MAX + 32];

        if (!format_c_type(program, declaration->type_index, type, sizeof(type),
                           errors)) {
            return false;
        }
        buffer_appendf(source, "static %s nyx_chan_%zu_buf[%d];\n", type, id,
                       channel_capacity(declaration));
    }
    return true;
}

/* The storage of every channel and the table that names it. */
static bool emit_channel_storage(const TypedProgram *program, TextBuffer *source,
                                 ErrorState *errors)
{
    size_t count = channel_count(program), id;

    buffer_append(source,
                  "typedef struct {\n"
                  "    unsigned char *buf;\n"
                  "    size_t elem, cap, head, count, taken;\n"
                  "    int closed;\n"
                  "    int rendezvous;   /* a send waits until the value is taken */\n"
                  "    int64_t interval; /* ms between ticks; 0 for a queue */\n"
                  "    int64_t next_due; /* a timer's next tick; 0 until first used */\n"
                  "} nyx_chan;\n\n");
    if (!emit_channel_buffers(program, source, count, errors)) {
        return false;
    }
    if (count == 0) {
        return true;
    }
    buffer_appendf(source, "static nyx_chan nyx_chans[%zu] = {\n", count);
    for (id = 0; id < count; id++) {
        const TypedNode *declaration = channel_declaration(program, id);

        buffer_appendf(
            source,
            "    { (unsigned char *)nyx_chan_%zu_buf, sizeof(nyx_chan_%zu_buf[0]), "
            "%d, 0, 0, 0, 0, %d, (int64_t)%lluULL, 0 },\n",
            id, id, channel_capacity(declaration),
            declaration->integer_value == 0 ? 1 : 0,
            (unsigned long long)declaration->integer_value_u);
    }
    buffer_append(source, "};\n\n");
    return true;
}

static bool emit_channel_core(TextBuffer *source, ErrorState *errors)
{
    if (buffer_append(
               source,
               "static pthread_mutex_t nyx_chan_lock = PTHREAD_MUTEX_INITIALIZER;\n"
               "static pthread_cond_t nyx_chan_cond;\n"
               "static pthread_once_t nyx_chan_once = PTHREAD_ONCE_INIT;\n"
               "/* Threads alive, threads waiting on a channel since the last state\n"
               "   change, and threads waiting for the arms of a block to end. */\n"
               "static size_t nyx_chan_live = 1, nyx_chan_blocked = 0, nyx_chan_joining = 0;\n"
               "static size_t nyx_chan_epoch = 0;\n"
               "static void nyx_chan_init(void) {\n"
               "    pthread_condattr_t attr;\n"
               "    pthread_condattr_init(&attr);\n"
               "    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);\n"
               "    pthread_cond_init(&nyx_chan_cond, &attr);\n"
               "    pthread_condattr_destroy(&attr);\n"
               "}\n"
               "static inline void nyx_chan_acquire(void) {\n"
               "    pthread_once(&nyx_chan_once, nyx_chan_init);\n"
               "    pthread_mutex_lock(&nyx_chan_lock);\n"
               "}\n"
               "static inline void nyx_chan_release(void) { pthread_mutex_unlock(&nyx_chan_lock); }\n"
               "static inline int64_t nyx_chan_now(void) {\n"
               "    struct timespec ts;\n"
               "    clock_gettime(CLOCK_MONOTONIC, &ts);\n"
               "    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);\n"
               "}\n"
               "/* Every thread that is alive is waiting, and one waits on a channel:\n"
               "   nothing can ever wake them. */\n"
               "static inline void nyx_chan_check(const char *file, size_t line, size_t column, size_t pos) {\n"
               "    if (nyx_chan_blocked > 0 && nyx_chan_blocked + nyx_chan_joining >= nyx_chan_live) {\n"
               "        fflush(stdout);\n"
               "        if (file != NULL) {\n"
               "            fprintf(stderr, \"" MESSAGE_RUNTIME_POSITION "\\n\", file, line, column, pos,\n"
               "                    \"" MESSAGE_RUNTIME_DEADLOCK "\" + sizeof(\"" MESSAGE_RUNTIME_PREFIX "\") - 1);\n"
               "        } else {\n"
               "            fprintf(stderr, \"" MESSAGE_RUNTIME_DEADLOCK "\\n\");\n"
               "        }\n"
               "        exit(1);\n"
               "    }\n"
               "}\n"
               "static inline void nyx_chan_wake(void) {\n"
               "    nyx_chan_epoch++;\n"
               "    nyx_chan_blocked = 0;\n"
               "    pthread_cond_broadcast(&nyx_chan_cond);\n"
               "}\n"
               "/* Waits for a state change, or until `due` (a monotonic millisecond\n"
               "   reading) when a timer is involved. A timed wait wakes by itself, so\n"
               "   it is never counted as blocked. */\n"
               "static inline void nyx_chan_wait(int64_t due, const char *file, size_t line, size_t column, size_t pos) {\n"
               "    if (due == INT64_MAX) {\n"
               "        size_t epoch = nyx_chan_epoch;\n"
               "        nyx_chan_blocked++;\n"
               "        nyx_chan_check(file, line, column, pos);\n"
               "        pthread_cond_wait(&nyx_chan_cond, &nyx_chan_lock);\n"
               "        if (epoch == nyx_chan_epoch) nyx_chan_blocked--;\n"
               "    } else {\n"
               "        struct timespec ts;\n"
               "        ts.tv_sec = (time_t)(due / 1000);\n"
               "        ts.tv_nsec = (long)(due % 1000) * 1000000L;\n"
               "        pthread_cond_timedwait(&nyx_chan_cond, &nyx_chan_lock, &ts);\n"
               "    }\n"
               "}\n"
               ) &&
           buffer_append(
               source,
               "static inline void nyx_chan_prime(nyx_chan *c) {\n"
               "    if (c->interval > 0 && c->next_due == 0) c->next_due = nyx_chan_now() + c->interval;\n"
               "}\n"
               "static inline int nyx_chan_ready_recv(nyx_chan *c) {\n"
               "    nyx_chan_prime(c);\n"
               "    if (c->interval > 0) return c->closed || nyx_chan_now() >= c->next_due;\n"
               "    return c->count > 0 || c->closed;\n"
               "}\n"
               "static inline int nyx_chan_ready_send(nyx_chan *c) {\n"
               "    return c->interval > 0 || c->closed || c->count < c->cap;\n"
               "}\n"
               "static inline int64_t nyx_chan_due(nyx_chan *c) {\n"
               "    nyx_chan_prime(c);\n"
               "    return c->interval > 0 && !c->closed ? c->next_due : INT64_MAX;\n"
               "}\n"
               "/* With the lock held. A tick carries the time it was due; ticks nobody\n"
               "   took are skipped, so a slow receiver sees the latest and no backlog. */\n"
               "static inline int nyx_chan_take(nyx_chan *c, void *out) {\n"
               "    if (c->interval > 0) {\n"
               "        int64_t now;\n"
               "        if (c->closed) return 0;\n"
               "        now = nyx_chan_now();\n"
               "        if (now < c->next_due) return 0;\n"
               "        *(int64_t *)out = c->next_due;\n"
               "        c->next_due += c->interval * ((now - c->next_due) / c->interval + 1);\n"
               "        return 1;\n"
               "    }\n"
               "    if (c->count == 0) return 0;\n"
               "    memcpy(out, c->buf + c->head * c->elem, c->elem);\n"
               "    c->head = (c->head + 1) % c->cap;\n"
               "    c->count--;\n"
               "    c->taken++;\n"
               "    nyx_chan_wake();\n"
               "    return 1;\n"
               "}\n"
               "static inline int nyx_chan_put(nyx_chan *c, const void *value) {\n"
               "    if (c->interval > 0 || c->closed || c->count == c->cap) return 0;\n"
               "    memcpy(c->buf + ((c->head + c->count) % c->cap) * c->elem, value, c->elem);\n"
               "    c->count++;\n"
               "    nyx_chan_wake();\n"
               "    return 1;\n"
               "}\n"
               ) &&
           buffer_append(
               source,
               "static inline int nyx_chan_recv(nyx_chan *c, void *out, const char *file, size_t line, size_t column, size_t pos) {\n"
               "    int got;\n"
               "    nyx_chan_acquire();\n"
               "    while (!nyx_chan_ready_recv(c)) nyx_chan_wait(nyx_chan_due(c), file, line, column, pos);\n"
               "    got = nyx_chan_take(c, out);\n"
               "    nyx_chan_release();\n"
               "    return got;\n"
               "}\n"
               "static inline int nyx_chan_try_recv(nyx_chan *c, void *out) {\n"
               "    int got;\n"
               "    nyx_chan_acquire();\n"
               "    got = nyx_chan_ready_recv(c) ? nyx_chan_take(c, out) : 0;\n"
               "    nyx_chan_release();\n"
               "    return got;\n"
               "}\n"
               "static inline int nyx_chan_send(nyx_chan *c, const void *value, const char *file, size_t line, size_t column, size_t pos) {\n"
               "    int sent;\n"
               "    nyx_chan_acquire();\n"
               "    while (!nyx_chan_ready_send(c)) nyx_chan_wait(INT64_MAX, file, line, column, pos);\n"
               "    sent = nyx_chan_put(c, value);\n"
               "    if (sent && c->rendezvous) {\n"
               "        /* The value is parked; the send ends when a receiver has taken it,\n"
               "           or is withdrawn if the channel closes first. */\n"
               "        size_t parked = c->taken;\n"
               "        while (c->taken == parked && !c->closed) nyx_chan_wait(INT64_MAX, file, line, column, pos);\n"
               "        if (c->taken == parked) { c->count = 0; sent = 0; }\n"
               "    }\n"
               "    nyx_chan_release();\n"
               "    return sent;\n"
               "}\n"
               "static inline int nyx_chan_try_send(nyx_chan *c, const void *value) {\n"
               "    int sent;\n"
               "    nyx_chan_acquire();\n"
               "    sent = nyx_chan_ready_send(c) ? nyx_chan_put(c, value) : 0;\n"
               "    nyx_chan_release();\n"
               "    return sent;\n"
               "}\n"
               "static inline void nyx_chan_close(nyx_chan *c) {\n"
               "    nyx_chan_acquire();\n"
               "    c->closed = 1;\n"
               "    /* A rendezvous value no receiver has taken is withdrawn, so its sender\n"
               "       gets false rather than racing a drain. */\n"
               "    if (c->rendezvous) c->count = 0;\n"
               "    nyx_chan_wake();\n"
               "    nyx_chan_release();\n"
               "}\n"
               "static inline int nyx_chan_closed(nyx_chan *c) {\n"
               "    int result;\n"
               "    nyx_chan_acquire();\n"
               "    result = c->closed && (c->interval > 0 || c->count == 0);\n"
               "    nyx_chan_release();\n"
               "    return result;\n"
               "}\n\n")) {
        return true;
    }
    return out_of_memory(errors);
}

/* The arms of a parallel block are threads the accounting must know about, and
   the thread that waits for them counts as waiting only while one is unfinished. */
static bool emit_channel_blocks(TextBuffer *source, ErrorState *errors)
{
    return buffer_append(
               source,
               "typedef struct { size_t unfinished; int parent_joining; } nyx_chan_block;\n"
               "static inline void nyx_chan_arm_started(nyx_chan_block *block) {\n"
               "    nyx_chan_acquire();\n"
               "    nyx_chan_live++;\n"
               "    block->unfinished++;\n"
               "    nyx_chan_release();\n"
               "}\n"
               "static inline void nyx_chan_arm_finished(nyx_chan_block *block) {\n"
               "    nyx_chan_acquire();\n"
               "    nyx_chan_live--;\n"
               "    block->unfinished--;\n"
               "    if (block->unfinished == 0 && block->parent_joining) {\n"
               "        nyx_chan_joining--;\n"
               "        block->parent_joining = 0;\n"
               "    }\n"
               "    nyx_chan_check(NULL, 0, 0, 0);\n"
               "    nyx_chan_release();\n"
               "}\n"
               "static inline void nyx_chan_join_begin(nyx_chan_block *block) {\n"
               "    nyx_chan_acquire();\n"
               "    if (block->unfinished > 0) {\n"
               "        block->parent_joining = 1;\n"
               "        nyx_chan_joining++;\n"
               "        nyx_chan_check(NULL, 0, 0, 0);\n"
               "    }\n"
               "    nyx_chan_release();\n"
               "}\n"
               "static inline void nyx_chan_join_end(nyx_chan_block *block) {\n"
               "    nyx_chan_acquire();\n"
               "    if (block->parent_joining) {\n"
               "        nyx_chan_joining--;\n"
               "        block->parent_joining = 0;\n"
               "    }\n"
               "    nyx_chan_release();\n"
               "}\n\n") ||
           out_of_memory(errors);
}

/* The typed face of one element type, shared by every channel that carries it:
   turning a received value into `T | Nil`, and the send and receive entry points
   an expression calls with the channel it acts on. */
static bool emit_channel_typed(const TypedProgram *program, size_t element,
                               size_t optional_type, TextBuffer *source,
                               ErrorState *errors)
{
    char type[NYX_IDENTIFIER_MAX + 32], optional[NYX_IDENTIFIER_MAX + 32];
    size_t value_tag = 0, nil_tag = 0;

    if (!optional_tags(program, optional_type, &value_tag, &nil_tag) ||
        !format_c_type(program, element, type, sizeof(type), errors) ||
        !format_c_type(program, optional_type, optional, sizeof(optional),
                       errors)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        }
        return false;
    }
    if (!buffer_appendf(
            source,
            "static inline %s nyx_chan_t%zu_wrap(int got, const %s *value) {\n"
            "    %s result;\n"
            "    if (got) result = (%s){ .tag = %zu, .value.v%zu = *value };\n"
            "    else result = (%s){ .tag = %zu };\n"
            "    return result;\n"
            "}\n",
            optional, element, type, optional, optional, value_tag, value_tag,
            optional, nil_tag) ||
        !buffer_appendf(
            source,
            "static inline %s nyx_chan_t%zu_recv(nyx_chan *c, int blocking, "
            "const char *file, size_t line, size_t column, size_t pos) {\n"
            "    %s value;\n"
            "    int got;\n"
            "    memset(&value, 0, sizeof(value));\n"
            "    got = blocking ? nyx_chan_recv(c, &value, file, line, column, pos)\n"
            "                   : nyx_chan_try_recv(c, &value);\n"
            "    return nyx_chan_t%zu_wrap(got, &value);\n"
            "}\n",
            optional, element, type, element) ||
        !buffer_appendf(
            source,
            "static inline bool nyx_chan_t%zu_send(nyx_chan *c, %s value, int blocking, "
            "const char *file, size_t line, size_t column, size_t pos) {\n"
            "    return (blocking ? nyx_chan_send(c, &value, file, line, column, pos)\n"
            "                     : nyx_chan_try_send(c, &value)) != 0;\n"
            "}\n\n",
            element, type)) {
        return out_of_memory(errors);
    }
    return true;
}

/* Channels made in a pool: the state and the buffer are carved from the pool,
   which never moves, and a Channel value is the pool, the offset of the state,
   and the pool's generation, checked on every use. */
static bool emit_channel_values(GeneratorContext *gen, TextBuffer *source,
                                ErrorState *errors)
{
    bool pool = analysis_has(gen->analysis, ANALYSIS_POOL);

    (void)errors;
    if (!analysis_has(gen->analysis, ANALYSIS_CHANNEL_VALUES)) {
        return true;
    }
    buffer_append(
        source,
        "static inline nyx_chanref nyx_chan_create(nyx_store *store, "
        "int32_t capacity, size_t elem, size_t align,\n"
        "                                    const char *file, size_t line, "
        "size_t column, size_t pos) {\n"
        "    nyx_chanref ref;\n"
        "    nyx_chan *c;\n"
        "    size_t buffer, state;\n"
        "    memset(&ref, 0, sizeof(ref));\n");
    buffer_append(source,
                  pool ? "    if (store == NULL || !store->pool)\n"
                         "        nyx_panic_at(\"" MESSAGE_RUNTIME_CHANNEL_POOL
                         "\", file, line, column, pos);\n"
                       : "    nyx_panic_at(\"" MESSAGE_RUNTIME_CHANNEL_POOL
                         "\", file, line, column, pos);\n"
                         "    (void)store;\n");
    buffer_appendf(
        source,
        "    if (capacity < 1 || capacity > %d || elem == 0 ||\n"
        "        (size_t)capacity > (size_t)%d / elem)\n"
        "        nyx_panic_at(\"" MESSAGE_RUNTIME_CHANNEL_CAPACITY
        "\", file, line, column, pos);\n",
        NYX_MAX_CHANNEL_CAPACITY, NYX_MAX_CHANNEL_BYTES);
    buffer_append(
        source,
        pool ? "    buffer = nyx_pool_take(store, (size_t)capacity * elem, align, "
               "file, line, column, pos);\n"
               "    state = nyx_pool_take(store, sizeof(nyx_chan), _Alignof(nyx_chan), "
               "file, line, column, pos);\n"
               "    c = (nyx_chan *)(void *)(store->data + state);\n"
               "    memset(c, 0, sizeof(*c));\n"
               "    c->buf = store->data + buffer;\n"
               "    c->elem = elem;\n"
               "    c->cap = (size_t)capacity;\n"
               "    ref.store = store;\n"
               "    ref.offset = state;\n"
               "    ref.generation = store->generation;\n"
             : "    (void)buffer; (void)state; (void)c; (void)align;\n");
    buffer_append(
        source,
        "    return ref;\n"
        "}\n"
        "static inline nyx_chan *nyx_chanref_get(nyx_chanref ref, "
        "const char *file, size_t line, size_t column, size_t pos) {\n"
        "    if (ref.store == NULL)\n"
        "        nyx_panic_at(\"" MESSAGE_RUNTIME_STORE_RANGE
        "\", file, line, column, pos);\n"
        "    if (ref.store->generation != ref.generation)\n"
        "        nyx_panic_at(\"" MESSAGE_RUNTIME_STALE_HANDLE
        "\", file, line, column, pos);\n"
        "    return (nyx_chan *)(void *)(ref.store->data + ref.offset);\n"
        "}\n\n");
    return true;
}

/* Whether `element` is already among the element types done. */
static bool wrapper_done(const List *done, size_t element)
{
    size_t j;

    for (j = 0; j < done->length; j++) {
        if (*(const size_t *)list_get(done, j) == element) {
            return true;
        }
    }
    return false;
}

/* The channel declarations and creations, walked together in node order, so the
   wrappers come out in the order the channels were written. */
typedef struct {
    const size_t *declarations;
    size_t declared;
    size_t next_declared;
    const size_t *creations;
    size_t created;
    size_t next_created;
} ChannelNodes;

static bool channel_nodes_next(ChannelNodes *nodes, size_t *index,
                               bool *from_declaration)
{
    if (nodes->next_declared >= nodes->declared &&
        nodes->next_created >= nodes->created) {
        return false;
    }
    *from_declaration =
        nodes->next_created >= nodes->created ||
        (nodes->next_declared < nodes->declared &&
         nodes->declarations[nodes->next_declared] <
             nodes->creations[nodes->next_created]);
    *index = *from_declaration ? nodes->declarations[nodes->next_declared++]
                               : nodes->creations[nodes->next_created++];
    return true;
}

/* Every element type a channel carries, once: the wrappers are keyed by it. */
static bool emit_channel_wrappers(const TypedProgram *program, GeneratorContext *gen, TextBuffer *source,
                                  ErrorState *errors)
{
    List done;
    ChannelNodes nodes = {0};
    size_t index;
    bool from_declaration;
    bool ok = true;

    nodes.declarations = analysis_nodes(gen->analysis, TYPED_CHANNEL_DECLARATION,
                                        &nodes.declared);
    nodes.creations = analysis_nodes(gen->analysis, TYPED_CHANNEL_CREATE,
                                     &nodes.created);
    list_init(&done, sizeof(size_t));
    while (ok && channel_nodes_next(&nodes, &index, &from_declaration)) {
        const TypedNode *node = typed_program_node(program, index);
        size_t element = from_declaration ? node->type_index
                                          : node->auxiliary_type;
        size_t optional = from_declaration ? node->auxiliary_type
                                           : (size_t)node->integer_value_u;

        if (!wrapper_done(&done, element)) {
            ok = list_push(&done, &element) &&
                 emit_channel_typed(program, element, optional, source, errors);
        }
    }
    list_free(&done);
    return ok;
}

bool emit_channel_runtime(const TypedProgram *program, GeneratorContext *gen, TextBuffer *source,
                          ErrorState *errors)
{
    if (!emit_channel_storage(program, source, errors) ||
        !emit_channel_core(source, errors)) {
        return false;
    }
    if (analysis_has(gen->analysis, ANALYSIS_PARALLEL) && !emit_channel_blocks(source, errors)) {
        return false;
    }
    return emit_channel_values(gen, source, errors) &&
           emit_channel_wrappers(program, gen, source, errors);
}

/* The channel an operation acts on: a global's table entry, or the Channel
   value resolved against its pool. */
static bool emit_channel_pointer(const TypedProgram *program,
                                 const TypedNode *node, TextBuffer *buffer,
                                 ErrorState *errors)
{
    bool ok = true;

    if (node->store_id != TYPED_NO_NODE) {
        return buffer_appendf(buffer, "(&nyx_chans[%zu])", node->store_id);
    }
    buffer_append(buffer, "nyx_chanref_get(");
    ok = emit_expression(program, node->right_child, buffer, errors);
    buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu)", node->pos.line,
                   node->pos.column, node->pos.offset);
    return ok;
}

static bool emit_op_send(const TypedProgram *program, const TypedNode *node,
                         TextBuffer *buffer, ErrorState *errors)
{
    bool ok;

    buffer_appendf(buffer, "nyx_chan_t%zu_send(", node->auxiliary_type);
    ok = emit_channel_pointer(program, node, buffer, errors);
    buffer_append(buffer, ", ");
    ok = emit_expression(program, node->left_child, buffer, errors) && ok;
    buffer_appendf(buffer, ", %d, __FILE__, %zu, %zu, %zu)",
                   node->integer_value == CHANNEL_OP_SEND ? 1 : 0,
                   node->pos.line, node->pos.column, node->pos.offset);
    return ok;
}

static bool emit_op_recv(const TypedProgram *program, const TypedNode *node,
                         TextBuffer *buffer, ErrorState *errors)
{
    bool ok;

    buffer_appendf(buffer, "nyx_chan_t%zu_recv(", node->auxiliary_type);
    ok = emit_channel_pointer(program, node, buffer, errors);
    buffer_appendf(buffer, ", %d, __FILE__, %zu, %zu, %zu)",
                   node->integer_value == CHANNEL_OP_RECV ? 1 : 0,
                   node->pos.line, node->pos.column, node->pos.offset);
    return ok;
}

static bool emit_op_close(const TypedProgram *program, const TypedNode *node,
                          TextBuffer *buffer, ErrorState *errors)
{
    bool ok;

    buffer_append(buffer, "(nyx_chan_close(");
    ok = emit_channel_pointer(program, node, buffer, errors);
    buffer_append(buffer, "), (nyx_nil)0)");
    return ok;
}

static bool emit_op_closed(const TypedProgram *program, const TypedNode *node,
                           TextBuffer *buffer, ErrorState *errors)
{
    bool ok;

    buffer_append(buffer, "(nyx_chan_closed(");
    ok = emit_channel_pointer(program, node, buffer, errors);
    buffer_append(buffer, ") != 0)");
    return ok;
}

/* How each channel operation reads as an expression. */
static bool (*const channel_op_emitters[])(const TypedProgram *,
                                           const TypedNode *, TextBuffer *,
                                           ErrorState *) = {
    [CHANNEL_OP_SEND] = emit_op_send,
    [CHANNEL_OP_RECV] = emit_op_recv,
    [CHANNEL_OP_TRY_SEND] = emit_op_send,
    [CHANNEL_OP_TRY_RECV] = emit_op_recv,
    [CHANNEL_OP_CLOSE] = emit_op_close,
    [CHANNEL_OP_CLOSED] = emit_op_closed,
};

bool emit_channel_op_expression(const TypedProgram *program,
                                const TypedNode *node, TextBuffer *buffer,
                                ErrorState *errors)
{
    return channel_op_emitters[node->integer_value](program, node, buffer,
                                                    errors);
}

/* pool.channel<T>(capacity) */
bool emit_channel_create_expression(const TypedProgram *program,
                                    const TypedNode *node, TextBuffer *buffer,
                                    ErrorState *errors)
{
    char element[NYX_IDENTIFIER_MAX + 32];
    bool ok;

    if (!store_reference_is_valid(program, node)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    if (!format_c_type(program, node->auxiliary_type, element, sizeof(element),
                       errors)) {
        return false;
    }
    buffer_append(buffer, "nyx_chan_create(");
    ok = emit_store_reference(program, node, buffer, errors);
    buffer_append(buffer, ", ");
    ok = emit_expression(program, node->left_child, buffer, errors) && ok;
    buffer_appendf(buffer, ", sizeof(%s), _Alignof(%s), __FILE__, %zu, %zu, %zu)",
                   element, element, node->pos.line, node->pos.column,
                   node->pos.offset);
    return ok;
}

/* when. The values of send arms are evaluated once, before anything waits. The
   lock is then held while the arms are tried in order, so what an arm finds
   ready is still ready when it takes it; the arm's block runs after the lock is
   released. Nothing wraps the blocks in a switch, so `break` and `continue` in an
   arm reach the loop around the `when`. */
static const TypedNode *when_arm_operation(const TypedProgram *program,
                                           const TypedNode *arm)
{
    const TypedNode *operation = typed_program_node(program, arm->left_child);

    return operation != NULL && operation->kind == TYPED_CHANNEL_OP ? operation
                                                                    : NULL;
}

static size_t when_child(const TypedProgram *program, const TypedNode *node,
                         size_t i)
{
    return *(const size_t *)list_get(&program->children,
                                     node->children_start + i);
}

/* The locals an arm needs: the value to send, or the buffer and result of a
   receive. */
static bool emit_when_locals(const TypedProgram *program, size_t arm_index,
                             const TypedNode *operation, size_t depth,
                             TextBuffer *source, ErrorState *errors)
{
    char type[NYX_IDENTIFIER_MAX + 32], optional[NYX_IDENTIFIER_MAX + 32];
    bool ok = format_c_type(program, operation->auxiliary_type, type,
                            sizeof(type), errors);

    if (!ok) {
        return false;
    }
    buffer_line(source, depth, "nyx_chan *nyx_wa_%zu_c = ", arm_index);
    ok = emit_channel_pointer(program, operation, source, errors);
    buffer_append(source, ";\n");
    if (operation->integer_value == CHANNEL_OP_SEND) {
        buffer_line(source, depth, "%s nyx_wa_%zu_v = ", type, arm_index);
        ok = emit_expression(program, operation->left_child, source, errors) &&
             ok;
        buffer_appendf(source, "; bool nyx_wa_%zu_s = false;\n", arm_index);
        return ok;
    }
    if (!format_c_type(program, operation->type_index, optional,
                       sizeof(optional), errors)) {
        return false;
    }
    buffer_line(source, depth,
                "%s nyx_wa_%zu_t; %s nyx_wa_%zu_r; "
                "memset(&nyx_wa_%zu_t, 0, sizeof(nyx_wa_%zu_t)); "
                "memset(&nyx_wa_%zu_r, 0, sizeof(nyx_wa_%zu_r));\n",
                type, arm_index, optional, arm_index, arm_index, arm_index,
                arm_index, arm_index);
    return ok;
}

/* Tries one arm with the lock held: if its operation can proceed, performs it
   and chooses the arm. */
static void emit_when_try(const TypedNode *operation, size_t arm_index,
                          size_t when_index, size_t position, size_t depth,
                          TextBuffer *source)
{
    if (operation->integer_value == CHANNEL_OP_SEND) {
        buffer_line(source, depth,
                    "if (nyx_chan_ready_send(nyx_wa_%zu_c)) { nyx_wa_%zu_s = "
                    "nyx_chan_put(nyx_wa_%zu_c, &nyx_wa_%zu_v) != 0; "
                    "nyx_when_%zu = %zu; break; }\n",
                    arm_index, arm_index, arm_index, arm_index, when_index,
                    position);
        return;
    }
    buffer_line(source, depth,
                "if (nyx_chan_ready_recv(nyx_wa_%zu_c)) { nyx_wa_%zu_r = "
                "nyx_chan_t%zu_wrap(nyx_chan_take(nyx_wa_%zu_c, &nyx_wa_%zu_t), "
                "&nyx_wa_%zu_t); nyx_when_%zu = %zu; break; }\n"
                "        nyx_due = nyx_chan_due(nyx_wa_%zu_c) < nyx_due "
                "? nyx_chan_due(nyx_wa_%zu_c) : nyx_due;\n",
                arm_index, arm_index, operation->auxiliary_type, arm_index,
                arm_index, arm_index, when_index, position, arm_index,
                arm_index);
}

/* The locals every arm needs, then the lock and the head of the loop that
   tries the arms. */
static bool emit_when_prelude(const TypedProgram *program, size_t node_index,
                              const TypedNode *node, size_t depth,
                              TextBuffer *source, ErrorState *errors)
{
    size_t i;

    buffer_line(source, depth, "{\n");
    buffer_line(source, depth + 1, "int nyx_when_%zu = -1;\n", node_index);
    for (i = 0; i < node->children_count; i++) {
        size_t arm_index = when_child(program, node, i);
        const TypedNode *operation = when_arm_operation(
            program, typed_program_node(program, arm_index));

        if (!emit_when_locals(program, arm_index, operation, depth + 1, source,
                              errors)) {
            return false;
        }
    }
    buffer_line(source, depth + 1, "nyx_chan_acquire();\n");
    buffer_line(source, depth + 1, "for (;;) {\n");
    buffer_line(source, depth + 2,
                "int64_t nyx_due = INT64_MAX; (void)nyx_due;\n");
    return true;
}

/* Each arm in turn, then either the else (which never waits) or the wait. */
static void emit_when_attempts(const TypedProgram *program, size_t node_index,
                               const TypedNode *node, size_t depth,
                               TextBuffer *source)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        size_t arm_index = when_child(program, node, i);

        emit_when_try(when_arm_operation(program,
                                         typed_program_node(program, arm_index)),
                      arm_index, node_index, i, depth + 2, source);
    }
    if (node->right_child != TYPED_NO_NODE) {
        buffer_line(source, depth + 2, "nyx_when_%zu = %zu; break;\n",
                    node_index, node->children_count);
    } else {
        buffer_line(source, depth + 2,
                    "nyx_chan_wait(nyx_due, __FILE__, %zu, %zu, %zu);\n",
                    node->pos.line, node->pos.column, node->pos.offset);
    }
    buffer_line(source, depth + 1, "}\n");
    buffer_line(source, depth + 1, "nyx_chan_release();\n");
}

/* The blocks, as an `if` chain after the lock is released. */
static bool emit_when_blocks(const TypedProgram *program, GeneratorContext *gen, size_t node_index,
                             const TypedNode *node, size_t depth,
                             const char *effective_source, TextBuffer *source,
                             ErrorState *errors)
{
    size_t i;
    bool ok = true;

    for (i = 0; ok && i < node->children_count; i++) {
        const TypedNode *arm = typed_program_node(program, when_child(program, node, i));

        buffer_line(source, depth + 1, "%sif (nyx_when_%zu == %zu) {\n",
                    i == 0 ? "" : "else ", node_index, i);
        ok = emit_block(program, gen, arm->right_child, depth + 2,
                        effective_source, source, errors);
        buffer_line(source, depth + 1, "}\n");
    }
    if (ok && node->right_child != TYPED_NO_NODE) {
        buffer_line(source, depth + 1, "else {\n");
        ok = emit_block(program, gen, node->right_child, depth + 2,
                        effective_source, source, errors);
        buffer_line(source, depth + 1, "}\n");
    }
    return ok;
}

bool emit_when_statement(const TypedProgram *program, GeneratorContext *gen, size_t node_index,
                         size_t depth, const char *effective_source,
                         TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    bool ok = emit_when_prelude(program, node_index, node, depth, source,
                                errors);

    if (ok) {
        emit_when_attempts(program, node_index, node, depth, source);
        ok = emit_when_blocks(program, gen, node_index, node, depth,
                              effective_source, source, errors);
    }
    buffer_line(source, depth, "}\n");
    return ok;
}

/* The result a `when` arm declares in its block. */
bool emit_when_result(const TypedProgram *program, const TypedNode *node,
                      TextBuffer *buffer, ErrorState *errors)
{
    size_t arm_index = (size_t)node->integer_value_u;
    const TypedNode *operation = when_arm_operation(
        program, typed_program_node(program, arm_index));

    (void)errors;
    return buffer_appendf(buffer,
                          operation->integer_value == CHANNEL_OP_SEND
                              ? "nyx_wa_%zu_s" : "nyx_wa_%zu_r",
                          arm_index);
}
