/* The switch out of the serial state when the process-wide barrier it
   registered at startup is refused later, as a sandbox's seccomp filter
   installed mid-run refuses it: the switch stops the world instead. The
   first task is attached and polling, in a hot loop of stores into shared
   objects, when a foreign thread (pthread_create through ctypes) becomes the
   second task and stops it; the loop goes on and every value is accounted
   for. */

#include "Python.h"

#include <errno.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "serial_sandbox.c:%d: check failed: %s\n", __LINE__, #cond); \
    failures++; } } while (0)

/* membarrier fails with EPERM from now on; every other call goes through. */
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

static const char program[] =
    "import ctypes, threading\n"
    "class P:\n"
    "    def __init__(self): self.i = 0; self.o = None\n"
    "ps = [P() for _ in range(8)]\n"
    "xs = [None] * 32\n"
    "toks = [object() for _ in range(32)]\n"
    "done = threading.Event()\n"
    "def other(arg):\n"
    "    for k in range(200000):\n"
    "        p = ps[k & 7]; p.i = k; p.o = toks[k & 31]; xs[k & 31] = toks[(k + 1) & 31]\n"
    "    done.set()\n"
    "    return None\n"
    "libc = ctypes.CDLL(None)\n"
    "libc.pthread_create.argtypes = [ctypes.POINTER(ctypes.c_ulong), ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]\n"
    "fn = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(other)\n"
    "tid = ctypes.c_ulong()\n"
    "def hot(n):\n"
    "    for k in range(n):\n"
    "        p = ps[k & 7]; p.i = -k; p.o = toks[k & 31]; xs[k & 31] = toks[k & 31]\n"
    "        if k == n // 4:\n"
    "            libc.pthread_create(ctypes.byref(tid), None, ctypes.cast(fn, ctypes.c_void_p), None)\n"
    "hot(600000)\n"
    "done.wait()\n"
    "ok = all(type(p.i) is int and p.o in toks for p in ps) and all(x in toks for x in xs)\n"
    "import sys\n"
    "sys.serial_sandbox_ok = ok\n";

__attribute__((constructor)) static void
check(void)
{
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    PyStatus status = Py_InitializeFromConfig(&config);
    CHECK(!PyStatus_Exception(status));
    CHECK(refuse_membarrier());
    CHECK(syscall(__NR_membarrier, 0, 0, 0) == -1 && errno == EPERM);
    CHECK(PyRun_SimpleString(program) == 0);
    PyObject *ok = PySys_GetObject("serial_sandbox_ok");
    CHECK(ok == Py_True);
    if (failures == 0) {
        puts("capi serial_sandbox ok");
    }
    fflush(stdout);
    _exit(failures != 0);
}
