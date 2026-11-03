/* Platform primitives and the runtime's blocking calls. Every call that may
   block is made here, inside LU_SYSCALL, so that its task counts as detached
   while it waits (DESIGN 5.5); tools/blockcheck enforces it. */

#define _GNU_SOURCE
#include "prim/prim.h"

#include <errno.h>
#include <linux/futex.h>
#include <string.h>
#include <sys/syscall.h>
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
