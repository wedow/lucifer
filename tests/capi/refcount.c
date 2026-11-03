/* A C extension built only against include/. Preloaded into bin/lucifer with
   LD_BIND_NOW=1, every symbol it imports must resolve against the executable
   before its constructor runs. The constructor then checks the static
   objects through the headers' inline accessors and the biased reference
   counts across threads, prints "capi refcount ok" and exits. */

#include "Python.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "refcount.c:%d: check failed: %s\n", __LINE__, #cond); \
    failures++; } } while (0)

static int deallocs;

static void
counted_dealloc(PyObject *op)
{
    __atomic_add_fetch(&deallocs, 1, __ATOMIC_SEQ_CST);
    PyObject_Free(op);
}

static PyTypeObject Counted_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "counted",
    .tp_basicsize = sizeof(PyObject),
    .tp_dealloc = counted_dealloc,
};

static PyObject *
counted_new(void)
{
    return PyObject_Init(PyObject_Malloc(sizeof(PyObject)), &Counted_Type);
}

static int
dealloc_count(void)
{
    return __atomic_load_n(&deallocs, __ATOMIC_SEQ_CST);
}

static void
check_static_objects(void)
{
    CHECK(Py_TYPE(&PyFloat_Type) == &PyType_Type);
    CHECK(strcmp(PyFloat_Type.tp_name, "float") == 0);
    CHECK(Py_TYPE(Py_True) == &PyBool_Type);
    CHECK(_PyLong_IsCompact((PyLongObject *)Py_True));
    CHECK(_PyLong_CompactValue((PyLongObject *)Py_True) == 1);
    CHECK(_PyLong_CompactValue((PyLongObject *)Py_False) == 0);
    CHECK(Py_TYPE(PyExc_TypeError) == &PyType_Type);
    CHECK(strcmp(((PyTypeObject *)PyExc_TypeError)->tp_name, "TypeError") == 0);
    CHECK(PyExc_IOError == PyExc_OSError);
    CHECK(_Py_IsImmortal(Py_None) && _Py_IsStaticImmortal(Py_None));
    Py_INCREF(Py_None);
    Py_DECREF(Py_None);
    CHECK(Py_REFCNT(Py_None) == _Py_IMMORTAL_INITIAL_REFCNT);
    CHECK(Py_Version == PY_VERSION_HEX);
}

static void
check_allocators(void)
{
    char *p = PyMem_Calloc(4, 8);
    CHECK(p != NULL && p[0] == 0 && p[31] == 0);
    memcpy(p, "lucifer", 8);
    p = PyMem_Realloc(p, 4096);
    CHECK(p != NULL && strcmp(p, "lucifer") == 0);
    PyMem_Free(p);
    CHECK((p = PyMem_Malloc(0)) != NULL);
    PyMem_Free(p);
    CHECK((p = PyObject_Calloc(1, 16)) != NULL && p[15] == 0);
    PyObject_Free(PyObject_Realloc(p, 64));
    CHECK((p = PyMem_RawMalloc(0)) != NULL);
    PyMem_RawFree(PyMem_RawRealloc(p, 32));
}

static void
check_local_refcount(void)
{
    int before = dealloc_count();
    PyObject *o = counted_new();
    CHECK(Py_REFCNT(o) == 1 && _Py_IsOwnedByCurrentThread(o));
    Py_INCREF(o);
    Py_IncRef(o);
    _Py_IncRef(o);
    CHECK(Py_REFCNT(o) == 4);
    _Py_DecRef(o);
    Py_DecRef(o);
    Py_DECREF(o);
    CHECK(dealloc_count() == before);
    Py_DECREF(o);
    CHECK(dealloc_count() == before + 1);
    CHECK(PyUnstable_IsImmortal(Py_None));
}

/* Every thread attaches through PyGILState_Ensure, which registers it as a
   biased-refcount owner before its first reference count operation. */

/* One reference owned by another thread, which outlives our decref. */
static pthread_barrier_t barrier;
static PyObject *shared_obj;

static void *
owner_waits(void *arg)
{
    PyGILState_STATE gil = PyGILState_Ensure();
    shared_obj = counted_new();
    Py_INCREF(shared_obj);              /* the reference handed over */
    pthread_barrier_wait(&barrier);     /* main drops the handed-over one */
    Py_DECREF(shared_obj);              /* local count 1, the queue holds one */
    pthread_barrier_wait(&barrier);
    pthread_barrier_wait(&barrier);     /* main checks before the merge */
    PyGILState_Release(gil);            /* unbinding merges the queue */
    return NULL;
}

static void *
owner_exits(void *arg)
{
    PyGILState_STATE gil = PyGILState_Ensure();
    shared_obj = counted_new();
    PyGILState_Release(gil);
    return NULL;
}

static void *
other_incref(void *arg)
{
    PyGILState_STATE gil = PyGILState_Ensure();
    Py_INCREF(shared_obj);
    PyGILState_Release(gil);
    return NULL;
}

static void *
other_decref(void *arg)
{
    PyGILState_STATE gil = PyGILState_Ensure();
    Py_DECREF(shared_obj);
    PyGILState_Release(gil);
    return NULL;
}

static void
run(void *(*fn)(void *))
{
    pthread_t t;
    pthread_create(&t, NULL, fn, NULL);
    pthread_join(t, NULL);
}

