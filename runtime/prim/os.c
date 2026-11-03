/* Platform primitives and the runtime's blocking calls. Every call that may
   block is made here, inside LU_SYSCALL, so that its task counts as detached
   while it waits (DESIGN 5.5); tools/blockcheck enforces it. */

#define _GNU_SOURCE
#include "prim/prim.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <langinfo.h>
#include <poll.h>
#include <locale.h>
#include <linux/futex.h>
#include <signal.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/* Evaluates the blocking call `call` between lu_syscall_begin and
   lu_syscall_end, which keeps errno. */
#define LU_SYSCALL(call) ({ \
        PyThreadState *lu_ts_ = lu_syscall_begin(); \
        __typeof__(call) lu_r_ = (call); \
        lu_syscall_end(lu_ts_); \
        lu_r_; })

/* lu_park's own futex calls, outside the bracket: lu_park brackets its
   whole sleep, and a wake never blocks. */
#define LU_PARKED(call) (call)

int
lu_futex_wait(uint32_t *word, uint32_t expected, PyTime_t deadline)
{
    struct timespec ts = { deadline / 1000000000, deadline % 1000000000 };
    /* FUTEX_WAIT_BITSET takes an absolute CLOCK_MONOTONIC deadline. */
    long r = LU_PARKED(syscall(SYS_futex, word, FUTEX_WAIT_BITSET_PRIVATE, expected,
                               deadline < 0 ? NULL : &ts, NULL, FUTEX_BITSET_MATCH_ANY));
    return r == 0 ? 0 : errno;
}

void
lu_futex_wake(uint32_t *word)
{
    LU_PARKED(syscall(SYS_futex, word, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0));
}

/* write(fd, str): the whole UTF-8 form of str, retrying short writes and
   EINTR. Raises OSError on failure; the errno-carrying OSError layout
   arrives with K1k. */
PyObject *
lu_prim_write(PyObject *module, PyObject *const *args, Py_ssize_t nargs)
{
    long fd;
    if (nargs != 2 || !PyLong_CheckExact(args[0]) || !PyUnicode_CheckExact(args[1])) {
        lu_err_format(PyExc_TypeError, "write(fd, str) takes an int and a str");
        return NULL;
    }
    if (lu_long_as_long(args[0], &fd) < 0) {
        return NULL;
    }
    Py_ssize_t size;
    const char *data = PyUnicode_AsUTF8AndSize(args[1], &size);
    if (data == NULL) {
        return NULL;
    }
    while (size > 0) {
        ssize_t n = LU_SYSCALL(write((int)fd, data, (size_t)size));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            lu_err_format(PyExc_OSError, "[Errno %d] %s", errno, strerror(errno));
            return NULL;
        }
        data += n;
        size -= n;
    }
    return Py_None;
}

/* The operating system under the posix and time ports. A failing call
   raises the OSError subclass its errno selects, with the file name when one
   was given. */

extern char **environ;

static void
set_oserror(int err, PyObject *filename)
{
    PyObject *args[3] = { PyLong_FromLong(err), PyUnicode_FromString(strerror(err)), filename };
    PyObject *exc = args[0] != NULL && args[1] != NULL
        ? lu_call(PyExc_OSError, args, filename != NULL ? 3 : 2, NULL) : NULL;
    Py_XDECREF(args[0]);
    Py_XDECREF(args[1]);
    if (exc != NULL) {
        PyErr_SetObject((PyObject *)Py_TYPE(exc), exc);
        Py_DECREF(exc);
    }
}

/* A path argument as a C string: a str (UTF-8) or bytes without a NUL. */
static const char *
path_arg(PyObject *o)
{
    const char *s = NULL;
    Py_ssize_t n = 0;
    if (PyUnicode_Check(o)) {
        s = PyUnicode_AsUTF8AndSize(o, &n);
    }
    else if (PyBytes_Check(o)) {
        s = PyBytes_AS_STRING(o);
        n = PyBytes_GET_SIZE(o);
    }
    else {
        lu_err_format(PyExc_TypeError, "expected str or bytes, not %.200s", Py_TYPE(o)->tp_name);
    }
    if (s != NULL && (Py_ssize_t)strlen(s) != n) {
        lu_err_format(PyExc_ValueError, "embedded null byte");
        return NULL;
    }
    return s;
}

static PyObject *
from_locale(const char *s)
{
    wchar_t *w = Py_DecodeLocale(s, NULL);
    PyObject *r = w != NULL ? PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND, w, (Py_ssize_t)wcslen(w))
                            : PyErr_NoMemory();
    PyMem_RawFree(w);
    return r;
}

static int
int_arg(PyObject *o, long *out)
{
    if (!PyLong_Check(o)) {
        lu_err_format(PyExc_TypeError, "an integer is required");
        return -1;
    }
    return lu_long_as_long(o, out);
}

/* getcwd() */
PyObject *
lu_prim_os_getcwd(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    char buf[4096];
    if (getcwd(buf, sizeof buf) == NULL) {
        set_oserror(errno, NULL);
        return NULL;
    }
    return from_locale(buf);
}

/* The fields of a struct stat: mode, ino, dev, nlink, uid, gid, size, then
   atime, mtime and ctime as seconds and nanoseconds, blksize, blocks, rdev. */
static PyObject *
stat_tuple(const struct stat *st)
{
    long long v[19] = { st->st_mode, st->st_ino, st->st_dev, st->st_nlink, st->st_uid, st->st_gid,
        st->st_size, st->st_atim.tv_sec, st->st_atim.tv_nsec, st->st_mtim.tv_sec,
        st->st_mtim.tv_nsec, st->st_ctim.tv_sec, st->st_ctim.tv_nsec, st->st_blksize,
        st->st_blocks, st->st_rdev };
    PyObject *t = PyTuple_New(16);
    for (int i = 0; t != NULL && i < 16; i++) {
        PyObject *n = i == 1 || i == 2 || i == 15 ? PyLong_FromLongLong((long long)(unsigned long long)v[i])
                                                    : PyLong_FromLongLong(v[i]);
        if (n == NULL) {
            Py_CLEAR(t);
            break;
        }
        PyTuple_SET_ITEM(t, i, n);
    }
    return t;
}

