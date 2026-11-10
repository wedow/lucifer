/* Module objects (Objects/moduleobject.c, layout half) and the _lucifer
   builtin module, the primitive boundary kernel Python calls (DESIGN 4.4).
   _lucifer uses multi-phase init with module state and keeps no file-scope
   mutable data (FOUNDATIONS 2.1). */

#include "interp/exceptions.h"
#include "prim/bytes.h"
#include <math.h>

/* bpo-39824: m_traverse and m_clear need the state a positive m_size promises. */
static int
module_has_state(PyModuleObject *m)
{
    return m->md_def != NULL && (m->md_def->m_size <= 0 || m->md_state != NULL);
}

static int
module_traverse(PyObject *op, visitproc visit, void *arg)
{
    PyModuleObject *m = (PyModuleObject *)op;
    if (module_has_state(m) && m->md_def->m_traverse != NULL) {
        int res = m->md_def->m_traverse(op, visit, arg);
        if (res) {
            return res;
        }
    }
    Py_VISIT(m->md_dict);
    return 0;
}

static int
module_clear(PyObject *op)
{
    PyModuleObject *m = (PyModuleObject *)op;
    if (module_has_state(m) && m->md_def->m_clear != NULL) {
        int res = m->md_def->m_clear(op);
        if (PyErr_Occurred()) {
            lu_err_unraisable_text("Exception ignored in m_clear of module", op);
        }
        if (res) {
            return res;
        }
    }
    Py_CLEAR(m->md_dict);
    return 0;
}

static void
module_dealloc(PyObject *op)
{
    PyModuleObject *m = (PyModuleObject *)op;
    lu_clear_weakrefs(op);
    if (m->md_def != NULL && m->md_def->m_free != NULL) {
        m->md_def->m_free(op);
    }
    PyMem_Free(m->md_state);
    Py_XDECREF(m->md_dict);
    Py_XDECREF(m->md_name);
    lu_object_free(op);
}

static PyMemberDef module_members[] = {
    { "__dict__", _Py_T_OBJECT, offsetof(PyModuleObject, md_dict), Py_READONLY, NULL },
    { NULL },
};

void
lu_module_init(void)
{
    PyModule_Type.tp_basicsize = sizeof(PyModuleObject);
    PyModule_Type.tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    PyModule_Type.tp_dealloc = module_dealloc;
    PyModule_Type.tp_traverse = module_traverse;
    PyModule_Type.tp_clear = module_clear;
    PyModule_Type.tp_dictoffset = offsetof(PyModuleObject, md_dict);
    PyModule_Type.tp_weaklistoffset = offsetof(PyModuleObject, md_weaklist);
    PyModule_Type.tp_members = module_members;
}

/* module_init_dict: name the module and give its dict __name__, __doc__,
   __package__, __loader__ and __spec__. */
static int
module_init_dict(PyModuleObject *m, PyObject *name, PyObject *doc)
{
    Py_XSETREF(m->md_name, Py_NewRef(name));
    if (m->md_dict == NULL && (m->md_dict = PyDict_New()) == NULL) {
        return -1;
    }
    return lu_ns_insert(m->md_dict, LU_ID(__name__), name) < 0
        || lu_ns_insert(m->md_dict, LU_ID(__doc__), doc) < 0
        || lu_ns_insert(m->md_dict, LU_ID(__package__), Py_None) < 0
        || lu_ns_insert(m->md_dict, LU_ID(__loader__), Py_None) < 0
        || lu_ns_insert(m->md_dict, LU_ID(__spec__), Py_None) < 0 ? -1 : 0;
}

/* PyModule_NewObject */
PyObject *
PyModule_NewObject(PyObject *name)
{
    PyModuleObject *m = (PyModuleObject *)lu_object_new(&PyModule_Type, sizeof(PyModuleObject));
    if (m != NULL && module_init_dict(m, name, Py_None) < 0) {
        Py_CLEAR(m);
    }
    return (PyObject *)m;
}