static void
check_shared_refcount(void)
{
    /* Decref by a non-owner while the owner lives: queued to the owner, and
       merged (then freed) when the owner exits. */
    int before = dealloc_count();
    pthread_t t;
    pthread_barrier_init(&barrier, NULL, 2);
    pthread_create(&t, NULL, owner_waits, NULL);
    pthread_barrier_wait(&barrier);
    Py_DECREF(shared_obj);
    pthread_barrier_wait(&barrier);
    CHECK(dealloc_count() == before);
    pthread_barrier_wait(&barrier);
    pthread_join(t, NULL);
    CHECK(dealloc_count() == before + 1);

    /* Decref after the owner exited: merged and freed at once. */
    run(owner_exits);
    CHECK(dealloc_count() == before + 1);
    Py_DECREF(shared_obj);
    CHECK(dealloc_count() == before + 2);

    /* The owner's local count reaches zero while another thread holds a
       shared reference: merged, then freed by the last shared decref. */
    shared_obj = counted_new();
    run(other_incref);
    Py_DECREF(shared_obj);
    CHECK(dealloc_count() == before + 2);
    run(other_decref);
    CHECK(dealloc_count() == before + 3);
}

/* A joined thread's TCB, and so its thread id, is reused by the next thread.
   The new thread sees the dead owner's objects as its own and counts them
   locally, while another thread merges them as abandoned. */
enum { REUSE_ROUNDS = 200, REUSE_OBJECTS = 64 };
static PyObject *reuse_objs[REUSE_OBJECTS];
static uintptr_t reuse_tid;
static int reuse_cur, reuse_done, reuse_seen;

static void *
owner_makes_two_refs(void *arg)
{
    PyGILState_STATE gil = PyGILState_Ensure();
    reuse_tid = _Py_ThreadId();
    for (int i = 0; i < REUSE_OBJECTS; i++) {
        reuse_objs[i] = counted_new();
        Py_INCREF(reuse_objs[i]);
    }
    PyGILState_Release(gil);
    return NULL;
}

/* Holds the second reference of each object; creates no objects. */
static void *
heir_counts(void *arg)
{
    PyGILState_STATE gil = PyGILState_Ensure();
    __atomic_store_n(&reuse_seen, _Py_ThreadId() == reuse_tid, __ATOMIC_SEQ_CST);
    pthread_barrier_wait(&barrier);
    while (!__atomic_load_n(&reuse_done, __ATOMIC_SEQ_CST)) {
        PyObject *o = reuse_objs[__atomic_load_n(&reuse_cur, __ATOMIC_SEQ_CST)];
        Py_INCREF(o);
        Py_DECREF(o);
    }
    pthread_barrier_wait(&barrier);     /* main checks nothing was freed */
    for (int i = 0; i < REUSE_OBJECTS; i++) {
        Py_DECREF(reuse_objs[i]);
    }
    PyGILState_Release(gil);
    return NULL;
}

static void
check_reused_thread_id(void)
{
    int reused = 0;
    pthread_barrier_init(&barrier, NULL, 2);
    for (int round = 0; round < REUSE_ROUNDS; round++) {
        int before = dealloc_count();
        run(owner_makes_two_refs);
        reuse_cur = reuse_done = 0;
        pthread_t t;
        pthread_create(&t, NULL, heir_counts, NULL);
        pthread_barrier_wait(&barrier);
        reused += __atomic_load_n(&reuse_seen, __ATOMIC_SEQ_CST);
        for (int i = 0; i < REUSE_OBJECTS; i++) {
            __atomic_store_n(&reuse_cur, i, __ATOMIC_SEQ_CST);
            for (volatile int spin = 0; spin < 200; spin++) {
            }
            Py_DECREF(reuse_objs[i]);   /* the owner exited: merged here */
        }
        __atomic_store_n(&reuse_done, 1, __ATOMIC_SEQ_CST);
        int early = dealloc_count() - before;
        pthread_barrier_wait(&barrier);
        pthread_join(t, NULL);
        if (early != 0 || dealloc_count() != before + REUSE_OBJECTS) {
            fprintf(stderr, "round %d: %d freed early, %d of %d freed\n", round,
                    early, dealloc_count() - before, REUSE_OBJECTS);
            failures++;
            return;
        }
    }
    CHECK(reused > 0);
}

/* Varargs entry points: binding them is part of the check. */
static void *const varargs[] = {
    (void *)PyArg_ParseTuple, (void *)PyArg_ParseTupleAndKeywords,
    (void *)Py_BuildValue, (void *)PyErr_Format, (void *)PyUnicode_FromFormat,
    (void *)PyObject_CallFunctionObjArgs, (void *)PyTuple_Pack,
};

static struct PyModuleDef refcount_module = {
    PyModuleDef_HEAD_INIT, .m_name = "refcount",
};

PyMODINIT_FUNC
PyInit_refcount(void)
{
    return PyModuleDef_Init(&refcount_module);
}

__attribute__((constructor)) static void
check(void)
{
    for (size_t i = 0; i < sizeof varargs / sizeof varargs[0]; i++) {
        CHECK(varargs[i] != NULL);
    }
    PyGILState_STATE gil = PyGILState_Ensure();
    check_static_objects();
    check_allocators();
    check_local_refcount();
    check_shared_refcount();
    check_reused_thread_id();
    PyGILState_Release(gil);
    if (failures == 0) {
        puts("capi refcount ok");
    }
    fflush(stdout);
    _exit(failures != 0);
}