/* stat(path, follow_symlinks) */
PyObject *
lu_prim_os_stat(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    const char *path = nargs == 2 ? path_arg(args[0]) : NULL;
    if (path == NULL) {
        return PyErr_Occurred() ? NULL : (lu_err_format(PyExc_TypeError, "stat(path, follow)"), NULL);
    }
    struct stat st;
    int r = args[1] == Py_True ? LU_SYSCALL(stat(path, &st)) : LU_SYSCALL(lstat(path, &st));
    if (r < 0) {
        set_oserror(errno, args[0]);
        return NULL;
    }
    return stat_tuple(&st);
}

/* fstat(fd) */
PyObject *
lu_prim_os_fstat(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long fd;
    struct stat st;
    if (nargs != 1 || int_arg(args[0], &fd) < 0) {
        return NULL;
    }
    if (LU_SYSCALL(fstat((int)fd, &st)) < 0) {
        set_oserror(errno, NULL);
        return NULL;
    }
    return stat_tuple(&st);
}

/* listdir(path): the entry names, without . and .. */
PyObject *
lu_prim_os_listdir(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    const char *path = nargs == 1 ? path_arg(args[0]) : NULL;
    if (path == NULL) {
        return NULL;
    }
    DIR *dir = LU_SYSCALL(opendir(path));
    if (dir == NULL) {
        set_oserror(errno, args[0]);
        return NULL;
    }
    PyObject *names = PyList_New(0);
    for (;;) {
        errno = 0;
        struct dirent *e = LU_SYSCALL(readdir(dir));
        if (e == NULL) {
            if (errno != 0) {
                set_oserror(errno, args[0]);
                Py_CLEAR(names);
            }
            break;
        }
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        PyObject *name = from_locale(e->d_name);
        if (name == NULL || lu_list_append(names, name) < 0) {
            Py_XDECREF(name);
            Py_CLEAR(names);
            break;
        }
        Py_DECREF(name);
    }
    LU_SYSCALL(closedir(dir));
    return names;
}

/* open(path, flags, mode) */
PyObject *
lu_prim_os_open(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long flags, mode;
    const char *path = nargs == 3 ? path_arg(args[0]) : NULL;
    if (path == NULL || int_arg(args[1], &flags) < 0 || int_arg(args[2], &mode) < 0) {
        return NULL;
    }
    int fd;
    do {
        fd = LU_SYSCALL(open(path, (int)flags | O_CLOEXEC, (mode_t)mode));
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        set_oserror(errno, args[0]);
        return NULL;
    }
    return PyLong_FromLong(fd);
}

/* mkdir(path, mode), rmdir(path), unlink(path) */
static PyObject *
path_call(PyObject *const *args, Py_ssize_t nargs, int which)
{
    long mode = 0777;
    const char *path = nargs >= 1 ? path_arg(args[0]) : NULL;
    if (path == NULL || (which == 0 && (nargs != 2 || int_arg(args[1], &mode) < 0))
        || (which != 0 && nargs != 1)) {
        return NULL;
    }
    int r = which == 0 ? LU_SYSCALL(mkdir(path, (mode_t)mode))
        : which == 1 ? LU_SYSCALL(rmdir(path)) : LU_SYSCALL(unlink(path));
    if (r < 0) {
        set_oserror(errno, args[0]);
        return NULL;
    }
    return Py_NewRef(Py_None);
}

static PyObject *
prim_os_mkdir(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    return path_call(args, nargs, 0);
}

static PyObject *
prim_os_rmdir(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    return path_call(args, nargs, 1);
}

static PyObject *
prim_os_unlink(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    return path_call(args, nargs, 2);
}

/* cpu_count(): the processors online. */
static PyObject *
prim_os_cpu_count(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n < 1 ? Py_NewRef(Py_None) : PyLong_FromLong(n);
}

/* rename(src, dst): the OSError names both paths, as CPython's does. */
static PyObject *
prim_os_rename(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    const char *src = nargs == 2 ? path_arg(args[0]) : NULL;
    const char *dst = src != NULL ? path_arg(args[1]) : NULL;
    if (dst == NULL) {
        return NULL;
    }
    if (LU_SYSCALL(rename(src, dst)) < 0) {
        int err = errno;
        PyObject *a[5] = { PyLong_FromLong(err), PyUnicode_FromString(strerror(err)), args[0], Py_None, args[1] };
        PyObject *exc = a[0] != NULL && a[1] != NULL ? lu_call(PyExc_OSError, a, 5, NULL) : NULL;
        Py_XDECREF(a[0]);
        Py_XDECREF(a[1]);
        if (exc != NULL) {
            PyErr_SetObject((PyObject *)Py_TYPE(exc), exc);
            Py_DECREF(exc);
        }
        return NULL;
    }
    return Py_NewRef(Py_None);
}

/* close(fd) */
PyObject *
lu_prim_os_close(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long fd;
    if (nargs != 1 || int_arg(args[0], &fd) < 0) {
        return NULL;
    }
    if (LU_SYSCALL(close((int)fd)) < 0 && errno != EINTR) {
        set_oserror(errno, NULL);
        return NULL;
    }
    return Py_NewRef(Py_None);
}

/* read(fd, size): up to size bytes. */
PyObject *
lu_prim_os_read(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long fd, size;
    if (nargs != 2 || int_arg(args[0], &fd) < 0 || int_arg(args[1], &size) < 0) {
        return NULL;
    }
    if (size < 0) {
        lu_err_format(PyExc_ValueError, "negative buffer size in read");
        return NULL;
    }
    PyObject *bytes = PyBytes_FromStringAndSize(NULL, size);
    if (bytes == NULL) {
        return NULL;
    }
    ssize_t n;
    do {
        n = LU_SYSCALL(read((int)fd, PyBytes_AS_STRING(bytes), (size_t)size));
    } while (n < 0 && errno == EINTR);
    if (n < 0) {
        set_oserror(errno, NULL);
        Py_DECREF(bytes);
        return NULL;
    }
    if (n < size) {
        PyObject *shorter = PyBytes_FromStringAndSize(PyBytes_AS_STRING(bytes), n);
        Py_DECREF(bytes);
        return shorter;
    }
    return bytes;
}