PyObject *
PyModule_New(const char *name)
{
    PyObject *s = PyUnicode_FromString(name);
    if (s == NULL) {
        return NULL;
    }
    PyObject *m = PyModule_NewObject(s);
    Py_DECREF(s);
    return m;
}

PyObject *
lu_module_new(const char *name)
{
    PyObject *s = PyUnicode_InternFromString(name);
    if (s == NULL) {
        return NULL;
    }
    PyObject *m = PyModule_NewObject(s);
    Py_DECREF(s);
    return m;
}

PyObject *
PyModule_GetDict(PyObject *m)
{
    return ((PyModuleObject *)m)->md_dict;
}

void *
PyModule_GetState(PyObject *m)
{
    return ((PyModuleObject *)m)->md_state;
}

/* PyModule_AddFunctions on a module made natively, by namespace stores:
   the kernel's attribute code may not exist yet. */
int
lu_module_add_functions(PyObject *m, PyMethodDef *defs)
{
    PyModuleObject *mo = (PyModuleObject *)m;
    for (PyMethodDef *ml = defs; ml->ml_name != NULL; ml++) {
        if (ml->ml_flags & (METH_CLASS | METH_STATIC)) {
            lu_err_format(PyExc_ValueError, "module functions cannot set METH_CLASS or METH_STATIC");
            return -1;
        }
        PyObject *f = PyCFunction_NewEx(ml, m, mo->md_name);
        PyObject *name = f != NULL ? PyUnicode_InternFromString(ml->ml_name) : NULL;
        int err = name == NULL || lu_ns_insert(mo->md_dict, name, f) < 0;
        Py_XDECREF(name);
        Py_XDECREF(f);
        if (err) {
            return -1;
        }
    }
    return 0;
}

#define add_functions lu_module_add_functions

/* A builtin module from a multi-phase definition, made natively before
   importlib exists: no spec, so no Py_mod_create. */
PyObject *
lu_module_from_def(PyModuleDef *def)
{
    PyModuleDef_Init(def);
    PyObject *m = lu_module_new(def->m_name);
    if (m == NULL) {
        return NULL;
    }
    ((PyModuleObject *)m)->md_def = def;
    for (PyModuleDef_Slot *slot = def->m_slots; slot != NULL && slot->slot != 0; slot++) {
        if (slot->slot == Py_mod_create) {
            lu_err_format(PyExc_SystemError, "module %s: Py_mod_create needs a spec", def->m_name);
            Py_DECREF(m);
            return NULL;
        }
    }
    if ((def->m_methods != NULL && add_functions(m, def->m_methods) < 0)
        || PyModule_ExecDef(m, def) < 0) {
        Py_DECREF(m);
        return NULL;
    }
    return m;
}

/* _lucifer */

/* socket_base: the instance layout of _socket.socket, PySocketSockObject
   (Modules/socketmodule.h), which extensions such as _ssl read through
   _socket.CAPI. _socket.socket, Python, subclasses it and keeps its state in
   these fields; the timeout is seconds (None for blocking) to Python and
   nanoseconds (-1) in the field. */
typedef struct {
    PyObject_HEAD
    int sock_fd, sock_family, sock_type, sock_proto;
    PyObject *(*errorhandler)(void);
    PyTime_t sock_timeout;
} lu_socket_base;

static PyObject *
socket_base_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    lu_socket_base *s = (lu_socket_base *)type->tp_alloc(type, 0);
    if (s != NULL) {
        s->sock_fd = -1;
        s->sock_timeout = -1;
    }
    return (PyObject *)s;
}

static PyObject *
socket_base_get_timeout(PyObject *self, void *closure)
{
    PyTime_t t = ((lu_socket_base *)self)->sock_timeout;
    return t < 0 ? Py_NewRef(Py_None) : PyFloat_FromDouble(PyTime_AsSecondsDouble(t));
}

