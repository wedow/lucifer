/* The switch out of the serial state with the process-wide barrier refused
   (a seccomp filter), raced by the first task itself: thread F becomes the
   second task and, for want of the barrier, stops the world; the first
   task, attached in C and never polling, then makes a task of its own
   (PyThreadState_New), which waits for the switch. It must wait detached,
   so that the stop can count it as stopped: both go on. */

#include "Python.h"

#include <errno.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "serial_sandbox_new.c:%d: check failed: %s\n", __LINE__, #cond); \
    failures++; } } while (0)

static int
refuse_membarrier(void)
{
    struct sock_filter code[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_membarrier, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    struct sock_fprog prog = { sizeof code / sizeof code[0], code };
    return prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 && prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) == 0;
}

static double
now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void *
f_main(void *arg)
{
    PyGILState_Release(PyGILState_Ensure());
    return NULL;
}

__attribute__((constructor)) static void
check(void)
{
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    PyStatus status = Py_InitializeFromConfig(&config);
    CHECK(!PyStatus_Exception(status));
    CHECK(refuse_membarrier());
    pthread_t f;
    pthread_create(&f, NULL, f_main, NULL);
    for (double end = now() + 0.3; now() < end;) {
    }
    /* F is stopping the world now, waiting for this task. */
    PyThreadState *ts = PyThreadState_New(PyThreadState_Get()->interp);
    CHECK(ts != NULL);
    PyThreadState_Clear(ts);
    PyThreadState_Delete(ts);
    Py_BEGIN_ALLOW_THREADS
    pthread_join(f, NULL);
    Py_END_ALLOW_THREADS
    if (failures == 0) {
        puts("capi serial_sandbox_new ok");
    }
    fflush(stdout);
    _exit(failures != 0);
}