/* write(fd, bytes): the count written. */
PyObject *
lu_prim_os_write(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long fd;
    if (nargs != 2 || int_arg(args[0], &fd) < 0 || !PyBytes_Check(args[1])) {
        lu_err_format(PyExc_TypeError, "os_write(fd, bytes)");
        return NULL;
    }
    ssize_t n;
    do {
        n = LU_SYSCALL(write((int)fd, PyBytes_AS_STRING(args[1]), (size_t)PyBytes_GET_SIZE(args[1])));
    } while (n < 0 && errno == EINTR);
    if (n < 0) {
        set_oserror(errno, NULL);
        return NULL;
    }
    return PyLong_FromLongLong(n);
}

PyObject *
lu_prim_os_getpid(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    return PyLong_FromLong(getpid());
}

/* environ(): the environment now, as a list of the bytes NAME=value. */
PyObject *
lu_prim_os_environ(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *list = PyList_New(0);
    for (char **e = environ; list != NULL && *e != NULL; e++) {
        PyObject *entry = PyBytes_FromString(*e);
        if (entry == NULL || lu_list_append(list, entry) < 0) {
            Py_XDECREF(entry);
            Py_CLEAR(list);
            break;
        }
        Py_DECREF(entry);
    }
    return list;
}

/* os_exit(status): leaves at once, with no cleanup. */
PyObject *
lu_prim_os_exit(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long status;
    if (nargs != 1 || int_arg(args[0], &status) < 0) {
        return NULL;
    }
    _exit((int)status);
}

/* clock_gettime(clock_id): nanoseconds. */
PyObject *
lu_prim_os_clock_gettime(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long clock;
    struct timespec ts;
    if (nargs != 1 || int_arg(args[0], &clock) < 0) {
        return NULL;
    }
    if (clock_gettime((clockid_t)clock, &ts) < 0) {
        set_oserror(errno, NULL);
        return NULL;
    }
    return PyLong_FromLongLong((long long)ts.tv_sec * 1000000000 + ts.tv_nsec);
}

/* sleep(ns): parks the task until the deadline. */
PyObject *
lu_prim_os_sleep(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long ns;
    if (nargs != 1 || int_arg(args[0], &ns) < 0) {
        return NULL;
    }
    struct lu_wait w;
    lu_wait_begin(&w);
    lu_park(&w, lu_monotonic_ns() + ns, 1);
    return Py_NewRef(Py_None);
}

/* lseek(fd, offset, whence): the new position. */
static PyObject *
prim_os_lseek(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long fd, offset, whence;
    if (nargs != 3 || int_arg(args[0], &fd) < 0 || int_arg(args[1], &offset) < 0
        || int_arg(args[2], &whence) < 0) {
        return NULL;
    }
    off_t r = lseek((int)fd, (off_t)offset, (int)whence);
    if (r < 0) {
        set_oserror(errno, NULL);
        return NULL;
    }
    return PyLong_FromLongLong(r);
}

/* isatty(fd) */
static PyObject *
prim_os_isatty(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long fd;
    if (nargs != 1 || int_arg(args[0], &fd) < 0) {
        return NULL;
    }
    return Py_NewRef(isatty((int)fd) ? Py_True : Py_False);
}

/* ftruncate(fd, length) */
static PyObject *
prim_os_ftruncate(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long fd, length;
    if (nargs != 2 || int_arg(args[0], &fd) < 0 || int_arg(args[1], &length) < 0) {
        return NULL;
    }
    if (ftruncate((int)fd, (off_t)length) < 0) {
        set_oserror(errno, NULL);
        return NULL;
    }
    return Py_NewRef(Py_None);
}

/* dup(fd) and dup2(fd, fd2): the new descriptor. */
static PyObject *
prim_os_dup(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long fd, fd2 = -1;
    if (nargs < 1 || nargs > 2 || int_arg(args[0], &fd) < 0 || (nargs == 2 && int_arg(args[1], &fd2) < 0)) {
        return NULL;
    }
    int r = nargs == 1 ? dup((int)fd) : dup2((int)fd, (int)fd2);
    if (r < 0) {
        set_oserror(errno, NULL);
        return NULL;
    }
    return PyLong_FromLong(r);
}

/* strerror(code) */
static PyObject *
prim_os_strerror(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long code;
    if (nargs != 1 || int_arg(args[0], &code) < 0) {
        return NULL;
    }
    return from_locale(strerror((int)code));
}

/* locale_encoding(): the codeset of the user's LC_CTYPE locale (nl_langinfo
   CODESET), or utf-8 when the locale has none. */
static PyObject *
prim_os_locale_encoding(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    locale_t locale = newlocale(LC_CTYPE_MASK, "", (locale_t)0);
    const char *codeset = locale != (locale_t)0 ? nl_langinfo_l(CODESET, locale) : NULL;
    PyObject *name = PyUnicode_FromString(codeset != NULL && codeset[0] != '\0' ? codeset : "utf-8");
    if (locale != (locale_t)0) {
        freelocale(locale);
    }
    return name;
}

/* The calls with a plain integer result, by name: os_call(name, arg...) with
   each argument a path (str or bytes) or an int, as the entry's spec says
   ('p' or 'i'). A result of -1 with errno set raises, naming the first path. */
union sys_arg {
    const char *p;
    long i;
};

