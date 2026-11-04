/* faulthandler (Python/traceback.c, Modules/faulthandler.c): dumping the
   Python stack without allocating, from a signal handler or a watchdog thread.
   It walks each task's frame chain (FOUNDATIONS 2.2); frames of kernel code and
   of the builtin modules, which look like C, are left out. The module
   lib/modules/faulthandler.py keeps CPython's argument handling and calls these
   primitives. Everything between a signal's arrival and the dump uses only
   write(2) and reads of live structures. */

#define _GNU_SOURCE
#include "prim/prim.h"
#include "interp/frame.h"
#include "interp/code.h"
#include "interp/frozen.h"

#include <dlfcn.h>
#include <errno.h>
#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define MAX_FRAMES 100
#define MAX_TEXT 500
#define MAX_TRACE 64

/* Output: write(2) only. */

static void
out(int fd, const char *s, size_t n)
{
    while (n > 0) {
        ssize_t w = write(fd, s, n);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        s += w;
        n -= (size_t)w;
    }
}

static void
outs(int fd, const char *s)
{
    out(fd, s, strlen(s));
}

static void
out_hex(int fd, uintptr_t v, int width)
{
    char buf[2 * sizeof v];
    int n = 0;
    do {
        buf[n++] = "0123456789abcdef"[v & 15];
        v >>= 4;
    } while (v != 0);
    for (int i = n; i < width; i++) {
        out(fd, "0", 1);
    }
    for (int i = n; i-- > 0;) {
        out(fd, &buf[i], 1);
    }
}

static void
out_dec(int fd, long v)
{
    char buf[24];
    int n = 0;
    unsigned long u = v < 0 ? 0UL - (unsigned long)v : (unsigned long)v;
    do {
        buf[n++] = (char)('0' + u % 10);
        u /= 10;
    } while (u != 0);
    if (v < 0) {
        out(fd, "-", 1);
    }
    for (int i = n; i-- > 0;) {
        out(fd, &buf[i], 1);
    }
}

/* A str, ASCII as it is and the rest escaped, cut at MAX_TEXT characters. */
static void
out_text(int fd, PyObject *s)
{
    if (s == NULL || !PyUnicode_Check(s)) {
        outs(fd, "???");
        return;
    }
    Py_ssize_t n = PyUnicode_GET_LENGTH(s);
    int kind = PyUnicode_KIND(s);
    const void *data = PyUnicode_DATA(s);
    for (Py_ssize_t i = 0; i < n; i++) {
        if (i >= MAX_TEXT) {
            outs(fd, "...");
            return;
        }
        Py_UCS4 c = PyUnicode_READ(kind, data, i);
        if (c >= ' ' && c < 0x7f) {
            char ch = (char)c;
            out(fd, &ch, 1);
        }
        else if (c <= 0xff) {
            outs(fd, "\\x");
            out_hex(fd, c, 2);
        }
        else if (c <= 0xffff) {
            outs(fd, "\\u");
            out_hex(fd, c, 4);
        }
        else {
            outs(fd, "\\U");
            out_hex(fd, c, 8);
        }
    }
}

/* Frames. */

/* A frame of Python code a user would see: not a kernel frame, and not a
   builtin module's, whose functions look like C. */
static int
is_user_frame(struct lu_frame *f)
{
    if (f->owner == LU_FRAME_C_ENTRY || f->owner == LU_FRAME_KERNEL_HIDDEN || f->code == NULL
        || (f->code->co_flags & LU_CO_KERNEL)) {
        return 0;
    }
    PyObject *file = f->code->co_filename;
    if (file != NULL && PyUnicode_Check(file) && PyUnicode_IS_ASCII(file)
        && PyUnicode_GET_LENGTH(file) > 9) {
        const char *name = (const char *)PyUnicode_1BYTE_DATA(file);
        if (strncmp(name, "<frozen ", 8) == 0) {
            for (const struct lu_frozen *m = lu_frozen_builtin; m->name != NULL; m++) {
                size_t n = strlen(m->name);
                if (strncmp(name + 8, m->name, n) == 0 && name[8 + n] == '>' && name[9 + n] == '\0') {
                    return 0;
                }
            }
        }
    }
    return 1;
}

