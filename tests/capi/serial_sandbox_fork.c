/* A fork during the switch out of the serial state with the process-wide
   barrier refused (a seccomp filter): thread F becomes the second task and,
   for want of the barrier, stops the world while holding the switch lock;
   the first task, attached in C and never polling, then forks (os.fork).
   The fork takes the switch lock before it stops the world, and waits for it
   detached, so the stop counts the first task as stopped and the switch
   completes; then the fork goes on. Both processes finish. */

#include "Python.h"

#include <errno.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "serial_sandbox_fork.c:%d: check failed: %s\n", __LINE__, #cond); \
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
    PyObject *os = PyImport_ImportModule("os");
    CHECK(os != NULL);
    pthread_t f;
    pthread_create(&f, NULL, f_main, NULL);
    for (double end = now() + 0.3; now() < end;) {
    }
    /* F holds the switch lock and is stopping the world, waiting for this task. */
    PyObject *pid = PyObject_CallMethod(os, "fork", NULL);
    CHECK(pid != NULL);
    long p = pid != NULL ? PyLong_AsLong(pid) : -1;
    if (p == 0) {
        _exit(0);
    }
    Py_XDECREF(pid);
    Py_DECREF(os);
    int code = -1;
    Py_BEGIN_ALLOW_THREADS
    if (p > 0) {
        int st;
        if (waitpid((pid_t)p, &st, 0) == (pid_t)p && WIFEXITED(st)) {
            code = WEXITSTATUS(st);
        }
    }
    pthread_join(f, NULL);
    Py_END_ALLOW_THREADS
    CHECK(code == 0);
    if (failures == 0) {
        puts("capi serial_sandbox_fork ok");
    }
    fflush(stdout);
    _exit(failures != 0);
}
