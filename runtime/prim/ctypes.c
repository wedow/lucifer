/* The native half of _ctypes: the dynamic loader, raw memory, and foreign
   calls and callbacks through libffi. lib/modules/_ctypes.py owns the types
   and conversions and passes plain numbers here: an argument is an int (a
   pointer is its address) or a float, described by a one-character code
   (struct's codes, with 'P' for pointers, 'g' for long double and 'v' for
   void). A foreign call runs in the syscall bracket: the callee may block. */

#include "interp/exceptions.h"
#include "prim/prim.h"

#include <dlfcn.h>
#include <errno.h>
#include <ffi.h>
#include <string.h>

static ffi_type *
ffi_for(int code)
{
    switch (code) {
    case 'b': return &ffi_type_sint8;
    case 'B': return &ffi_type_uint8;
    case 'h': return &ffi_type_sint16;
    case 'H': return &ffi_type_uint16;
    case 'i': return &ffi_type_sint32;
    case 'I': return &ffi_type_uint32;
    case 'l': case 'q': return &ffi_type_sint64;
    case 'L': case 'Q': return &ffi_type_uint64;
    case 'f': return &ffi_type_float;
    case 'd': return &ffi_type_double;
    case 'g': return &ffi_type_longdouble;
    case 'P': return &ffi_type_pointer;
    case 'v': return &ffi_type_void;
    default: return NULL;
    }
}

/* A slot big enough for any ffi value. */
typedef union {
    int8_t s8; uint8_t u8; int16_t s16; uint16_t u16; int32_t s32; uint32_t u32;
    int64_t s64; uint64_t u64; float f; double d; long double ld; void *p;
    ffi_arg arg;
} value;

static int
to_value(int code, PyObject *o, value *v)
{
    memset(v, 0, sizeof *v);
    switch (code) {
    case 'f': case 'd': case 'g': {
        double d = PyFloat_AsDouble(o);
        if (d == -1.0 && PyErr_Occurred()) {
            return -1;
        }
        if (code == 'f') { v->f = (float)d; } else if (code == 'd') { v->d = d; } else { v->ld = d; }
        return 0;
    }
    case 'v':
        return 0;
    default: {
        unsigned long long n = PyLong_AsUnsignedLongLongMask(o);
        if (n == (unsigned long long)-1 && PyErr_Occurred()) {
            return -1;
        }
        switch (code) {
        case 'b': v->s8 = (int8_t)n; break;
        case 'B': v->u8 = (uint8_t)n; break;
        case 'h': v->s16 = (int16_t)n; break;
        case 'H': v->u16 = (uint16_t)n; break;
        case 'i': v->s32 = (int32_t)n; break;
        case 'I': v->u32 = (uint32_t)n; break;
        case 'P': v->p = (void *)(uintptr_t)n; break;
        default: v->u64 = n;
        }
        return 0;
    }
    }
}

static PyObject *
from_value(int code, const value *v)
{
    switch (code) {
    case 'b': return PyLong_FromLong(v->s8);
    case 'B': return PyLong_FromUnsignedLong(v->u8);
    case 'h': return PyLong_FromLong(v->s16);
    case 'H': return PyLong_FromUnsignedLong(v->u16);
    case 'i': return PyLong_FromLong(v->s32);
    case 'I': return PyLong_FromUnsignedLong(v->u32);
    case 'l': case 'q': return PyLong_FromLongLong(v->s64);
    case 'L': case 'Q': return PyLong_FromUnsignedLongLong(v->u64);
    case 'f': return PyFloat_FromDouble(v->f);
    case 'd': return PyFloat_FromDouble(v->d);
    case 'g': return PyFloat_FromDouble((double)v->ld);
    case 'P': return PyLong_FromUnsignedLongLong((uintptr_t)v->p);
    default: return Py_NewRef(Py_None);
    }
}

/* Integer returns narrower than a register arrive widened in an ffi_arg. */
static void
widen(int code, value *v)
{
    ffi_arg a = v->arg;
    switch (code) {
    case 'b': v->s8 = (int8_t)a; break;
    case 'B': v->u8 = (uint8_t)a; break;
    case 'h': v->s16 = (int16_t)a; break;
    case 'H': v->u16 = (uint16_t)a; break;
    case 'i': v->s32 = (int32_t)a; break;
    case 'I': v->u32 = (uint32_t)a; break;
    default: break;
    }
}

