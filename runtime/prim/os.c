/* Platform primitives and the runtime's blocking calls. Every call that may
   block is made here, inside LU_SYSCALL, so that its task counts as detached
   while it waits (DESIGN 5.5); tools/blockcheck enforces it. */

#define _GNU_SOURCE
#include "prim/prim.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <langinfo.h>
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
    { "os_waitpid", (PyCFunction)(void (*)(void))prim_os_waitpid, METH_FASTCALL, NULL },
    { "locale_encoding", (PyCFunction)(void (*)(void))prim_os_locale_encoding, METH_FASTCALL, NULL },
    { NULL, NULL, 0, NULL },
};