static void
dump_frames(int fd, struct lu_frame *f)
{
    int depth = 0, shown = 0;
    for (; f != NULL && depth < 4 * MAX_FRAMES; f = f->previous, depth++) {
        if (!is_user_frame(f)) {
            continue;
        }
        if (shown++ >= MAX_FRAMES) {
            outs(fd, "  ...\n");
            return;
        }
        PyCodeObject *co = f->code;
        outs(fd, "  File \"");
        out_text(fd, co->co_filename);
        outs(fd, "\", line ");
        int line = PyCode_Addr2Line(co, (int)(lu_frame_lasti(f) * sizeof(lu_codeunit)));
        if (line >= 0) {
            out_dec(fd, line);
        }
        else {
            outs(fd, "???");
        }
        outs(fd, " in ");
        out_text(fd, co->co_name);
        outs(fd, "\n");
    }
    if (shown == 0) {
        outs(fd, "  <no Python frame>\n");
    }
}

static struct lu_frame *
top_frame(PyThreadState *ts)
{
    return ts != NULL ? (struct lu_frame *)ts->current_frame : NULL;
}

/* The tasks of threads (the main task and the threads started by _thread), for
   a dump of all threads. */
struct fh_thread {
    PyThreadState *ts;
    struct fh_thread *next;
};
static struct fh_thread *fh_threads;
static pthread_mutex_t fh_threads_lock = PTHREAD_MUTEX_INITIALIZER;

void
lu_fh_thread_enter(PyThreadState *ts)
{
    struct fh_thread *t = malloc(sizeof *t);
    if (t == NULL) {
        return;
    }
    t->ts = ts;
    pthread_mutex_lock(&fh_threads_lock);
    t->next = fh_threads;
    fh_threads = t;
    pthread_mutex_unlock(&fh_threads_lock);
}

void
lu_fh_thread_exit(PyThreadState *ts)
{
    pthread_mutex_lock(&fh_threads_lock);
    for (struct fh_thread **p = &fh_threads; *p != NULL; p = &(*p)->next) {
        if ((*p)->ts == ts) {
            struct fh_thread *dead = *p;
            *p = dead->next;
            free(dead);
            break;
        }
    }
    pthread_mutex_unlock(&fh_threads_lock);
}

static void
dump_thread(int fd, PyThreadState *ts, int current)
{
    outs(fd, current ? "Current thread 0x" : "Thread 0x");
    out_hex(fd, (uintptr_t)ts->thread_id, 2 * sizeof(void *));
    outs(fd, " (most recent call first):\n");
    dump_frames(fd, top_frame(ts));
}

/* The stacks of the threads other than `current`, then current's: all threads
   with a header each, or just current's as "Stack". `current` may be NULL (a
   watchdog dumps every thread as another). */
static void
dump_threads(int fd, PyThreadState *current, int all_threads)
{
    if (!all_threads) {
        outs(fd, "Stack (most recent call first):\n");
        dump_frames(fd, top_frame(current));
        return;
    }
    int first = 1;
    PyThreadState *main_ts = (PyThreadState *)_PyRuntime.main_task;
    if (main_ts != NULL && main_ts != current) {
        dump_thread(fd, main_ts, 0);
        first = 0;
    }
    /* A signal handler may have interrupted a thread that holds the list: it dumps the
       threads it already knows of. */
    if (pthread_mutex_trylock(&fh_threads_lock) == 0) {
        for (struct fh_thread *t = fh_threads; t != NULL; t = t->next) {
            if (t->ts != current && t->ts != main_ts) {
                if (!first) {
                    outs(fd, "\n");
                }
                dump_thread(fd, t->ts, 0);
                first = 0;
            }
        }
        pthread_mutex_unlock(&fh_threads_lock);
    }
    if (current != NULL) {
        if (!first) {
            outs(fd, "\n");
        }
        dump_thread(fd, current, 1);
    }
}

/* The C stack. */

static void
dump_c_stack(int fd)
{
    void *trace[MAX_TRACE];
    int n = backtrace(trace, MAX_TRACE);
    outs(fd, "Current thread's C stack trace (most recent call first):\n");
    if (n <= 0) {
        outs(fd, "  <cannot get C stack on this system>\n");
        return;
    }
    for (int i = 0; i < n; i++) {
        Dl_info info;
        outs(fd, "  Binary file \"");
        if (dladdr(trace[i], &info) != 0 && info.dli_fname != NULL) {
            outs(fd, info.dli_fname);
            outs(fd, "\", at ");
            uintptr_t base = info.dli_sname != NULL ? (uintptr_t)info.dli_saddr : (uintptr_t)info.dli_fbase;
            if (info.dli_sname != NULL) {
                outs(fd, info.dli_sname);
            }
            outs(fd, "+0x");
            out_hex(fd, (uintptr_t)trace[i] - base, 1);
        }
        else {
            outs(fd, "???\"");
        }
        outs(fd, " [0x");
        out_hex(fd, (uintptr_t)trace[i], 1);
        outs(fd, "]\n");
    }
}