/* The calls on the blocking list carry their own LU_SYSCALL. */
#define SYS_CALLS(X) \
    X(sys_unlink, "p", unlink(a[0].p)) \
    X(sys_rmdir, "p", rmdir(a[0].p)) \
    X(sys_mkdir, "pi", mkdir(a[0].p, (mode_t)a[1].i)) \
    X(sys_mkfifo, "pi", mkfifo(a[0].p, (mode_t)a[1].i)) \
    X(sys_chdir, "p", chdir(a[0].p)) \
    X(sys_chmod, "pi", chmod(a[0].p, (mode_t)a[1].i)) \
    X(sys_fchmod, "ii", fchmod((int)a[0].i, (mode_t)a[1].i)) \
    X(sys_chown, "pii", chown(a[0].p, (uid_t)a[1].i, (gid_t)a[2].i)) \
    X(sys_lchown, "pii", lchown(a[0].p, (uid_t)a[1].i, (gid_t)a[2].i)) \
    X(sys_access, "pi", access(a[0].p, (int)a[1].i)) \
    X(sys_rename, "pp", rename(a[0].p, a[1].p)) \
    X(sys_link, "pp", link(a[0].p, a[1].p)) \
    X(sys_symlink, "pp", symlink(a[0].p, a[1].p)) \
    X(sys_truncate, "pi", truncate(a[0].p, (off_t)a[1].i)) \
    X(sys_fsync, "i", LU_SYSCALL(fsync((int)a[0].i))) \
    X(sys_fdatasync, "i", LU_SYSCALL(fdatasync((int)a[0].i))) \
    X(sys_umask, "i", umask((mode_t)a[0].i)) \
    X(sys_getuid, "", getuid()) \
    X(sys_geteuid, "", geteuid()) \
    X(sys_getgid, "", getgid()) \
    X(sys_getegid, "", getegid()) \
    X(sys_getppid, "", getppid()) \
    X(sys_getpgrp, "", getpgrp()) \
    X(sys_setsid, "", setsid()) \
    X(sys_kill, "ii", kill((pid_t)a[0].i, (int)a[1].i)) \
    X(sys_fcntl, "iii", LU_SYSCALL(fcntl((int)a[0].i, (int)a[1].i, a[2].i))) \
    X(sys_sysconf, "i", sysconf((int)a[0].i)) \
    X(sys_utime, "piiiii", utimensat(AT_FDCWD, a[0].p, (struct timespec[2]){ { a[1].i, a[2].i }, { a[3].i, a[4].i } }, (int)a[5].i))

#define X(NAME, SPEC, EXPR) static long NAME(union sys_arg *a) { return EXPR; }
SYS_CALLS(X)
#undef X

static const struct sys_entry {
    const char *name, *spec;
    long (*fn)(union sys_arg *);
} sys_table[] = {
#define X(NAME, SPEC, EXPR) { #NAME + 4, SPEC, NAME },
    SYS_CALLS(X)
#undef X
};

static PyObject *
prim_os_call(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    const char *name = nargs >= 1 && PyUnicode_Check(args[0]) ? PyUnicode_AsUTF8(args[0]) : NULL;
    if (name == NULL) {
        return PyErr_Occurred() ? NULL : (lu_err_format(PyExc_TypeError, "os_call(name, ...)"), NULL);
    }
    for (size_t k = 0; k < sizeof sys_table / sizeof sys_table[0]; k++) {
        const struct sys_entry *e = &sys_table[k];
        if (strcmp(e->name, name) != 0) {
            continue;
        }
        union sys_arg a[6];
        PyObject *filename = NULL;
        if ((size_t)nargs != strlen(e->spec) + 1) {
            lu_err_format(PyExc_TypeError, "%s takes %zu arguments", name, strlen(e->spec));
            return NULL;
        }
        for (Py_ssize_t i = 0; i < nargs - 1; i++) {
            if (e->spec[i] == 'p') {
                filename = filename != NULL ? filename : args[i + 1];
                if ((a[i].p = path_arg(args[i + 1])) == NULL) {
                    return NULL;
                }
            }
            else if (int_arg(args[i + 1], &a[i].i) < 0) {
                return NULL;
            }
        }
        errno = 0;
        long r = e->fn(a);
        if (r < 0 && errno != 0) {
            set_oserror(errno, filename);
            return NULL;
        }
        return PyLong_FromLong(r);
    }
    lu_err_format(PyExc_ValueError, "no such call: %s", name);
    return NULL;
}

/* readlink(path) */
static PyObject *
prim_os_readlink(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    const char *path = nargs == 1 ? path_arg(args[0]) : NULL;
    char buf[4096];
    if (path == NULL) {
        return PyErr_Occurred() ? NULL : (lu_err_format(PyExc_TypeError, "readlink(path)"), NULL);
    }
    ssize_t n = LU_SYSCALL(readlink(path, buf, sizeof buf - 1));
    if (n < 0) {
        set_oserror(errno, args[0]);
        return NULL;
    }
    buf[n] = '\0';
    return from_locale(buf);
}

/* urandom(size) */
static PyObject *
prim_os_urandom(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long size;
    if (nargs != 1 || int_arg(args[0], &size) < 0) {
        return NULL;
    }
    if (size < 0) {
        lu_err_format(PyExc_ValueError, "negative argument not allowed");
        return NULL;
    }
    PyObject *bytes = PyBytes_FromStringAndSize(NULL, size);
    char *p = bytes != NULL ? PyBytes_AS_STRING(bytes) : NULL;
    for (long left = size; p != NULL && left > 0;) {
        ssize_t n = LU_SYSCALL(getrandom(p + (size - left), (size_t)left, 0));
        if (n < 0 && errno != EINTR) {
            set_oserror(errno, NULL);
            Py_CLEAR(bytes);
            break;
        }
        left -= n > 0 ? n : 0;
    }
    return bytes;
}

/* pipe(): (read end, write end), close-on-exec. */
static PyObject *
prim_os_pipe(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) < 0) {
        set_oserror(errno, NULL);
        return NULL;
    }
    PyObject *r = PyLong_FromLong(fds[0]), *w = PyLong_FromLong(fds[1]);
    PyObject *t = r != NULL && w != NULL ? PyTuple_Pack(2, r, w) : NULL;
    Py_XDECREF(r);
    Py_XDECREF(w);
    return t;
}

