/* POSIX interfaces (fork, waitpid, mkdtemp, nftw) are not in strict C11. */
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "harness.h"

#include <ctype.h>
#include <errno.h>
#ifdef __GLIBC__
#include <execinfo.h>
#endif
#include <fcntl.h>
#include <ftw.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_TIMEOUT_SECONDS 120
#define OUTPUT_LIMIT 16384
#define PATH_BUFFERS 4
#define PATH_CAPACITY 512
/* A returned path is a directory plus a file name, so it gets headroom. */
#define RETURNED_PATH_CAPACITY (PATH_CAPACITY * 2)

typedef struct {
    const char *file;
    int line;
    const char *name;
    TestFn run;
    TestDataFn run_data;
    void *data;
    size_t sequence; /* registration order, to keep ties in a stable order */
    char group[48];
} Entry;

/* What happened to one selected test. `result` is a TestResult; `detail` is
   set when the child died or timed out rather than returning. */
typedef struct {
    bool ran; /* false for a test never started, after --fail-fast */
    int result;
    double seconds;
    char detail[64];
    char *output;
} Outcome;

typedef struct {
    bool verbose;
    bool list;
    bool fail_fast;
    bool help;
    size_t jobs;
    unsigned timeout;
    size_t slowest;
    const char **filters;
    size_t filter_count;
} Options;

static Entry *entries;
static size_t entry_count;
static size_t entry_capacity;

/* Set in each test's child process. The directory is created on first use, so
   the many tests that never touch a file pay nothing for it. */
static char work_directory[PATH_CAPACITY] = ".";
static bool work_directory_private;
static bool work_directory_made;


void expect_message(char *buffer, size_t size, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vsnprintf(buffer, size, format, args);
    va_end(args);
}


const char *test_path(const char *name)
{
    static char buffers[PATH_BUFFERS][RETURNED_PATH_CAPACITY];
    static size_t next;
    char *buffer = buffers[next++ % PATH_BUFFERS];

    if (work_directory_private && !work_directory_made) {
        work_directory_made = true;
        mkdir(work_directory, 0700);
    }
    snprintf(buffer, RETURNED_PATH_CAPACITY, "%s/%s", work_directory, name);
    return buffer;
}


/* ---- registration ---- */

static void group_from_file(const char *file, char *group, size_t size)
{
    const char *slash = strrchr(file, '/');
    const char *base = slash == NULL ? file : slash + 1;
    const char *dot = strrchr(base, '.');
    size_t length = dot == NULL ? strlen(base) : (size_t)(dot - base);

    if (length >= size) {
        length = size - 1;
    }
    memcpy(group, base, length);
    group[length] = '\0';
}


static Entry *new_entry(const char *file, int line, const char *name)
{
    Entry *entry;

    if (entry_count == entry_capacity) {
        size_t capacity = entry_capacity == 0 ? 256 : entry_capacity * 2;
        Entry *grown = realloc(entries, capacity * sizeof(Entry));

        if (grown == NULL) {
            fprintf(stderr, "test harness: out of memory registering tests\n");
            exit(2);
        }
        entries = grown;
        entry_capacity = capacity;
    }
    entry = &entries[entry_count];
    memset(entry, 0, sizeof(*entry));
    entry->sequence = entry_count++;
    entry->file = file;
    entry->line = line;
    entry->name = name;
    group_from_file(file, entry->group, sizeof(entry->group));
    return entry;
}


void test_register(const char *file, int line, const char *name, TestFn run)
{
    new_entry(file, line, name)->run = run;
}


void test_register_data(const char *file, int line, const char *name,
                        TestDataFn run, void *data)
{
    Entry *entry = new_entry(file, line, name);

    entry->run_data = run;
    entry->data = data;
}


/* Constructors run in link order, which is not a property worth depending on,
   so tests are ordered by group and then by position in their file. */
static int compare_entries(const void *a, const void *b)
{
    const Entry *left = a;
    const Entry *right = b;
    int by_group = strcmp(left->group, right->group);

    if (by_group != 0) {
        return by_group;
    }
    if (strcmp(left->file, right->file) != 0) {
        return strcmp(left->file, right->file);
    }
    if (left->line != right->line) {
        return left->line - right->line;
    }
    return left->sequence < right->sequence
               ? -1
               : left->sequence > right->sequence;
}


/* ---- selection ---- */