/* The fatal-signal handlers. */

static struct {
    int enabled, fd, all_threads, c_stack;
    char *modules;                      /* the "Extension modules:" line, or NULL */
    struct sigaction previous[NSIG];
} fatal;

static const struct { int signum; const char *name; } fatal_signals[] = {
    { SIGBUS, "Bus error" }, { SIGILL, "Illegal instruction" }, { SIGFPE, "Floating-point exception" },
    { SIGABRT, "Aborted" }, { SIGSEGV, "Segmentation fault" },
};
#define NFATAL (sizeof fatal_signals / sizeof fatal_signals[0])

static void
dump_fatal(int fd, const char *header, int all_threads, int c_stack, int in_handler)
{
    outs(fd, header);
    outs(fd, "\n\n");
    PyThreadState *ts = PyThreadState_GetUnchecked();
    if (all_threads) {
        outs(fd, "<Cannot show all threads while the GIL is disabled>\n");
    }
    outs(fd, "Stack (most recent call first):\n");
    if (ts != NULL) {
        dump_frames(fd, top_frame(ts));
    }
    else {
        outs(fd, "  <tstate is freed>\n");
    }
    if (fatal.modules != NULL) {
        outs(fd, "\n");
        outs(fd, fatal.modules);
        outs(fd, "\n");
    }
    if (c_stack) {
        outs(fd, "\n");
        dump_c_stack(fd);
    }
}

static void
fatal_handler(int signum, siginfo_t *info, void *context)
{
    int saved_errno = errno;
    const char *name = "Unknown signal";
    for (size_t i = 0; i < NFATAL; i++) {
        if (fatal_signals[i].signum == signum) {
            name = fatal_signals[i].name;
        }
    }
    /* A second fault while dumping runs the default action. */
    for (size_t i = 0; i < NFATAL; i++) {
        sigaction(fatal_signals[i].signum, &fatal.previous[fatal_signals[i].signum], NULL);
    }
    char header[80] = "Fatal Python error: ";
    strncat(header, name, sizeof header - strlen(header) - 1);
    dump_fatal(fatal.fd, header, fatal.all_threads, fatal.c_stack, 1);
    errno = saved_errno;
    raise(signum);
}

static void *altstack;

static int
install_altstack(void)
{
    stack_t old;
    if (sigaltstack(NULL, &old) == 0 && !(old.ss_flags & SS_DISABLE)) {
        return 0;                       /* the thread already has one */
    }
    size_t size = 256 * 1024;
    altstack = altstack != NULL ? altstack : mmap(NULL, size, PROT_READ | PROT_WRITE,
                                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (altstack == MAP_FAILED || altstack == NULL) {
        altstack = NULL;
        return -1;
    }
    stack_t ss = { .ss_sp = altstack, .ss_size = size, .ss_flags = 0 };
    return sigaltstack(&ss, NULL);
}

/* fh_enable(fd, all_threads, c_stack, modules or None) */
static PyObject *
prim_fh_enable(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long fd;
    if (nargs != 4 || lu_long_as_long(args[0], &fd) < 0) {
        lu_err_format(PyExc_TypeError, "fh_enable(fd, all_threads, c_stack, modules)");
        return NULL;
    }
    if (fatal.enabled) {
        fatal.fd = (int)fd;
        fatal.all_threads = args[1] == Py_True;
        fatal.c_stack = args[2] == Py_True;
        return Py_NewRef(Py_None);
    }
    void *warm[2];
    backtrace(warm, 2);                 /* loads the unwinder outside any signal handler */
    free(fatal.modules);
    fatal.modules = NULL;
    if (PyUnicode_Check(args[3])) {
        fatal.modules = strdup(PyUnicode_AsUTF8(args[3]));
    }
    fatal.fd = (int)fd;
    fatal.all_threads = args[1] == Py_True;
    fatal.c_stack = args[2] == Py_True;
    install_altstack();
    for (size_t i = 0; i < NFATAL; i++) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = fatal_handler;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
        sigemptyset(&sa.sa_mask);
        sigaction(fatal_signals[i].signum, &sa, &fatal.previous[fatal_signals[i].signum]);
    }
    fatal.enabled = 1;
    return Py_NewRef(Py_None);
}