/* uname(): (sysname, nodename, release, version, machine) */
static PyObject *
prim_os_uname(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    struct utsname u;
    if (uname(&u) < 0) {
        set_oserror(errno, NULL);
        return NULL;
    }
    const char *f[5] = { u.sysname, u.nodename, u.release, u.version, u.machine };
    PyObject *t = PyTuple_New(5);
    for (int i = 0; t != NULL && i < 5; i++) {
        PyObject *s = from_locale(f[i]);
        if (s == NULL) {
            Py_CLEAR(t);
            break;
        }
        PyTuple_SET_ITEM(t, i, s);
    }
    return t;
}

/* putenv(name, value) and, with value None, unsetenv(name): both bytes. */
static PyObject *
prim_os_putenv(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    const char *name = nargs == 2 ? path_arg(args[0]) : NULL;
    const char *value = name != NULL && args[1] != Py_None ? path_arg(args[1]) : NULL;
    if (name == NULL || (args[1] != Py_None && value == NULL)) {
        return PyErr_Occurred() ? NULL : (lu_err_format(PyExc_TypeError, "putenv(name, value)"), NULL);
    }
    if ((value != NULL ? setenv(name, value, 1) : unsetenv(name)) < 0) {
        set_oserror(errno, NULL);
        return NULL;
    }
    return Py_NewRef(Py_None);
}

/* scandir(path): the entries as (name, d_type, inode), without . and .. */
static PyObject *
prim_os_scandir(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    const char *path = nargs == 1 ? path_arg(args[0]) : NULL;
    if (path == NULL) {
        return PyErr_Occurred() ? NULL : (lu_err_format(PyExc_TypeError, "scandir(path)"), NULL);
    }
    DIR *dir = LU_SYSCALL(opendir(path));
    if (dir == NULL) {
        set_oserror(errno, args[0]);
        return NULL;
    }
    PyObject *entries = PyList_New(0);
    for (;;) {
        errno = 0;
        struct dirent *e = LU_SYSCALL(readdir(dir));
        if (e == NULL) {
            if (errno != 0) {
                set_oserror(errno, args[0]);
                Py_CLEAR(entries);
            }
            break;
        }
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        PyObject *name = from_locale(e->d_name);
        PyObject *type = PyLong_FromLong(e->d_type), *ino = PyLong_FromLongLong((long long)e->d_ino);
        PyObject *entry = name != NULL && type != NULL && ino != NULL ? PyTuple_Pack(3, name, type, ino) : NULL;
        Py_XDECREF(name);
        Py_XDECREF(type);
        Py_XDECREF(ino);
        if (entry == NULL || lu_list_append(entries, entry) < 0) {
            Py_XDECREF(entry);
            Py_CLEAR(entries);
            break;
        }
        Py_DECREF(entry);
    }
    LU_SYSCALL(closedir(dir));
    return entries;
}

static int bytes_array(PyObject *seq, char ***out);

/* poll(fds, timeout_ms): fds is a list of (fd, events) pairs; the answer is
   the list of (fd, revents) of those with events, or None when a signal
   interrupted the wait (the caller runs its handlers and retries). */
static PyObject *
prim_os_poll(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long timeout;
    if (nargs != 2 || !PyList_Check(args[0]) || int_arg(args[1], &timeout) < 0) {
        return PyErr_Occurred() ? NULL : (lu_err_format(PyExc_TypeError, "poll(fds, timeout)"), NULL);
    }
    Py_ssize_t n = PyList_GET_SIZE(args[0]);
    struct pollfd *fds = PyMem_RawCalloc((size_t)n + 1, sizeof *fds);
    if (fds == NULL) {
        return PyErr_NoMemory();
    }
    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *pair = PyList_GET_ITEM(args[0], i);
        long fd, events;
        if (!PyTuple_Check(pair) || PyTuple_GET_SIZE(pair) != 2 || int_arg(PyTuple_GET_ITEM(pair, 0), &fd) < 0
            || int_arg(PyTuple_GET_ITEM(pair, 1), &events) < 0) {
            PyMem_RawFree(fds);
            return PyErr_Occurred() ? NULL : (lu_err_format(PyExc_TypeError, "poll(fds, timeout)"), NULL);
        }
        fds[i].fd = (int)fd;
        fds[i].events = (short)events;
    }
    int r = LU_SYSCALL(poll(fds, (nfds_t)n, (int)timeout));
    PyObject *result = NULL;
    if (r < 0) {
        if (errno == EINTR) {
            result = Py_NewRef(Py_None);
        }
        else {
            set_oserror(errno, NULL);
        }
    }
    else {
        result = PyList_New(0);
        for (Py_ssize_t i = 0; result != NULL && i < n; i++) {
            if (fds[i].revents == 0) {
                continue;
            }
            PyObject *fd = PyLong_FromLong(fds[i].fd), *ev = PyLong_FromLong(fds[i].revents);
            PyObject *pair = fd != NULL && ev != NULL ? PyTuple_Pack(2, fd, ev) : NULL;
            Py_XDECREF(fd);
            Py_XDECREF(ev);
            if (pair == NULL || lu_list_append(result, pair) < 0) {
                Py_XDECREF(pair);
                Py_CLEAR(result);
                break;
            }
            Py_DECREF(pair);
        }
    }
    PyMem_RawFree(fds);
    return result;
}