static PyObject *
need_args(Py_ssize_t nargs, Py_ssize_t n, const char *what)
{
    return PyErr_Format(PyExc_TypeError, "%s takes %zd arguments (%zd given)", what, n, nargs);
}

static __thread int saved_errno;

/* ct_errno(new): the saved errno of this thread (the one use_errno calls
   exchange with the C errno), after storing `new` if it is not None. */
static PyObject *
prim_ct_errno(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    int old = saved_errno;
    if (nargs == 1 && args[0] != Py_None) {
        long n = PyLong_AsLong(args[0]);
        if (n == -1 && PyErr_Occurred()) {
            return NULL;
        }
        saved_errno = (int)n;
    }
    return PyLong_FromLong(old);
}

/* The _flags_ of a function pointer type (FUNCFLAG_* of _ctypes.py). */
#define CT_FUNCFLAG_PYTHONAPI 4
#define CT_FUNCFLAG_USE_ERRNO 8

/* call(address, restype, argtypes, args, flags): the call's result */

static PyObject *
prim_ct_call(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 5) {
        return need_args(nargs, 5, "ct_call");
    }
    uintptr_t fn = (uintptr_t)PyLong_AsUnsignedLongLongMask(args[0]);
    int rcode = (int)PyLong_AsLong(args[1]);
    PyObject *codes = args[2], *values = args[3];
    Py_ssize_t n = PyTuple_GET_SIZE(codes);
    if (PyErr_Occurred() || PyTuple_GET_SIZE(values) != n || n > 64) {
        return PyErr_Occurred() ? NULL : PyErr_Format(PyExc_TypeError, "ct_call: argument count");
    }
    ffi_type *types[64];
    value store[64];
    void *pointers[64];
    for (Py_ssize_t i = 0; i < n; i++) {
        int code = (int)PyLong_AsLong(PyTuple_GET_ITEM(codes, i));
        types[i] = ffi_for(code);
        if (types[i] == NULL || code == 'v' || to_value(code, PyTuple_GET_ITEM(values, i), &store[i]) < 0) {
            return PyErr_Occurred() ? NULL : PyErr_Format(PyExc_TypeError, "ct_call: bad argument type");
        }
        pointers[i] = &store[i];
    }
    ffi_type *rtype = ffi_for(rcode);
    ffi_cif cif;
    if (rtype == NULL || ffi_prep_cif(&cif, FFI_DEFAULT_ABI, (unsigned)n, rtype, types) != FFI_OK) {
        return PyErr_Format(PyExc_TypeError, "ct_call: cannot prepare the call");
    }
    value result;
    memset(&result, 0, sizeof result);
    long flags = PyLong_AsLong(args[4]);
    int use_errno = (flags & CT_FUNCFLAG_USE_ERRNO) != 0;
    /* A PyDLL function calls the C API and keeps the thread attached. */
    PyThreadState *ts = (flags & CT_FUNCFLAG_PYTHONAPI) ? NULL : lu_syscall_begin();
    if (use_errno) {
        errno = saved_errno;
    }
    ffi_call(&cif, (void (*)(void))fn, &result, pointers);
    if (use_errno) {
        saved_errno = errno;
    }
    if (ts != NULL) {
        lu_syscall_end(ts);
    }
    /* A C API function that raised fails the call (_ctypes_callproc). */
    if ((flags & CT_FUNCFLAG_PYTHONAPI) && PyErr_Occurred()) {
        return NULL;
    }
    widen(rcode, &result);
    return from_value(rcode, &result);
}

/* A callback: libffi closure, calling a Python callable. */
struct callback {
    ffi_cif cif;
    ffi_closure *closure;
    void *code;
    ffi_type *types[64];
    int codes[64];
    int rcode, nargs;
    PyObject *callable;
};