static bool contains_ignoring_case(const char *text, const char *needle)
{
    size_t needle_length = strlen(needle);
    size_t i;

    if (needle_length == 0) {
        return true;
    }
    for (i = 0; text[i] != '\0'; i++) {
        size_t j = 0;

        while (j < needle_length && text[i + j] != '\0' &&
               tolower((unsigned char)text[i + j]) ==
                   tolower((unsigned char)needle[j])) {
            j++;
        }
        if (j == needle_length) {
            return true;
        }
    }
    return false;
}


/* A test is selected when any filter is a case-insensitive substring of
   "group: name". With no filters every test is selected. */
static bool is_selected(const Entry *entry, const Options *options)
{
    char label[256];
    size_t i;

    if (options->filter_count == 0) {
        return true;
    }
    snprintf(label, sizeof(label), "%s: %s", entry->group, entry->name);
    for (i = 0; i < options->filter_count; i++) {
        if (contains_ignoring_case(label, options->filters[i])) {
            return true;
        }
    }
    return false;
}


/* ---- running ---- */

/* The signal that asked the run to stop, or 0. The handler only records it;
   the run loop reacts, because most cleanup is not safe inside a handler. */
static volatile sig_atomic_t interrupted_by;

static void note_signal(int number)
{
    interrupted_by = number;
}


static void install_signal_handlers(void)
{
    struct sigaction action;
    int signals[] = {SIGINT, SIGTERM, SIGHUP};
    size_t i;

    memset(&action, 0, sizeof(action));
    action.sa_handler = note_signal;
    sigemptyset(&action.sa_mask);
    /* No SA_RESTART: a signal must interrupt waitpid so the loop can react. */
    for (i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
        sigaction(signals[i], &action, NULL);
    }
}


static double now_seconds(void)
{
    struct timespec time;

    clock_gettime(CLOCK_MONOTONIC, &time);
    return (double)time.tv_sec + (double)time.tv_nsec / 1e9;
}


static int remove_entry(const char *path, const struct stat *status, int flag,
                        struct FTW *walk)
{
    (void)status;
    (void)flag;
    (void)walk;
    return remove(path);
}