/* execve(path, argv, env): argv and env are tuples of bytes, env may be None for execv. */
static PyObject *
prim_os_execve(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    const char *path = nargs == 3 ? path_arg(args[0]) : NULL;
    char **argv, **envp;
    if (path == NULL) {
        return PyErr_Occurred() ? NULL : (lu_err_format(PyExc_TypeError, "execve(path, argv, env)"), NULL);
    }
    if (bytes_array(args[1], &argv) < 0 || argv == NULL) {
        return PyErr_Occurred() ? NULL : (lu_err_format(PyExc_TypeError, "execve(path, argv, env)"), NULL);
    }
    if (bytes_array(args[2], &envp) < 0) {
        PyMem_RawFree(argv);
        return NULL;
    }
    if (envp != NULL) {
        execve(path, argv, envp);
    }
    else {
        execv(path, argv);
    }
    set_oserror(errno, args[0]);
    PyMem_RawFree(argv);
    PyMem_RawFree(envp);
    return NULL;
}

/* waitpid(pid, options): (pid, status) */
static PyObject *
prim_os_waitpid(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long pid, options;
    int status = 0;
    if (nargs != 2 || int_arg(args[0], &pid) < 0 || int_arg(args[1], &options) < 0) {
        return NULL;
    }
    pid_t r;
    do {
        r = LU_SYSCALL(waitpid((pid_t)pid, &status, (int)options));
    } while (r < 0 && errno == EINTR);
    if (r < 0) {
        set_oserror(errno, NULL);
        return NULL;
    }
    PyObject *p = PyLong_FromLong(r), *s = PyLong_FromLong(status);
    PyObject *t = p != NULL && s != NULL ? PyTuple_Pack(2, p, s) : NULL;
    Py_XDECREF(p);
    Py_XDECREF(s);
    return t;
}

/* fork_exec(argv, executables, close_fds, fds_to_keep, cwd, env, p2cread,
   p2cwrite, c2pread, c2pwrite, errread, errwrite, errpipe_read, errpipe_write,
   restore_signals, call_setsid, pgid_to_set, gid, extra_groups, uid, umask):
   _posixsubprocess.fork_exec without preexec_fn. argv, executables, env and
   cwd are bytes (argv and env may be None); gid and uid are -1 when unset.
   The child makes only the calls of Modules/_posixsubprocess.c child_exec,
   none of which takes a lock, and reports a failure on errpipe_write.

   Nothing else runs in the child, so no task there can be stopped: its calls
   are made bare, under LU_FORKED, which blockcheck accepts like LU_SYSCALL. */
#define LU_FORKED(call) (call)