static void
callback_entry(ffi_cif *cif, void *ret, void **cargs, void *user)
{
    struct callback *cb = user;
    PyGILState_STATE gil = PyGILState_Ensure();
    PyObject *argv[64];
    int built = 0;
    for (; built < cb->nargs; built++) {
        value v;
        memset(&v, 0, sizeof v);
        memcpy(&v, cargs[built], cb->types[built]->size);
        argv[built] = from_value(cb->codes[built], &v);
        if (argv[built] == NULL) {
            goto failed;
        }
    }
    PyObject *r = lu_call(cb->callable, argv, (size_t)cb->nargs, NULL);
    if (r != NULL && cb->rcode != 'v') {
        if (cb->rcode == 'f' || cb->rcode == 'd' || cb->rcode == 'g') {
            value out;
            if (to_value(cb->rcode, r, &out) < 0) {
                Py_CLEAR(r);
            }
            else {
                memcpy(ret, &out, cif->rtype->size);
            }
        }
        else {
            unsigned long long n = PyLong_AsUnsignedLongLongMask(r);
            if (n == (unsigned long long)-1 && PyErr_Occurred()) {
                Py_CLEAR(r);
            }
            else {
                ffi_arg a = (ffi_arg)n;     /* libffi wants a narrow integer return widened */
                memcpy(ret, &a, sizeof a);
            }
        }
    }
    if (r == NULL) {
        lu_err_unraisable("calling a ctypes callback", cb->callable);
    }
    Py_XDECREF(r);
failed:
    for (int i = 0; i < built; i++) {
        Py_DECREF(argv[i]);
    }
    PyGILState_Release(gil);
}

/* callback(callable, restype, argtypes): the address of a C function that calls callable */
static PyObject *
prim_ct_callback(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 3) {
        return need_args(nargs, 3, "ct_callback");
    }
    struct callback *cb = calloc(1, sizeof *cb);
    if (cb == NULL) {
        return PyErr_NoMemory();
    }
    PyObject *codes = args[2];
    cb->nargs = (int)PyTuple_GET_SIZE(codes);
    cb->rcode = (int)PyLong_AsLong(args[1]);
    if (cb->nargs > 64) {
        free(cb);
        return PyErr_Format(PyExc_ValueError, "too many arguments for a callback");
    }
    for (int i = 0; i < cb->nargs; i++) {
        cb->codes[i] = (int)PyLong_AsLong(PyTuple_GET_ITEM(codes, i));
        cb->types[i] = ffi_for(cb->codes[i]);
    }
    ffi_type *rtype = ffi_for(cb->rcode);
    cb->closure = ffi_closure_alloc(sizeof(ffi_closure), &cb->code);
    if (PyErr_Occurred() || rtype == NULL || cb->closure == NULL
        || ffi_prep_cif(&cb->cif, FFI_DEFAULT_ABI, (unsigned)cb->nargs, rtype, cb->types) != FFI_OK
        || ffi_prep_closure_loc(cb->closure, &cb->cif, callback_entry, cb, cb->code) != FFI_OK) {
        free(cb);
        return PyErr_Occurred() ? NULL : PyErr_Format(PyExc_SystemError, "cannot create a callback");
    }
    cb->callable = Py_NewRef(args[0]);      /* the closure lives as long as the process */
    return PyLong_FromUnsignedLongLong((uintptr_t)cb->code);
}

/* dlopen(name or None, mode): a handle */
static PyObject *
prim_ct_dlopen(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long mode = nargs == 2 ? PyLong_AsLong(args[1]) : -1;
    const char *name = nargs == 2 && args[0] != Py_None ? PyUnicode_AsUTF8(args[0]) : NULL;
    if (PyErr_Occurred() || (nargs == 2 && args[0] != Py_None && name == NULL)) {
        return PyErr_Occurred() ? NULL : need_args(nargs, 2, "ct_dlopen");
    }
    void *handle = dlopen(name, (int)mode ? (int)mode : RTLD_NOW);
    if (handle == NULL) {
        const char *err = dlerror();
        return PyErr_Format(PyExc_OSError, "%s", err != NULL ? err : "dlopen failed");
    }
    return PyLong_FromUnsignedLongLong((uintptr_t)handle);
}

static PyObject *
prim_ct_dlclose(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    void *handle = (void *)(uintptr_t)PyLong_AsUnsignedLongLongMask(args[0]);
    return PyLong_FromLong(dlclose(handle));
}

/* dlsym(handle, name): an address, or dlerror's text */
static PyObject *
prim_ct_dlsym(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    void *handle = (void *)(uintptr_t)PyLong_AsUnsignedLongLongMask(args[0]);
    const char *name = PyUnicode_AsUTF8(args[1]);
    if (name == NULL) {
        return NULL;
    }
    dlerror();
    void *p = dlsym(handle, name);
    const char *err = p == NULL ? dlerror() : NULL;
    if (err != NULL) {
        return PyUnicode_FromString(err);
    }
    return PyLong_FromUnsignedLongLong((uintptr_t)p);
}