/* _PyTime_FromSecondsObject with _PyTime_ROUND_TIMEOUT (away from zero). */
static int
socket_base_set_timeout(PyObject *self, PyObject *value, void *closure)
{
    PyTime_t t = -1;
    if (value == NULL) {
        PyErr_SetString(PyExc_AttributeError, "cannot delete the timeout");
        return -1;
    }
    if (value != Py_None) {
        double d = PyFloat_AsDouble(value);
        if (d == -1.0 && PyErr_Occurred()) {
            return -1;
        }
        d = d * 1e9;
        d = d >= 0 ? ceil(d) : floor(d);
        if (!((double)PyTime_MIN <= d && d < -(double)PyTime_MIN)) {
            PyErr_SetString(PyExc_OverflowError, "timestamp too large to convert to C PyTime_t");
            return -1;
        }
        t = (PyTime_t)d;
    }
    ((lu_socket_base *)self)->sock_timeout = t;
    return 0;
}

static PyMemberDef socket_base_members[] = {
    { "_fd", Py_T_INT, offsetof(lu_socket_base, sock_fd), 0, NULL },
    { "_family", Py_T_INT, offsetof(lu_socket_base, sock_family), 0, NULL },
    { "_type", Py_T_INT, offsetof(lu_socket_base, sock_type), 0, NULL },
    { "_proto", Py_T_INT, offsetof(lu_socket_base, sock_proto), 0, NULL },
    { NULL },
};

static PyGetSetDef socket_base_getset[] = {
    { "_timeout", socket_base_get_timeout, socket_base_set_timeout, NULL, NULL },
    { NULL },
};

static PyType_Slot socket_base_slots[] = {
    { Py_tp_new, socket_base_new },
    { Py_tp_members, socket_base_members },
    { Py_tp_getset, socket_base_getset },
    { 0, NULL },
};

static PyType_Spec socket_base_spec = {
    "_lucifer.socket_base", sizeof(lu_socket_base), 0, Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    socket_base_slots,
};

static int
lucifer_exec(PyObject *m)
{
    struct lu_prim_state *state = PyModule_GetState(m);
    state->interp = PyInterpreterState_Get();
    if (add_functions(m, lu_prim_type_methods) < 0 || add_functions(m, lu_prim_descr_methods) < 0
        || add_functions(m, lu_prim_slot_methods) < 0 || add_functions(m, lu_prim_int_methods) < 0
        || add_functions(m, lu_prim_float_methods) < 0 || add_functions(m, lu_prim_complex_methods) < 0
        || add_functions(m, lu_prim_math_methods) < 0
        || add_functions(m, lu_prim_calendar_methods) < 0
        || add_functions(m, lu_prim_datetime_methods) < 0
        || add_functions(m, lu_prim_context_methods) < 0
        || add_functions(m, lu_prim_zlib_methods) < 0
        || add_functions(m, lu_prim_ctypes_methods) < 0
        || add_functions(m, lu_prim_passwd_methods) < 0
        || add_functions(m, lu_prim_mt_methods) < 0
        || add_functions(m, lu_prim_faulthandler_methods) < 0
        || add_functions(m, lu_prim_ucd_methods) < 0
        || add_functions(m, lu_prim_compiler_methods) < 0
        || add_functions(m, lu_prim_str_methods) < 0 || add_functions(m, lu_prim_bytes_methods) < 0
        || add_functions(m, lu_prim_mv_methods) < 0
        || add_functions(m, lu_prim_exc_methods) < 0
        || add_functions(m, lu_prim_storage_methods) < 0
        || add_functions(m, lu_prim_deque_methods) < 0
        || add_functions(m, lu_prim_dict_methods) < 0
        || add_functions(m, lu_prim_set_methods) < 0
        || add_functions(m, lu_prim_gen_methods) < 0
        || add_functions(m, lu_prim_frame_methods) < 0
        || add_functions(m, lu_prim_builtins_methods) < 0
        || add_functions(m, lu_prim_import_methods) < 0
        || add_functions(m, lu_prim_code_methods) < 0
        || add_functions(m, lu_prim_thread_methods) < 0
        || add_functions(m, lu_prim_gc_methods) < 0
        || add_functions(m, lu_prim_signal_methods) < 0
        || add_functions(m, lu_prim_weakref_methods) < 0
        || add_functions(m, lu_prim_os_methods) < 0) {
        return -1;
    }
    PyObject *name = PyUnicode_InternFromString("MISSING");
    int err = name == NULL || lu_ns_insert(PyModule_GetDict(m), name, LU_MISSING) < 0;
    Py_XDECREF(name);
    /* What an uninitialized list's slots answer, its repr's "<NULL>": never
       interned, so no string a program holds - sys.intern("<NULL>") included -
       is the same object as the slot's answer */
    PyObject *null_item = PyUnicode_FromString("<NULL>");
    if (null_item == NULL) {
        return -1;
    }
    lu_set_immortal(null_item);
    _PyRuntime.singletons.null_item = null_item;
    name = PyUnicode_InternFromString("NULL_ITEM");
    err = name == NULL || lu_ns_insert(PyModule_GetDict(m), name, null_item) < 0;
    Py_XDECREF(name);
    return err ? -1 : 0;
}

