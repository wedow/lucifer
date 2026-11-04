/* Signals (Modules/signalmodule.c, the delivery half). The C handler only
   records that a signal arrived and raises the signals reason in the main
   thread's carrier; the Python handler runs in the main task, at its next
   poll (DESIGN 5.6). Which Python handler a signal has, and its sigaction,
   are set by _signal through the primitives here. */

#define _GNU_SOURCE
#include "prim/prim.h"

#include <errno.h>
#include <signal.h>
#include <string.h>

/* _Py_ThreadCanHandleSignals: the main thread of the main interpreter. A
   task's ident is the thread it first ran on, so this is the main task
   wherever it runs. */
int
lu_thread_can_handle_signals(void)
{
    struct lu_task *t = lu_current_task();
    return t != NULL ? t->ts.thread_id == (unsigned long)_PyRuntime.main_thread
                     : pthread_equal(pthread_self(), _PyRuntime.main_thread);
}

/* _PyEval_SignalReceived: the reason is aimed at the main task, which may be
   on any carrier, and that carrier is woken in case the task sleeps in a
   park. Async-signal-safe: atomic stores and a futex wake only. */
static void
signal_received(void)
{
    struct lu_task *main = __atomic_load_n(&_PyRuntime.main_task, __ATOMIC_ACQUIRE);
    if (main == NULL) {
        return;
    }
    lu_task_signal(main, LU_POLL_SIGNALS_PENDING);
    struct lu_carrier *c = __atomic_load_n(&main->carrier, __ATOMIC_ACQUIRE);
    if (c != NULL) {
        lu_carrier_wake(c);
    }
}

/* Under -X lucifer_handoff the main task runs on a carrier that need not be the thread the kernel
   picked (it prefers the main thread) to receive a signal sent to the process. A blocking call in the
   task's thread is interrupted only if the thread itself receives the signal, so the signal goes there
   too; that thread's handler finds itself the target and stops. */
static void
redirect_to_main_task(int signum)
{
    if (!__atomic_load_n(&_PyRuntime.sched.active, __ATOMIC_ACQUIRE)) {
        return;
    }
    struct lu_task *main = __atomic_load_n(&_PyRuntime.main_task, __ATOMIC_ACQUIRE);
    struct lu_carrier *c = main != NULL ? __atomic_load_n(&main->carrier, __ATOMIC_ACQUIRE) : NULL;
    if (c != NULL && !pthread_equal(c->thread, pthread_self())) {
        pthread_kill(c->thread, signum);
    }
}

static void
trip(int signum)
{
    __atomic_store_n(&_PyRuntime.signals.handlers[signum].tripped, 1, __ATOMIC_SEQ_CST);
    __atomic_store_n(&_PyRuntime.signals.tripped, 1, __ATOMIC_SEQ_CST);
    signal_received();
}

/* The sigaction handler. PyErr_SetInterruptEx trips without it: no signal
   was sent, so there is none to redirect (it would trip a second time). */
static void
trip_signal(int signum)
{
    int save_errno = errno;
    trip(signum);
    redirect_to_main_task(signum);
    errno = save_errno;
}

/* _PyErr_CheckSignalsTstate: runs the Python handler of each tripped signal,
   in signal order. A handler that raises trips the reason again, so that the
   signals after it still run at the next poll. */
static int
check_signals(PyThreadState *ts)
{
    if (!__atomic_load_n(&_PyRuntime.signals.tripped, __ATOMIC_SEQ_CST)) {
        return 0;
    }
    __atomic_store_n(&_PyRuntime.signals.tripped, 0, __ATOMIC_SEQ_CST);
    for (int i = 1; i < LU_NSIG; i++) {
        if (!__atomic_exchange_n(&_PyRuntime.signals.handlers[i].tripped, 0, __ATOMIC_SEQ_CST)) {
            continue;
        }
        PyObject *func = _PyRuntime.signals.handlers[i].func;
        if (func == NULL) {
            continue;
        }
        PyObject *frame = (PyObject *)PyThreadState_GetFrame(ts);
        PyObject *args[2] = { PyLong_FromLong(i), frame != NULL ? frame : Py_None };
        PyObject *result = args[0] != NULL ? lu_call(func, args, 2, NULL) : NULL;
        Py_XDECREF(args[0]);
        Py_XDECREF(frame);
        if (result == NULL) {
            __atomic_store_n(&_PyRuntime.signals.tripped, 1, __ATOMIC_SEQ_CST);
            signal_received();
            return -1;
        }
        Py_DECREF(result);
    }
    return 0;
}

/* handle_signals: the reason is cleared first; a thread that cannot run
   handlers leaves the signals tripped for the main task. */