static void
child_write_all(int fd, const char *s, size_t n)
{
    while (n > 0) {
        ssize_t w = LU_FORKED(write(fd, s, n));
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

static int
child_set_inheritable(int fd, int inheritable)
{
    int flags = LU_FORKED(fcntl(fd, F_GETFD));
    if (flags < 0) {
        return -1;
    }
    int wanted = inheritable ? flags & ~FD_CLOEXEC : flags | FD_CLOEXEC;
    return wanted == flags ? 0 : LU_FORKED(fcntl(fd, F_SETFD, wanted));
}

struct spawn {
    char **exec_array, **argv, **envp;
    const char *cwd;
    int fds[6];                 /* p2cread, p2cwrite, c2pread, c2pwrite, errread, errwrite */
    int errpipe_read, errpipe_write, close_fds, restore_signals, call_setsid, umask;
    pid_t pgid_to_set;
    gid_t gid, *extra_groups;
    long extra_group_size;
    uid_t uid;
    int *keep;
    Py_ssize_t nkeep;
};

#define CHILD_CALL(call) do { if ((call) == -1) { goto error; } } while (0)

static void
child_exec(const struct spawn *sp)
{
    int p2cread = sp->fds[0], p2cwrite = sp->fds[1], c2pread = sp->fds[2], c2pwrite = sp->fds[3];
    int errread = sp->fds[4], errwrite = sp->fds[5];
    const char *err_msg = "noexec";
    char hex_errno[sizeof(int) * 2 + 1];
    int saved_errno;

    for (Py_ssize_t i = 0; i < sp->nkeep; i++) {
        if (sp->keep[i] != sp->errpipe_write) {
            CHILD_CALL(child_set_inheritable(sp->keep[i], 1));
        }
    }
    if (p2cwrite != -1) {
        CHILD_CALL(LU_FORKED(close(p2cwrite)));
    }
    if (c2pread != -1) {
        CHILD_CALL(LU_FORKED(close(c2pread)));
    }
    if (errread != -1) {
        CHILD_CALL(LU_FORKED(close(errread)));
    }
    CHILD_CALL(LU_FORKED(close(sp->errpipe_read)));
    if (c2pwrite == 0) {
        CHILD_CALL(c2pwrite = dup(c2pwrite));
        CHILD_CALL(child_set_inheritable(c2pwrite, 0));
    }
    while (errwrite == 0 || errwrite == 1) {
        CHILD_CALL(errwrite = dup(errwrite));
        CHILD_CALL(child_set_inheritable(errwrite, 0));
    }
    if (p2cread == 0) {
        CHILD_CALL(child_set_inheritable(p2cread, 1));
    }
    else if (p2cread != -1) {
        CHILD_CALL(dup2(p2cread, 0));
    }
    if (c2pwrite == 1) {
        CHILD_CALL(child_set_inheritable(c2pwrite, 1));
    }
    else if (c2pwrite != -1) {
        CHILD_CALL(dup2(c2pwrite, 1));
    }
    if (errwrite == 2) {
        CHILD_CALL(child_set_inheritable(errwrite, 1));
    }
    else if (errwrite != -1) {
        CHILD_CALL(dup2(errwrite, 2));
    }
    if (sp->cwd != NULL && chdir(sp->cwd) == -1) {
        err_msg = "noexec:chdir";
        goto error;
    }
    if (sp->umask >= 0) {
        umask((mode_t)sp->umask);
    }
    if (sp->restore_signals) {
        signal(SIGPIPE, SIG_DFL);
        signal(SIGXFSZ, SIG_DFL);
    }
    if (sp->call_setsid) {
        CHILD_CALL(setsid());
    }
    if (sp->pgid_to_set >= 0) {
        CHILD_CALL(setpgid(0, sp->pgid_to_set));
    }
    if (sp->extra_group_size >= 0) {
        CHILD_CALL(setgroups((size_t)sp->extra_group_size, sp->extra_groups));
    }
    if (sp->gid != (gid_t)-1) {
        CHILD_CALL(setregid(sp->gid, sp->gid));
    }
    if (sp->uid != (uid_t)-1) {
        CHILD_CALL(setreuid(sp->uid, sp->uid));
    }
    err_msg = "";
    if (sp->close_fds) {
        unsigned int start = 3;
        for (Py_ssize_t i = 0; i <= sp->nkeep; i++) {
            unsigned int stop = i < sp->nkeep ? (unsigned int)sp->keep[i] : ~0U;
            if (i < sp->nkeep && (int)stop < 3) {
                continue;
            }
            if (start < stop) {
                unsigned int last = i < sp->nkeep ? stop - 1 : ~0U;
                if (close_range(start, last, 0) < 0) {
                    for (unsigned int fd = start; fd <= last && fd < 65536; fd++) {
                        LU_FORKED(close((int)fd));
                    }
                }
            }
            start = stop + 1;
        }
    }
    saved_errno = 0;
    for (int i = 0; sp->exec_array[i] != NULL; i++) {
        if (sp->envp != NULL) {
            execve(sp->exec_array[i], sp->argv, sp->envp);
        }
        else {
            execv(sp->exec_array[i], sp->argv);
        }
        if (errno != ENOENT && errno != ENOTDIR && saved_errno == 0) {
            saved_errno = errno;
        }
    }
    if (saved_errno) {
        errno = saved_errno;
    }
error:
    saved_errno = errno;
    if (saved_errno) {
        child_write_all(sp->errpipe_write, "OSError:", 8);
        char *cur = hex_errno + sizeof(hex_errno);
        while (saved_errno != 0 && cur != hex_errno) {
            *--cur = "0123456789abcdef"[saved_errno % 16];
            saved_errno /= 16;
        }
        child_write_all(sp->errpipe_write, cur, (size_t)(hex_errno + sizeof(hex_errno) - cur));
        child_write_all(sp->errpipe_write, ":", 1);
    }
    else {
        child_write_all(sp->errpipe_write, "SubprocessError:0:", 18);
    }
    child_write_all(sp->errpipe_write, err_msg, strlen(err_msg));
}

/* A NULL-terminated array of the C strings of a tuple of bytes, or NULL
   (not an error) for None. The strings belong to the tuple. */
static int
bytes_array(PyObject *seq, char ***out)
{
    *out = NULL;
    if (seq == Py_None) {
        return 0;
    }
    if (!PyTuple_Check(seq)) {
        lu_err_format(PyExc_TypeError, "a tuple of bytes is required");
        return -1;
    }
    Py_ssize_t n = PyTuple_GET_SIZE(seq);
    char **array = PyMem_RawCalloc((size_t)n + 1, sizeof(char *));
    if (array == NULL) {
        PyErr_NoMemory();
        return -1;
    }
    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *item = PyTuple_GET_ITEM(seq, i);
        if (!PyBytes_Check(item)) {
            lu_err_format(PyExc_TypeError, "a tuple of bytes is required");
            PyMem_RawFree(array);
            return -1;
        }
        if ((Py_ssize_t)strlen(PyBytes_AS_STRING(item)) != PyBytes_GET_SIZE(item)) {
            lu_err_format(PyExc_ValueError, "embedded null byte");
            PyMem_RawFree(array);
            return -1;
        }
        array[i] = PyBytes_AS_STRING(item);
    }
    *out = array;
    return 0;
}

static PyObject *
prim_os_fork_exec(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    struct spawn sp = { .extra_group_size = -1 };
    long v[8], pgid, gid, uid, umask_;
    if (nargs != 21) {
        lu_err_format(PyExc_TypeError, "os_fork_exec() takes 21 arguments");
        return NULL;
    }
    for (int i = 0; i < 6; i++) {
        if (int_arg(args[6 + i], &v[i]) < 0) {
            return NULL;
        }
        sp.fds[i] = (int)v[i];
    }
    if (int_arg(args[12], &v[6]) < 0 || int_arg(args[13], &v[7]) < 0 || int_arg(args[16], &pgid) < 0
        || int_arg(args[17], &gid) < 0 || int_arg(args[19], &uid) < 0 || int_arg(args[20], &umask_) < 0) {
        return NULL;
    }
    sp.errpipe_read = (int)v[6];
    sp.errpipe_write = (int)v[7];
    sp.close_fds = args[2] == Py_True;
    sp.restore_signals = args[14] == Py_True;
    sp.call_setsid = args[15] == Py_True;
    sp.pgid_to_set = (pid_t)pgid;
    sp.gid = (gid_t)gid;
    sp.uid = (uid_t)uid;
    sp.umask = (int)umask_;
    sp.cwd = args[4] == Py_None ? NULL : path_arg(args[4]);
    if ((args[4] != Py_None && sp.cwd == NULL) || bytes_array(args[0], &sp.argv) < 0) {
        return NULL;
    }
    PyObject *result = NULL;
    int *keep = NULL;
    gid_t *groups = NULL;
    if (bytes_array(args[1], &sp.exec_array) < 0 || bytes_array(args[5], &sp.envp) < 0
        || sp.exec_array == NULL) {
        goto done;
    }
    Py_ssize_t nkeep = PyTuple_Check(args[3]) ? PyTuple_GET_SIZE(args[3]) : -1;
    if (nkeep < 0) {
        lu_err_format(PyExc_TypeError, "fds_to_keep must be a tuple");
        goto done;
    }
    keep = PyMem_RawCalloc((size_t)nkeep + 1, sizeof(int));
    for (Py_ssize_t i = 0; keep != NULL && i < nkeep; i++) {
        long fd;
        if (int_arg(PyTuple_GET_ITEM(args[3], i), &fd) < 0) {
            goto done;
        }
        if (fd < 0 || (i > 0 && fd <= keep[i - 1])) {
            lu_err_format(PyExc_ValueError, "bad value(s) in fds_to_keep");
            goto done;
        }
        keep[i] = (int)fd;
    }
    if (keep == NULL) {
        PyErr_NoMemory();
        goto done;
    }
    sp.keep = keep;
    sp.nkeep = nkeep;
    if (args[18] != Py_None) {
        if (!PyTuple_Check(args[18])) {
            lu_err_format(PyExc_TypeError, "extra_groups must be a tuple");
            goto done;
        }
        sp.extra_group_size = PyTuple_GET_SIZE(args[18]);
        groups = PyMem_RawCalloc((size_t)sp.extra_group_size + 1, sizeof(gid_t));
        for (long i = 0; groups != NULL && i < sp.extra_group_size; i++) {
            long g;
            if (int_arg(PyTuple_GET_ITEM(args[18], i), &g) < 0) {
                goto done;
            }
            groups[i] = (gid_t)g;
        }
        if (groups == NULL) {
            PyErr_NoMemory();
            goto done;
        }
        sp.extra_groups = groups;
    }
    pid_t pid = fork();
    if (pid == 0) {
        child_exec(&sp);
        _exit(255);
    }
    if (pid < 0) {
        set_oserror(errno, NULL);
    }
    else {
        result = PyLong_FromLong(pid);
    }
done:
    PyMem_RawFree(sp.argv);
    PyMem_RawFree(sp.exec_array);
    PyMem_RawFree(sp.envp);
    PyMem_RawFree(keep);
    PyMem_RawFree(groups);
    return result;
}

PyMethodDef lu_prim_os_methods[] = {
    { "os_getcwd", (PyCFunction)(void (*)(void))lu_prim_os_getcwd, METH_FASTCALL, NULL },
    { "os_stat", (PyCFunction)(void (*)(void))lu_prim_os_stat, METH_FASTCALL, NULL },
    { "os_fstat", (PyCFunction)(void (*)(void))lu_prim_os_fstat, METH_FASTCALL, NULL },
    { "os_listdir", (PyCFunction)(void (*)(void))lu_prim_os_listdir, METH_FASTCALL, NULL },
    { "os_open", (PyCFunction)(void (*)(void))lu_prim_os_open, METH_FASTCALL, NULL },
    { "os_close", (PyCFunction)(void (*)(void))lu_prim_os_close, METH_FASTCALL, NULL },
    { "os_cpu_count", (PyCFunction)(void (*)(void))prim_os_cpu_count, METH_FASTCALL, NULL },
    { "os_mkdir", (PyCFunction)(void (*)(void))prim_os_mkdir, METH_FASTCALL, NULL },
    { "os_rmdir", (PyCFunction)(void (*)(void))prim_os_rmdir, METH_FASTCALL, NULL },
    { "os_unlink", (PyCFunction)(void (*)(void))prim_os_unlink, METH_FASTCALL, NULL },
    { "os_rename", (PyCFunction)(void (*)(void))prim_os_rename, METH_FASTCALL, NULL },
    { "os_read", (PyCFunction)(void (*)(void))lu_prim_os_read, METH_FASTCALL, NULL },
    { "os_write", (PyCFunction)(void (*)(void))lu_prim_os_write, METH_FASTCALL, NULL },
    { "os_getpid", (PyCFunction)(void (*)(void))lu_prim_os_getpid, METH_FASTCALL, NULL },
    { "os_environ", (PyCFunction)(void (*)(void))lu_prim_os_environ, METH_FASTCALL, NULL },
    { "os_exit", (PyCFunction)(void (*)(void))lu_prim_os_exit, METH_FASTCALL, NULL },
    { "os_clock_gettime", (PyCFunction)(void (*)(void))lu_prim_os_clock_gettime, METH_FASTCALL, NULL },
    { "os_sleep", (PyCFunction)(void (*)(void))lu_prim_os_sleep, METH_FASTCALL, NULL },
    { "os_lseek", (PyCFunction)(void (*)(void))prim_os_lseek, METH_FASTCALL, NULL },
    { "os_isatty", (PyCFunction)(void (*)(void))prim_os_isatty, METH_FASTCALL, NULL },
    { "os_ftruncate", (PyCFunction)(void (*)(void))prim_os_ftruncate, METH_FASTCALL, NULL },
    { "os_dup", (PyCFunction)(void (*)(void))prim_os_dup, METH_FASTCALL, NULL },
    { "os_strerror", (PyCFunction)(void (*)(void))prim_os_strerror, METH_FASTCALL, NULL },
    { "os_call", (PyCFunction)(void (*)(void))prim_os_call, METH_FASTCALL, NULL },
    { "os_readlink", (PyCFunction)(void (*)(void))prim_os_readlink, METH_FASTCALL, NULL },
    { "os_urandom", (PyCFunction)(void (*)(void))prim_os_urandom, METH_FASTCALL, NULL },
    { "os_pipe", (PyCFunction)(void (*)(void))prim_os_pipe, METH_FASTCALL, NULL },
    { "os_uname", (PyCFunction)(void (*)(void))prim_os_uname, METH_FASTCALL, NULL },
    { "os_putenv", (PyCFunction)(void (*)(void))prim_os_putenv, METH_FASTCALL, NULL },
    { "os_scandir", (PyCFunction)(void (*)(void))prim_os_scandir, METH_FASTCALL, NULL },
    { "os_fork_exec", (PyCFunction)(void (*)(void))prim_os_fork_exec, METH_FASTCALL, NULL },
    { "os_poll", (PyCFunction)(void (*)(void))prim_os_poll, METH_FASTCALL, NULL },
    { "os_execve", (PyCFunction)(void (*)(void))prim_os_execve, METH_FASTCALL, NULL },
    { "os_waitpid", (PyCFunction)(void (*)(void))prim_os_waitpid, METH_FASTCALL, NULL },
    { "locale_encoding", (PyCFunction)(void (*)(void))prim_os_locale_encoding, METH_FASTCALL, NULL },
    { NULL, NULL, 0, NULL },
};