/* module_init(module, name, doc): module.__init__'s dict. */
static PyObject *
prim_module_init(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 3 || !PyModule_Check(args[0]) || !PyUnicode_Check(args[1])) {
        lu_err_format(PyExc_TypeError, "module_init(module, name, doc)");
        return NULL;
    }
    return module_init_dict((PyModuleObject *)args[0], args[1], args[2]) < 0 ? NULL : Py_NewRef(Py_None);
}

/* socket_base(): a new socket_base type, for the _socket module being made. */
static PyObject *
prim_socket_base(PyObject *m, PyObject *unused)
{
    return PyType_FromSpec(&socket_base_spec);
}

/* socket_capi(type, error, timeout): _socket.CAPI, the capsule of
   PySocketModule_APIObject (Modules/socketmodule.h) other extensions import.
   The record lives as long as the process, as the module's does. */
static PyObject *
prim_socket_capi(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 3 || !PyType_Check(args[0])) {
        lu_err_format(PyExc_TypeError, "socket_capi(type, error, timeout)");
        return NULL;
    }
    struct { PyTypeObject *Sock_Type; PyObject *error, *timeout_error; } *api = PyMem_Malloc(sizeof *api);
    if (api == NULL) {
        return PyErr_NoMemory();
    }
    api->Sock_Type = (PyTypeObject *)Py_NewRef(args[0]);
    api->error = Py_NewRef(args[1]);
    api->timeout_error = Py_NewRef(args[2]);
    return PyCapsule_New(api, "_socket.CAPI", NULL);
}

static PyMethodDef lucifer_methods[] = {
    { "module_init", (PyCFunction)(void (*)(void))prim_module_init, METH_FASTCALL,
      "module_init(module, name, doc): the namespace of a new module." },
    { "socket_base", prim_socket_base, METH_NOARGS, "socket_base(): the instance layout of _socket.socket." },
    { "socket_capi", (PyCFunction)(void (*)(void))prim_socket_capi, METH_FASTCALL,
      "socket_capi(type, error, timeout): the _socket.CAPI capsule." },
    { "write", (PyCFunction)(void (*)(void))lu_prim_write, METH_FASTCALL,
      "write(fd, str): write str to fd as UTF-8, all of it." },
    { "str_join", (PyCFunction)(void (*)(void))lu_prim_str_join, METH_FASTCALL | LU_METH_NOPY,
      "str_join(sep, items): the strs of a tuple or list, joined by sep." },
    { "text_repr", (PyCFunction)(void (*)(void))lu_prim_text_repr, METH_FASTCALL,
      "text_repr(obj, limit): repr() of a str or bytes, cut to limit characters." },
    { NULL, NULL, 0, NULL },
};

static PyModuleDef_Slot lucifer_slots[] = {
    { Py_mod_exec, (void *)lucifer_exec },
    { Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED },
    { Py_mod_gil, Py_MOD_GIL_NOT_USED },
    { 0, NULL },
};

PyModuleDef lu_lucifer_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "_lucifer",
    .m_doc = "Native primitives under the kernel's Python builtins.",
    .m_size = sizeof(struct lu_prim_state),
    .m_methods = lucifer_methods,
    .m_slots = lucifer_slots,
};