int
lu_signals_handle(PyThreadState *ts)
{
    lu_task_unsignal((struct lu_task *)ts, LU_POLL_SIGNALS_PENDING);
    if (!lu_thread_can_handle_signals()) {
        return 0;
    }
    if (check_signals(ts) < 0) {
        lu_task_signal((struct lu_task *)ts, LU_POLL_SIGNALS_PENDING);
        return -1;
    }
    return 0;
}

int
PyErr_CheckSignals(void)
{
    PyThreadState *ts = PyThreadState_Get();
    return lu_thread_can_handle_signals() ? check_signals(ts) : 0;
}

int
PyErr_SetInterruptEx(int signum)
{
    if (signum < 1 || signum >= LU_NSIG) {
        return -1;
    }
    if (_PyRuntime.signals.handlers[signum].func != NULL) {
        trip(signum);
    }
    return 0;
}

void
PyErr_SetInterrupt(void)
{
    (void)PyErr_SetInterruptEx(SIGINT);
}

int
PyOS_InterruptOccurred(void)
{
    if (!lu_thread_can_handle_signals()) {
        return 0;
    }
    return __atomic_exchange_n(&_PyRuntime.signals.handlers[SIGINT].tripped, 0, __ATOMIC_SEQ_CST);
}

#define ARGS(n) do { if (nargs != (n)) { \
        lu_err_format(PyExc_TypeError, "%s() takes %d arguments", __func__ + 5, (n)); \
        return NULL; } } while (0)

/* signal_install(signum, handler): handler is SIG_DFL (0), SIG_IGN (1) or a
   callable that runs in the main task as handler(signum, frame). Only the main
   thread installs. A system call interrupted by the signal fails with EINTR
   (no SA_RESTART), as CPython's handlers do. */
static PyObject *
prim_signal_install(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    long signum, action = -1;
    if (!PyLong_CheckExact(args[0]) || lu_long_as_long(args[0], &signum) < 0) {
        return NULL;
    }
    if (signum < 1 || signum >= LU_NSIG) {
        lu_err_format(PyExc_ValueError, "signal number out of range");
        return NULL;
    }
    if (!lu_thread_can_handle_signals()) {
        lu_err_format(PyExc_ValueError, "signal only works in main thread of the main interpreter");
        return NULL;
    }
    if (PyLong_CheckExact(args[1])) {
        if (lu_long_as_long(args[1], &action) < 0 || (action != 0 && action != 1)) {
            return NULL;
        }
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = action < 0 ? trip_signal : action == 1 ? SIG_IGN : SIG_DFL;
    sa.sa_flags = SA_ONSTACK;
    if (sigaction((int)signum, &sa, NULL) < 0) {
        lu_err_format(PyExc_OSError, "[Errno %d] %s", errno, strerror(errno));
        return NULL;
    }
    PyObject *func = action < 0 ? Py_NewRef(args[1]) : NULL;
    PyObject *old = _PyRuntime.signals.handlers[signum].func;
    _PyRuntime.signals.handlers[signum].func = func;
    Py_XDECREF(old);
    return Py_NewRef(Py_None);
}

/* signal_raise(signum): raise(signum) in this thread, then the handlers it
   tripped run at once if this is the main thread (signal.raise_signal). */
static PyObject *
prim_signal_raise(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    long signum;
    if (!PyLong_Check(args[0]) || lu_long_as_long(args[0], &signum) < 0) {
        return NULL;
    }
    if (raise((int)signum) < 0) {
        lu_err_format(PyExc_OSError, "[Errno %d] %s", errno, strerror(errno));
        return NULL;
    }
    return PyErr_CheckSignals() < 0 ? NULL : Py_NewRef(Py_None);
}

/* signal_interrupt(signum): _thread.interrupt_main, simulating the signal's
   arrival. */
static PyObject *
prim_signal_interrupt(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    long signum;
    if (!PyLong_Check(args[0]) || lu_long_as_long(args[0], &signum) < 0) {
        return NULL;
    }
    if (PyErr_SetInterruptEx((int)signum) < 0) {
        lu_err_format(PyExc_ValueError, "signal number out of range");
        return NULL;
    }
    return Py_NewRef(Py_None);
}

/* signal_ignored(signum): whether the process ignores the signal (SIG_IGN), as it does for
   SIGINT when started in the background. */
static PyObject *
prim_signal_ignored(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    long signum;
    struct sigaction sa;
    if (!PyLong_Check(args[0]) || lu_long_as_long(args[0], &signum) < 0
        || sigaction((int)signum, NULL, &sa) < 0) {
        lu_err_format(PyExc_ValueError, "signal number out of range");
        return NULL;
    }
    return Py_NewRef(sa.sa_handler == SIG_IGN ? Py_True : Py_False);
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_signal_methods[] = {
    FAST(signal_install), FAST(signal_raise), FAST(signal_interrupt), FAST(signal_ignored),
    { NULL, NULL, 0, NULL },
};