static void remove_tree(const char *path)
{
    nftw(path, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
}


#ifdef __GLIBC__
/* A crash in a test prints the functions on the stack, so a report says where
   it happened and not only that it did. Symbol names need the tester linked
   with -rdynamic; static functions show as addresses. */
static void report_crash(int number)
{
    void *frames[32];
    static const char heading[] = "  stack at the crash:\n";
    int count = backtrace(frames, 32);

    if (write(STDERR_FILENO, heading, sizeof(heading) - 1) > 0) {
        backtrace_symbols_fd(frames, count, STDERR_FILENO);
    }
    signal(number, SIG_DFL);
    raise(number);
}
#endif


/* The body of a test's child process. It never returns. */
static void run_child(const Entry *entry, const char *root, size_t index,
                      unsigned timeout)
{
    char output_path[PATH_CAPACITY];
    int descriptor;
    TestResult result;

    snprintf(output_path, sizeof(output_path), "%s/%zu.out", root, index);
    snprintf(work_directory, sizeof(work_directory), "%s/%zu", root, index);
    /* The runner's handlers are for the runner; a test dies on a signal. */
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
    work_directory_private = true;
    descriptor = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (descriptor < 0) {
        _exit(TEST_ERROR);
    }
    dup2(descriptor, STDOUT_FILENO);
    dup2(descriptor, STDERR_FILENO);
    close(descriptor);
#ifdef __GLIBC__
    signal(SIGSEGV, report_crash);
    signal(SIGBUS, report_crash);
    signal(SIGFPE, report_crash);
    signal(SIGABRT, report_crash);
#endif
    if (timeout > 0) {
        alarm(timeout);
    }
    result = entry->run_data != NULL ? entry->run_data(entry->data)
                                     : entry->run();
    fflush(stdout);
    fflush(stderr);
    _exit((int)result);
}


static char *read_output(const char *root, size_t index)
{
    char path[PATH_CAPACITY];
    FILE *file;
    char *text;
    size_t length;

    snprintf(path, sizeof(path), "%s/%zu.out", root, index);
    file = fopen(path, "r");
    if (file == NULL) {
        return NULL;
    }
    text = malloc(OUTPUT_LIMIT + 1);
    if (text == NULL) {
        fclose(file);
        return NULL;
    }
    length = fread(text, 1, OUTPUT_LIMIT, file);
    text[length] = '\0';
    fclose(file);
    return text;
}


static void record_exit(Outcome *outcome, int wait_status, unsigned timeout)
{
    if (WIFEXITED(wait_status) && WEXITSTATUS(wait_status) <= TEST_ERROR) {
        outcome->result = WEXITSTATUS(wait_status);
    } else if (WIFSIGNALED(wait_status) && WTERMSIG(wait_status) == SIGALRM) {
        outcome->result = TEST_ERROR;
        snprintf(outcome->detail, sizeof(outcome->detail),
                 "timed out after %us", timeout);
    } else if (WIFSIGNALED(wait_status)) {
        outcome->result = TEST_ERROR;
        snprintf(outcome->detail, sizeof(outcome->detail), "crashed: %s",
                 strsignal(WTERMSIG(wait_status)));
    } else {
        outcome->result = TEST_ERROR;
        snprintf(outcome->detail, sizeof(outcome->detail),
                 "exited with status %d", WEXITSTATUS(wait_status));
    }
}


typedef struct {
    pid_t pid;
    size_t slot;
    double started;
} Running;


/* Runs the selected tests, at most `options->jobs` at a time. Fills `outcomes`
   in selection order. */
static void run_selected(const size_t *selected, size_t count,
                         const Options *options, const char *root,
                         Outcome *outcomes, bool show_progress)
{
    Running *running = calloc(options->jobs, sizeof(Running));
    size_t next = 0;
    size_t active = 0;
    size_t finished = 0;
    bool stop = false;
    size_t i;

    if (running == NULL) {
        fprintf(stderr, "test harness: out of memory\n");
        exit(2);
    }
    while ((next < count && !stop) || active > 0) {
        if (interrupted_by != 0 && !stop) {
            /* Stop starting tests and ask the running ones to end. */
            stop = true;
            for (i = 0; i < options->jobs; i++) {
                if (running[i].pid != 0) {
                    kill(running[i].pid, SIGTERM);
                }
            }
        }
        if (next < count && !stop && active < options->jobs) {
            size_t index = selected[next];
            pid_t pid;

            fflush(stdout);
            fflush(stderr);
            pid = fork();
            if (pid < 0) {
                perror("fork");
                outcomes[next].ran = true;
                outcomes[next].result = TEST_ERROR;
                snprintf(outcomes[next].detail, sizeof(outcomes[next].detail),
                         "could not start");
                next++;
                finished++;
                continue;
            }
            if (pid == 0) {
                run_child(&entries[index], root, next, options->timeout);
            }
            for (i = 0; i < options->jobs; i++) {
                if (running[i].pid == 0) {
                    running[i].pid = pid;
                    running[i].slot = next;
                    running[i].started = now_seconds();
                    break;
                }
            }
            next++;
            active++;
        } else {
            int wait_status;
            pid_t pid = waitpid(-1, &wait_status, 0);

            if (pid < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            for (i = 0; i < options->jobs; i++) {
                if (running[i].pid == pid) {
                    Outcome *outcome = &outcomes[running[i].slot];

                    outcome->ran = true;
                    outcome->seconds = now_seconds() - running[i].started;
                    record_exit(outcome, wait_status, options->timeout);
                    if (outcome->result != TEST_PASS || options->verbose) {
                        outcome->output = read_output(root, running[i].slot);
                    }
                    if (outcome->result == TEST_FAIL ||
                        outcome->result == TEST_ERROR) {
                        stop = stop || options->fail_fast;
                    }
                    running[i].pid = 0;
                    active--;
                    finished++;
                    if (show_progress) {
                        fprintf(stderr, "\r%zu/%zu", finished, count);
                    }
                    break;
                }
            }
        }
    }
    free(running);
}


/* ---- reporting ---- */

static const char *label(int result)
{
    switch (result) {
    case TEST_PASS:
        return "PASS ";
    case TEST_FAIL:
        return "FAIL ";
    case TEST_TODO:
        return "TODO ";
    default:
        return "ERROR";
    }
}


static void print_indented(const char *text)
{
    const char *line = text;

    while (*line != '\0') {
        const char *end = strchr(line, '\n');
        size_t length = end == NULL ? strlen(line) : (size_t)(end - line);

        printf("    | %.*s\n", (int)length, line);
        if (end == NULL) {
            break;
        }
        line = end + 1;
    }
}


static void print_usage(const char *program)
{
    printf("usage: %s [options] [filter...]\n"
           "\n"
           "Runs the tests whose \"group: name\" contains any filter (case-"
           "insensitive).\n"
           "With no filter, runs everything.\n"
           "\n"
           "  -v, --verbose      print every test with its time\n"
           "  -l, --list         list the selected tests by group and exit\n"
           "  -j, --jobs N       run N tests at once (default: one per CPU, or "
           "$NIPPY_TEST_JOBS)\n"
           "  -x, --fail-fast    stop starting new tests after the first "
           "failure\n"
           "  -t, --timeout S    kill a test after S seconds (default %d)\n"
           "      --slowest N    list the N slowest tests\n"
           "  -h, --help         show this help\n",
           program, DEFAULT_TIMEOUT_SECONDS);
}


static bool parse_options(int argc, char **argv, Options *options)
{
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    const char *env_jobs = getenv("NIPPY_TEST_JOBS");
    int i;

    memset(options, 0, sizeof(*options));
    options->jobs = cpus > 0 ? (size_t)cpus : 1;
    if (env_jobs != NULL && atoi(env_jobs) > 0) {
        options->jobs = (size_t)atoi(env_jobs);
    }
    options->timeout = DEFAULT_TIMEOUT_SECONDS;
    options->filters = calloc((size_t)argc + 1, sizeof(char *));
    if (options->filters == NULL) {
        return false;
    }
    for (i = 1; i < argc; i++) {
        const char *argument = argv[i];

        if (strcmp(argument, "-v") == 0 || strcmp(argument, "--verbose") == 0) {
            options->verbose = true;
        } else if (strcmp(argument, "-l") == 0 ||
                   strcmp(argument, "--list") == 0) {
            options->list = true;
        } else if (strcmp(argument, "-x") == 0 ||
                   strcmp(argument, "--fail-fast") == 0) {
            options->fail_fast = true;
        } else if (strcmp(argument, "-h") == 0 ||
                   strcmp(argument, "--help") == 0) {
            options->help = true;
        } else if (strncmp(argument, "-j", 2) == 0 && argument[2] != '\0') {
            int jobs = atoi(argument + 2);

            options->jobs = jobs > 0 ? (size_t)jobs : 1;
        } else if ((strcmp(argument, "-j") == 0 ||
                    strcmp(argument, "--jobs") == 0) &&
                   i + 1 < argc) {
            int jobs = atoi(argv[++i]);

            options->jobs = jobs > 0 ? (size_t)jobs : 1;
        } else if ((strcmp(argument, "-t") == 0 ||
                    strcmp(argument, "--timeout") == 0) &&
                   i + 1 < argc) {
            int seconds = atoi(argv[++i]);

            options->timeout = seconds > 0 ? (unsigned)seconds : 0;
        } else if (strcmp(argument, "--slowest") == 0 && i + 1 < argc) {
            int count = atoi(argv[++i]);

            options->slowest = count > 0 ? (size_t)count : 0;
        } else if (argument[0] == '-' && argument[1] != '\0') {
            fprintf(stderr, "unknown option '%s' (try --help)\n", argument);
            return false;
        } else {
            options->filters[options->filter_count++] = argument;
        }
    }
    return true;
}


typedef struct {
    size_t slot;
    double seconds;
} Timing;

static int compare_timing(const void *a, const void *b)
{
    const Timing *left = a;
    const Timing *right = b;

    if (left->seconds < right->seconds) {
        return 1;
    }
    return left->seconds > right->seconds ? -1 : 0;
}


int test_main(int argc, char **argv, const char *title)
{
    Options options;
    size_t *selected = NULL;
    Outcome *outcomes = NULL;
    size_t selected_count = 0;
    size_t counts[TEST_ERROR + 1] = {0};
    size_t skipped = 0;
    char root_template[PATH_CAPACITY];
    const char *temporary = getenv("TMPDIR");
    const char *root;
    double started;
    double elapsed;
    size_t i;
    int status = 0;

    if (!parse_options(argc, argv, &options)) {
        return 2;
    }
    if (options.help) {
        print_usage(argv[0]);
        free(options.filters);
        return 0;
    }
    qsort(entries, entry_count, sizeof(Entry), compare_entries);
    selected = calloc(entry_count == 0 ? 1 : entry_count, sizeof(size_t));
    if (selected == NULL) {
        return 2;
    }
    for (i = 0; i < entry_count; i++) {
        if (is_selected(&entries[i], &options)) {
            selected[selected_count++] = i;
        }
    }

    if (options.list) {
        const char *group = "";

        for (i = 0; i < selected_count; i++) {
            const Entry *entry = &entries[selected[i]];

            if (strcmp(group, entry->group) != 0) {
                size_t total = 0;
                size_t j;

                for (j = i; j < selected_count &&
                            strcmp(entries[selected[j]].group,
                                   entry->group) == 0;
                     j++) {
                    total++;
                }
                printf("%s (%zu)\n", entry->group, total);
                group = entry->group;
            }
            printf("  %s\n", entry->name);
        }
        printf("%zu tests\n", selected_count);
        free(selected);
        free(options.filters);
        return 0;
    }
    if (selected_count == 0) {
        fprintf(stderr, "%s: no tests match", title);
        for (i = 0; i < options.filter_count; i++) {
            fprintf(stderr, " '%s'", options.filters[i]);
        }
        fprintf(stderr, "\n");
        free(selected);
        free(options.filters);
        return 2;
    }

    snprintf(root_template, sizeof(root_template), "%s/nippy-test-XXXXXX",
             temporary != NULL && temporary[0] != '\0' ? temporary : "/tmp");
    root = mkdtemp(root_template);
    outcomes = calloc(selected_count, sizeof(Outcome));
    if (root == NULL || outcomes == NULL) {
        fprintf(stderr, "%s: could not create a scratch directory\n", title);
        free(selected);
        free(options.filters);
        return 2;
    }
    if (options.jobs > selected_count) {
        options.jobs = selected_count;
    }

    install_signal_handlers();
    started = now_seconds();
    run_selected(selected, selected_count, &options, root, outcomes,
                 !options.verbose && isatty(STDERR_FILENO));
    elapsed = now_seconds() - started;
    if (interrupted_by != 0) {
        int number = interrupted_by;

        fprintf(stderr, "\n%s: interrupted\n", title);
        remove_tree(root);
        for (i = 0; i < selected_count; i++) {
            free(outcomes[i].output);
        }
        free(outcomes);
        free(selected);
        free(options.filters);
        return 128 + number;
    }
    if (!options.verbose && isatty(STDERR_FILENO)) {
        fprintf(stderr, "\r          \r");
    }

    for (i = 0; i < selected_count; i++) {
        const Entry *entry = &entries[selected[i]];
        const Outcome *outcome = &outcomes[i];

        if (!outcome->ran) {
            skipped++;
            continue;
        }
        counts[outcome->result]++;
        if (outcome->result == TEST_PASS && !options.verbose) {
            continue;
        }
        if (outcome->result == TEST_PASS && options.verbose) {
            printf("%s %s: %s (%.0f ms)\n", label(outcome->result),
                   entry->group, entry->name, outcome->seconds * 1000.0);
            if (outcome->output != NULL && outcome->output[0] != '\0') {
                print_indented(outcome->output);
            }
            continue;
        }
        printf("%s %s: %s", label(outcome->result), entry->group, entry->name);
        if (outcome->detail[0] != '\0') {
            printf(" (%s)", outcome->detail);
        }
        printf("\n    at %s:%d\n", entry->file, entry->line);
        if (outcome->output != NULL && outcome->output[0] != '\0') {
            print_indented(outcome->output);
        }
    }

    if (options.slowest > 0) {
        Timing *timings = calloc(selected_count, sizeof(Timing));

        if (timings != NULL) {
            size_t shown = options.slowest < selected_count
                               ? options.slowest
                               : selected_count;

            for (i = 0; i < selected_count; i++) {
                timings[i].slot = i;
                timings[i].seconds = outcomes[i].seconds;
            }
            qsort(timings, selected_count, sizeof(Timing), compare_timing);
            printf("slowest:\n");
            for (i = 0; i < shown; i++) {
                const Entry *entry = &entries[selected[timings[i].slot]];

                printf("  %6.0f ms  %s: %s\n", timings[i].seconds * 1000.0,
                       entry->group, entry->name);
            }
            free(timings);
        }
    }

    printf("%s: %zu passed", title, counts[TEST_PASS]);
    if (counts[TEST_FAIL] > 0) {
        printf(", %zu failed", counts[TEST_FAIL]);
    }
    if (counts[TEST_ERROR] > 0) {
        printf(", %zu errors", counts[TEST_ERROR]);
    }
    if (counts[TEST_TODO] > 0) {
        printf(", %zu todo", counts[TEST_TODO]);
    }
    if (skipped > 0) {
        printf(", %zu not run", skipped);
    }
    printf(" (%zu of %zu tests, %.2fs, %zu at a time)\n", selected_count,
           entry_count, elapsed, options.jobs);
    status = counts[TEST_FAIL] > 0 || counts[TEST_ERROR] > 0 ? 1 : 0;

    for (i = 0; i < selected_count; i++) {
        free(outcomes[i].output);
    }
    remove_tree(root);
    free(outcomes);
    free(selected);
    free(options.filters);
    return status;
}