static PyObject *
prim_fh_disable(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    int was = fatal.enabled;
    if (was) {
        for (size_t i = 0; i < NFATAL; i++) {
            sigaction(fatal_signals[i].signum, &fatal.previous[fatal_signals[i].signum], NULL);
        }
        fatal.enabled = 0;
    }
    return Py_NewRef(was ? Py_True : Py_False);
}

static PyObject *
prim_fh_is_enabled(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    return Py_NewRef(fatal.enabled ? Py_True : Py_False);
}

/* Py_FatalError and its relatives: the message, then the stack. */
void
lu_fh_fatal(const char *func, const char *msg)
{
    int fd = fatal.enabled ? fatal.fd : 2;
    outs(fd, "Fatal Python error: ");
    if (func != NULL) {
        outs(fd, func);
        outs(fd, ": ");
    }
    outs(fd, msg);
    outs(fd, "\nPython runtime state: initialized\n\n");
    PyThreadState *ts = PyThreadState_GetUnchecked();
    outs(fd, "Stack (most recent call first):\n");
    if (ts != NULL) {
        dump_frames(fd, top_frame(ts));
    }
    else {
        outs(fd, "  <tstate is freed>\n");
    }
    outs(fd, "\n");
    dump_c_stack(fd);
    abort();
}

/* Explicit dumps. */

/* fh_dump(fd, all_threads) */
static PyObject *
prim_fh_dump(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long fd;
    if (nargs != 2 || lu_long_as_long(args[0], &fd) < 0) {
        lu_err_format(PyExc_TypeError, "fh_dump(fd, all_threads)");
        return NULL;
    }
    dump_threads((int)fd, PyThreadState_Get(), args[1] == Py_True);
    return Py_NewRef(Py_None);
}

static PyObject *
prim_fh_dump_c_stack(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long fd;
    if (nargs != 1 || lu_long_as_long(args[0], &fd) < 0) {
        lu_err_format(PyExc_TypeError, "fh_dump_c_stack(fd)");
        return NULL;
    }
    dump_c_stack((int)fd);
    return Py_NewRef(Py_None);
}

/* dump_traceback_later: one watchdog thread, waiting on a condition. */

static struct {
    pthread_t thread;
    int running;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int cancel;
    long long timeout_ns;
    int repeat, fd, exit_process;
    char header[80];
} later = { .lock = PTHREAD_MUTEX_INITIALIZER };

static void *
watchdog(void *arg)
{
    pthread_mutex_lock(&later.lock);
    while (!later.cancel) {
        struct timespec deadline;
        clock_gettime(CLOCK_MONOTONIC, &deadline);
        deadline.tv_sec += later.timeout_ns / 1000000000LL;
        deadline.tv_nsec += later.timeout_ns % 1000000000LL;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
        int err = 0;
        while (!later.cancel && err != ETIMEDOUT) {
            err = pthread_cond_timedwait(&later.cond, &later.lock, &deadline);
        }
        if (later.cancel) {
            break;
        }
        outs(later.fd, later.header);
        dump_threads(later.fd, NULL, 1);
        if (later.exit_process) {
            _exit(1);
        }
        if (!later.repeat) {
            break;
        }
    }
    pthread_mutex_unlock(&later.lock);
    return NULL;
}

static void
stop_watchdog(void)
{
    if (!later.running) {
        return;
    }
    pthread_mutex_lock(&later.lock);
    later.cancel = 1;
    pthread_cond_signal(&later.cond);
    pthread_mutex_unlock(&later.lock);
    pthread_join(later.thread, NULL);
    later.running = 0;
}

/* fh_dump_later(timeout_us, repeat, fd, exit, header) */
static PyObject *
prim_fh_dump_later(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long us, fd;
    if (nargs != 5 || lu_long_as_long(args[0], &us) < 0 || lu_long_as_long(args[2], &fd) < 0
        || !PyUnicode_Check(args[4])) {
        lu_err_format(PyExc_TypeError, "fh_dump_later(timeout_us, repeat, fd, exit, header)");
        return NULL;
    }
    stop_watchdog();
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&later.cond, &attr);
    pthread_condattr_destroy(&attr);
    later.cancel = 0;
    later.timeout_ns = (long long)us * 1000;
    later.repeat = args[1] == Py_True;
    later.fd = (int)fd;
    later.exit_process = args[3] == Py_True;
    snprintf(later.header, sizeof later.header, "%s", PyUnicode_AsUTF8(args[4]));
    if (pthread_create(&later.thread, NULL, watchdog, NULL) != 0) {
        lu_err_format(PyExc_RuntimeError, "unable to start watchdog thread");
        return NULL;
    }
    later.running = 1;
    return Py_NewRef(Py_None);
}