/* read(address, size): bytes */
static PyObject *
prim_ct_read(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    uintptr_t p = (uintptr_t)PyLong_AsUnsignedLongLongMask(args[0]);
    Py_ssize_t n = PyLong_AsSsize_t(args[1]);
    if (PyErr_Occurred()) {
        return NULL;
    }
    return PyBytes_FromStringAndSize((const char *)p, n);
}

/* write(address, data) */
static PyObject *
prim_ct_write(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    uintptr_t p = (uintptr_t)PyLong_AsUnsignedLongLongMask(args[0]);
    Py_buffer view;
    if (PyErr_Occurred() || PyObject_GetBuffer(args[1], &view, PyBUF_SIMPLE) < 0) {
        return NULL;
    }
    memmove((void *)p, view.buf, (size_t)view.len);
    PyBuffer_Release(&view);
    return Py_NewRef(Py_None);
}

/* strlen(address, wide): the length of the zero-terminated string there */
static PyObject *
prim_ct_strlen(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    uintptr_t p = (uintptr_t)PyLong_AsUnsignedLongLongMask(args[0]);
    int wide = PyObject_IsTrue(args[1]);
    Py_ssize_t n = 0;
    if (wide) {
        while (((const int32_t *)p)[n] != 0) {
            n++;
        }
    }
    else {
        n = (Py_ssize_t)strlen((const char *)p);
    }
    return PyLong_FromSsize_t(n);
}

/* address(obj): where the buffer of a bytes-like object starts; the object must be kept alive */
static PyObject *
prim_ct_address(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    Py_buffer view;
    if (PyObject_GetBuffer(args[0], &view, PyBUF_SIMPLE) < 0) {
        return NULL;
    }
    PyObject *r = PyLong_FromUnsignedLongLong((uintptr_t)view.buf);
    PyBuffer_Release(&view);
    return r;
}

/* view(address, size, readonly): a memoryview over raw memory */
static PyObject *
prim_ct_view(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    uintptr_t p = (uintptr_t)PyLong_AsUnsignedLongLongMask(args[0]);
    Py_ssize_t n = PyLong_AsSsize_t(args[1]);
    if (PyErr_Occurred()) {
        return NULL;
    }
    return PyMemoryView_FromMemory((char *)p, n, PyObject_IsTrue(args[2]) ? PyBUF_READ : PyBUF_WRITE);
}

/* object_address(obj) and object_at(address): a PyObject * as an int, and back */
static PyObject *
prim_ct_object_address(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    return PyLong_FromUnsignedLongLong((uintptr_t)args[0]);
}

static PyObject *
prim_ct_object_at(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *o = (PyObject *)(uintptr_t)PyLong_AsUnsignedLongLongMask(args[0]);
    return o == NULL ? Py_NewRef(Py_None) : Py_NewRef(o);
}

/* longdouble(data) -> float and longdouble_pack(float) -> bytes: the 16-byte x87 value */
static PyObject *
prim_ct_longdouble(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    Py_buffer view;
    if (PyObject_GetBuffer(args[0], &view, PyBUF_SIMPLE) < 0) {
        return NULL;
    }
    long double ld = 0;
    memcpy(&ld, view.buf, view.len < (Py_ssize_t)sizeof ld ? (size_t)view.len : sizeof ld);
    PyBuffer_Release(&view);
    return PyFloat_FromDouble((double)ld);
}

static PyObject *
prim_ct_longdouble_pack(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    double d = PyFloat_AsDouble(args[0]);
    if (d == -1.0 && PyErr_Occurred()) {
        return NULL;
    }
    long double ld = d;
    char bytes[sizeof ld];
    memset(bytes, 0, sizeof bytes);
    memcpy(bytes, &ld, 10);
    return PyBytes_FromStringAndSize(bytes, (Py_ssize_t)sizeof bytes);
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_ctypes_methods[] = {
    FAST(ct_call), FAST(ct_callback), FAST(ct_errno), FAST(ct_dlopen), FAST(ct_dlclose), FAST(ct_dlsym),
    FAST(ct_read), FAST(ct_write), FAST(ct_strlen), FAST(ct_address), FAST(ct_view),
    FAST(ct_object_address), FAST(ct_object_at), FAST(ct_longdouble), FAST(ct_longdouble_pack),
    { NULL, NULL, 0, NULL },
};