static PyObject *
prim_fh_cancel_later(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    stop_watchdog();
    return Py_NewRef(Py_None);
}

/* register: a user signal dumps the stack of the registering task. */

static struct user_signal {
    int registered, fd, all_threads, chain;
    PyThreadState *ts;
    struct sigaction previous;
} user_signals[NSIG];

static void
user_handler(int signum, siginfo_t *info, void *context)
{
    int saved_errno = errno;
    struct user_signal *u = &user_signals[signum];
    PyThreadState *ts = PyThreadState_GetUnchecked();
    dump_threads(u->fd, ts != NULL ? ts : u->ts, u->all_threads);
    if (u->chain) {
        if (u->previous.sa_flags & SA_SIGINFO) {
            if (u->previous.sa_sigaction != NULL) {
                u->previous.sa_sigaction(signum, info, context);
            }
        }
        else if (u->previous.sa_handler != SIG_DFL && u->previous.sa_handler != SIG_IGN
                 && u->previous.sa_handler != NULL) {
            u->previous.sa_handler(signum);
        }
    }
    errno = saved_errno;
}

/* fh_register(signum, fd, all_threads, chain): 1, or 0 when the signal is not valid here. */
static PyObject *
prim_fh_register(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long signum, fd;
    if (nargs != 4 || lu_long_as_long(args[0], &signum) < 0 || lu_long_as_long(args[1], &fd) < 0) {
        lu_err_format(PyExc_TypeError, "fh_register(signum, fd, all_threads, chain)");
        return NULL;
    }
    if (signum < 1 || signum >= NSIG) {
        return Py_NewRef(Py_False);
    }
    struct user_signal *u = &user_signals[signum];
    u->fd = (int)fd;
    u->all_threads = args[2] == Py_True;
    u->chain = args[3] == Py_True;
    u->ts = PyThreadState_Get();
    if (!u->registered) {
        install_altstack();
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = user_handler;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK | (u->chain ? 0 : SA_NODEFER);
        sigemptyset(&sa.sa_mask);
        if (sigaction((int)signum, &sa, &u->previous) < 0) {
            return Py_NewRef(Py_False);
        }
        u->registered = 1;
    }
    return Py_NewRef(Py_True);
}

static PyObject *
prim_fh_unregister(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long signum;
    if (nargs != 1 || lu_long_as_long(args[0], &signum) < 0 || signum < 1 || signum >= NSIG) {
        return Py_NewRef(Py_False);
    }
    struct user_signal *u = &user_signals[signum];
    if (!u->registered) {
        return Py_NewRef(Py_False);
    }
    sigaction((int)signum, &u->previous, NULL);
    u->registered = 0;
    return Py_NewRef(Py_True);
}

/* The faults the tests provoke. */

static PyObject *
prim_fh_sigsegv(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    raise(SIGSEGV);
    return Py_NewRef(Py_None);
}

static PyObject *
prim_fh_sigabrt(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    abort();
}

static PyObject *
prim_fh_sigfpe(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    raise(SIGFPE);
    return Py_NewRef(Py_None);
}

static PyObject *
prim_fh_read_null(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    volatile int *p = NULL;
    return PyLong_FromLong(*p);
}

static volatile int sink;

static int
recurse(int n)
{
    volatile char pad[512];
    pad[0] = (char)n;
    sink += pad[0];
    return recurse(n + 1) + pad[1];
}

static PyObject *
prim_fh_stack_overflow(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    return PyLong_FromLong(recurse(0));
}

static void *
fatal_thread(void *arg)
{
    lu_fh_fatal("faulthandler_fatal_error_thread", "in new thread");
    return NULL;
}

static PyObject *
prim_fh_fatal_error_c_thread(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, fatal_thread, NULL) != 0) {
        lu_err_format(PyExc_RuntimeError, "unable to create the thread");
        return NULL;
    }
    pthread_join(thread, NULL);
    return Py_NewRef(Py_None);
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_faulthandler_methods[] = {
    FAST(fh_enable), FAST(fh_disable), FAST(fh_is_enabled), FAST(fh_dump), FAST(fh_dump_c_stack),
    FAST(fh_dump_later), FAST(fh_cancel_later), FAST(fh_register), FAST(fh_unregister),
    FAST(fh_sigsegv), FAST(fh_sigabrt), FAST(fh_sigfpe), FAST(fh_read_null), FAST(fh_stack_overflow),
    FAST(fh_fatal_error_c_thread),
    { NULL, NULL, 0, NULL },
};
