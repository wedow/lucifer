/* Descriptor, method and kernel-function layouts (Objects/descrobject.c,
   classobject.c, funcobject.c's classmethod and staticmethod, the layout
   halves of property, super and mappingproxy). Their behaviour, __get__ and
   __set__ among it, is kernel Python (lib/kernel/descriptors.py); these are
   the constructors, members and getsets over the structs, and the calls C
   makes into kernel functions.

   A kernel function looks native (DESIGN 4.4): its PyMethodDef heads an
   lu_kernel_def whose ml_meth is the marker lu_kernel_meth, and the Python
   function hangs off the def. The interpreter runs that function inline
   (lu_kernel_target); C callers reach it through the vectorcalls below,
   which run it in a nested eval loop. */

#include "interp/frame.h"
#include "prim/prim.h"
#include "opcode.h"

#include <math.h>
#include <string.h>

extern PyTypeObject _PyUnion_Type;      /* internal in CPython, defined in types/static.c */

/* property, classmethod/staticmethod, super and mappingproxy, as in CPython. */
typedef struct {
    PyObject_HEAD
    PyObject *prop_get, *prop_set, *prop_del, *prop_doc, *prop_name;
    int getter_doc;
} propertyobject;

typedef struct {
    PyObject_HEAD
    PyObject *cm_callable;
    PyObject *cm_dict;
} classmethod;

typedef struct {
    PyObject_HEAD
    PyTypeObject *type;
    PyObject *obj;
    PyTypeObject *obj_type;
} superobject;

typedef struct {
    PyObject_HEAD
    PyObject *mapping;
} mappingproxyobject;

typedef struct {
    PyObject_HEAD
    PyWrapperDescrObject *descr;
    PyObject *self;
} wrapperobject;

/* A kernel getset's two functions; MISSING is passed for a deletion. */
struct lu_kernel_getset {
    PyGetSetDef def;
    PyObject *fget, *fset, *name;
};

/* Kernel wrapper descriptors share this wrapperbase; d_wrapped is the def. */
static PyObject *kernel_wrapper(PyObject *self, PyObject *args, void *wrapped, PyObject *kwargs);
static struct wrapperbase kernel_wrapperbase = {
    .name = "kernel", .wrapper = (wrapperfunc)(void (*)(void))kernel_wrapper,
    .flags = PyWrapperFlag_KEYWORDS };

PyObject *
lu_kernel_meth(PyObject *self, PyObject *args)
{
    lu_err_format(PyExc_SystemError, "a kernel function was called through its ml_meth");
    return NULL;
}

struct lu_kernel_def *
lu_kernel_def_new(PyObject *name, PyObject *func, int flags, int bind)
{
    struct lu_kernel_def *k = PyMem_Calloc(1, sizeof *k);
    if (k == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    k->def.ml_name = PyUnicode_AsUTF8(name);
    k->def.ml_meth = lu_kernel_meth;
    k->def.ml_flags = METH_FASTCALL | METH_KEYWORDS | LU_METH_KERNEL | flags;
    PyObject *doc = ((PyFunctionObject *)func)->func_doc;
    k->def.ml_doc = doc != NULL && PyUnicode_Check(doc) ? PyUnicode_AsUTF8(doc) : NULL;
    k->func = Py_NewRef(func);
    k->name = Py_NewRef(name);
    k->bind = bind;
    return k;
}

/* Calls a Python function from C with self prepended when non-NULL. */
static PyObject *
call_prepended(PyObject *func, PyObject *self, PyObject *const *args, size_t nargsf,
               PyObject *kwnames)
{
    if (self == NULL) {
        return lu_call(func, args, nargsf, kwnames);
    }
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    Py_ssize_t total = nargs + (kwnames != NULL ? PyTuple_GET_SIZE(kwnames) : 0);
    PyObject *small[8];
    PyObject **stack = total + 1 <= 8 ? small : PyMem_Malloc((total + 1) * sizeof(PyObject *));
    if (stack == NULL) {
        return PyErr_NoMemory();
    }
    stack[0] = self;
    memcpy(stack + 1, args, total * sizeof(PyObject *));
    PyObject *result = lu_call(func, stack, nargs + 1, kwnames);
    if (stack != small) {
        PyMem_Free(stack);
    }
    return result;
}

/* method_check_args and wrapperdescr_call's checks of an unbound call. */
static int
check_self(PyDescrObject *descr, PyObject *const *args, Py_ssize_t nargs, int wrapper)
{
    if (nargs >= 1 && PyObject_TypeCheck(args[0], descr->d_type)) {
        return 0;
    }
    const char *name = PyUnicode_AsUTF8(descr->d_name);
    if (nargs < 1) {
        lu_err_format(PyExc_TypeError, "descriptor '%s' of '%.100s' object needs an argument",
                      name, descr->d_type->tp_name);
        return -1;
    }
    if (wrapper) {
        lu_err_format(PyExc_TypeError, "descriptor '%s' requires a '%.100s' object but received a '%.100s'",
                      name, descr->d_type->tp_name, Py_TYPE(args[0])->tp_name);
    }
    else {
        lu_err_format(PyExc_TypeError, "descriptor '%s' for '%.100s' objects doesn't apply to a '%.100s' object",
                      name, descr->d_type->tp_name, Py_TYPE(args[0])->tp_name);
    }
    return -1;
}

int
lu_kernel_target(PyObject *callable, PyObject *const *args, Py_ssize_t nargs,
                 PyObject **func, PyObject **self)
{
    PyTypeObject *tp = Py_TYPE(callable);
    struct lu_kernel_def *k;
    *self = NULL;
    if (tp == &PyCFunction_Type) {
        PyCFunctionObject *f = (PyCFunctionObject *)callable;
        if ((k = lu_kernel_def_of(f->m_ml)) == NULL) {
            return 0;
        }
        *func = k->func;
        *self = k->bind ? f->m_self : NULL;
        return 1;
    }
    if (tp == &PyMethodDescr_Type) {
        PyMethodDescrObject *d = (PyMethodDescrObject *)callable;
        if ((k = lu_kernel_def_of(d->d_method)) == NULL) {
            return 0;
        }
        if (check_self((PyDescrObject *)d, args, nargs, 0) < 0) {
            return -1;
        }
        *func = k->func;
        return 1;
    }
    if (tp == &PyWrapperDescr_Type) {
        PyWrapperDescrObject *d = (PyWrapperDescrObject *)callable;
        if (d->d_base != &kernel_wrapperbase) {
            return 0;
        }
        if (check_self((PyDescrObject *)d, args, nargs, 1) < 0) {
            return -1;
        }
        *func = ((struct lu_kernel_def *)d->d_wrapped)->func;
        return 1;
    }
    if (tp == &lu_method_wrapper_type) {
        wrapperobject *w = (wrapperobject *)callable;
        if (w->descr->d_base != &kernel_wrapperbase) {
            return 0;
        }
        *func = ((struct lu_kernel_def *)w->descr->d_wrapped)->func;
        *self = w->self;
        return 1;
    }
    return 0;
}

PyObject *
lu_kernel_function_of(PyObject *callable, PyObject **self)
{
    PyTypeObject *tp = Py_TYPE(callable);
    struct lu_kernel_def *k = NULL;
    *self = NULL;
    if (tp == &PyCFunction_Type) {
        k = lu_kernel_def_of(((PyCFunctionObject *)callable)->m_ml);
        if (k != NULL && k->bind) {
            *self = ((PyCFunctionObject *)callable)->m_self;
        }
    }
    else if (tp == &PyMethodDescr_Type) {
        k = lu_kernel_def_of(((PyMethodDescrObject *)callable)->d_method);
    }
    else if (tp == &PyWrapperDescr_Type && ((PyWrapperDescrObject *)callable)->d_base == &kernel_wrapperbase) {
        k = (struct lu_kernel_def *)((PyWrapperDescrObject *)callable)->d_wrapped;
    }
    return k != NULL ? k->func : NULL;
}

int
lu_new_function(PyObject *new_, PyObject **func)
{
    PyObject *self;
    if (Py_IS_TYPE(new_, &PyStaticMethod_Type)) {
        *func = ((classmethod *)new_)->cm_callable;
        return PyFunction_Check(*func);
    }
    return lu_kernel_target(new_, NULL, 0, func, &self) > 0 && self == NULL;
}

/* The C entry points of kernel functions. */

static PyObject *
kernel_cfunction_vectorcall(PyObject *callable, PyObject *const *args, size_t nargsf,
                            PyObject *kwnames)
{
    PyCFunctionObject *f = (PyCFunctionObject *)callable;
    struct lu_kernel_def *k = (struct lu_kernel_def *)f->m_ml;
    return call_prepended(k->func, k->bind ? f->m_self : NULL, args, nargsf, kwnames);
}

static PyObject *
kernel_method_vectorcall(PyObject *callable, PyObject *const *args, size_t nargsf,
                         PyObject *kwnames)
{
    PyMethodDescrObject *d = (PyMethodDescrObject *)callable;
    if (check_self((PyDescrObject *)d, args, PyVectorcall_NARGS(nargsf), 0) < 0) {
        return NULL;
    }
    return lu_call(((struct lu_kernel_def *)d->d_method)->func, args, nargsf, kwnames);
}

/* A wrapperfunc for kernel slot wrappers called with a tuple and dict from C. */
static PyObject *
kernel_wrapper(PyObject *self, PyObject *args, void *wrapped, PyObject *kwargs)
{
    Py_ssize_t n;
    PyObject *kwnames;
    PyObject **stack = lu_unpack_call(args, kwargs != NULL && ((PyDictObject *)kwargs)->ma_used ? kwargs : NULL,
                                      &n, &kwnames);
    if (stack == NULL) {
        return NULL;
    }
    PyObject *result = call_prepended(((struct lu_kernel_def *)wrapped)->func, self, stack, n, kwnames);
    Py_ssize_t total = n + (kwnames != NULL ? PyTuple_GET_SIZE(kwnames) : 0);
    for (Py_ssize_t i = 0; i < total; i++) {
        Py_DECREF(stack[i]);
    }
    Py_XDECREF(kwnames);
    PyMem_Free(stack - 1);
    return result;
}

/* wrapperdescr_call's last step: only a wrapper flagged
   PyWrapperFlag_KEYWORDS takes keyword arguments. */
static PyObject *
call_wrapper(PyWrapperDescrObject *d, PyObject *self, PyObject *args, void *wrapped,
             PyObject *kwargs)
{
    if (d->d_base->flags & PyWrapperFlag_KEYWORDS) {
        return ((wrapperfunc_kwds)(void (*)(void))d->d_base->wrapper)(self, args, wrapped, kwargs);
    }
    if (kwargs != NULL && ((PyDictObject *)kwargs)->ma_used) {
        lu_err_format(PyExc_TypeError, "wrapper %s() takes no keyword arguments",
                      PyUnicode_AsUTF8(d->d_common.d_name));
        return NULL;
    }
    return d->d_base->wrapper(self, args, wrapped);
}

PyObject *
lu_wrapper_descr_call(PyObject *descr, PyObject *const *args, Py_ssize_t nargs, PyObject *kwargs)
{
    PyWrapperDescrObject *d = (PyWrapperDescrObject *)descr;
    if (check_self((PyDescrObject *)d, args, nargs, 1) < 0) {
        return NULL;
    }
    PyObject *rest = PyTuple_New(nargs - 1);
    for (Py_ssize_t i = 1; rest != NULL && i < nargs; i++) {
        PyTuple_SET_ITEM(rest, i - 1, Py_NewRef(args[i]));
    }
    if (rest == NULL) {
        return NULL;
    }
    PyObject *result = call_wrapper(d, args[0], rest, d->d_wrapped, kwargs);
    Py_DECREF(rest);
    return result;
}

static PyObject *
wrapperdescr_call(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    return lu_wrapper_descr_call(callable, &PyTuple_GET_ITEM(args, 0), PyTuple_GET_SIZE(args), kwargs);
}

static PyObject *
method_wrapper_call(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    wrapperobject *w = (wrapperobject *)callable;
    return call_wrapper(w->descr, w->self, args, w->descr->d_wrapped, kwargs);
}

static PyObject *
kernel_getset_get(PyObject *obj, void *closure)
{
    struct lu_kernel_getset *g = closure;
    return lu_call(g->fget, &obj, 1, NULL);
}

static int
kernel_getset_set(PyObject *obj, PyObject *value, void *closure)
{
    struct lu_kernel_getset *g = closure;
    if (g->fset == NULL) {
        lu_err_format(PyExc_AttributeError, "attribute '%s' of '%.100s' objects is not writable",
                      g->def.name, Py_TYPE(obj)->tp_name);
        return -1;
    }
    PyObject *args[2] = { obj, value != NULL ? value : LU_MISSING };
    PyObject *r = lu_call(g->fset, args, 2, NULL);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

/* Descriptor constructors (descr_new and its users). */

static PyDescrObject *
descr_new(PyTypeObject *descrtype, PyTypeObject *type, const char *name)
{
    PyDescrObject *d = (PyDescrObject *)lu_object_new(descrtype, descrtype->tp_basicsize);
    if (d == NULL) {
        return NULL;
    }
    lu_set_deferred((PyObject *)d);
    d->d_type = (PyTypeObject *)Py_NewRef(type);
    d->d_name = PyUnicode_InternFromString(name);
    if (d->d_name == NULL) {
        Py_DECREF(d);
        return NULL;
    }
    return d;
}

/* A native method descriptor called unbound: the method over args[0], bound. */
static PyObject *
native_method_vectorcall(PyObject *callable, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    PyMethodDescrObject *d = (PyMethodDescrObject *)callable;
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    if (check_self((PyDescrObject *)d, args, nargs, 0) < 0) {
        return NULL;
    }
    /* A METH_METHOD function takes the class the descriptor was made for. */
    PyObject *bound = (d->d_method->ml_flags & METH_METHOD)
        ? PyCMethod_New(d->d_method, args[0], NULL, PyDescr_TYPE(d))
        : PyCFunction_NewEx(d->d_method, args[0], NULL);
    if (bound == NULL) {
        return NULL;
    }
    PyObject *result = lu_call(bound, args + 1, (size_t)(nargs - 1), kwnames);
    Py_DECREF(bound);
    return result;
}

PyObject *
lu_descr_new_method(PyTypeObject *type, PyMethodDef *ml)
{
    PyMethodDescrObject *d = (PyMethodDescrObject *)descr_new(&PyMethodDescr_Type, type, ml->ml_name);
    if (d != NULL) {
        d->d_method = ml;
        d->vectorcall = lu_kernel_def_of(ml) != NULL ? kernel_method_vectorcall : native_method_vectorcall;
    }
    return (PyObject *)d;
}

PyObject *
PyDescr_NewMethod(PyTypeObject *type, PyMethodDef *ml)
{
    return lu_descr_new_method(type, ml);
}

PyObject *
lu_descr_new_classmethod(PyTypeObject *type, PyMethodDef *ml)
{
    PyMethodDescrObject *d = (PyMethodDescrObject *)descr_new(&PyClassMethodDescr_Type, type,
                                                              ml->ml_name);
    if (d != NULL) {
        d->d_method = ml;
    }
    return (PyObject *)d;
}

PyObject *
lu_descr_new_wrapper(PyTypeObject *type, PyObject *name, struct lu_kernel_def *k)
{
    PyWrapperDescrObject *d = (PyWrapperDescrObject *)descr_new(&PyWrapperDescr_Type, type,
                                                                PyUnicode_AsUTF8(name));
    if (d != NULL) {
        d->d_base = &kernel_wrapperbase;
        d->d_wrapped = k;
    }
    return (PyObject *)d;
}

PyObject *
PyDescr_NewWrapper(PyTypeObject *type, struct wrapperbase *base, void *wrapped)
{
    PyWrapperDescrObject *d = (PyWrapperDescrObject *)descr_new(&PyWrapperDescr_Type, type, base->name);
    if (d != NULL) {
        d->d_base = base;
        d->d_wrapped = wrapped;
    }
    return (PyObject *)d;
}

PyObject *
lu_descr_new_member(PyTypeObject *type, PyMemberDef *member)
{
    PyMemberDescrObject *d = (PyMemberDescrObject *)descr_new(&PyMemberDescr_Type, type, member->name);
    if (d != NULL) {
        d->d_member = member;
    }
    return (PyObject *)d;
}

PyObject *
PyDescr_NewMember(PyTypeObject *type, PyMemberDef *member)
{
    return lu_descr_new_member(type, member);
}

PyObject *
lu_descr_new_getset(PyTypeObject *type, PyGetSetDef *getset)
{
    PyGetSetDescrObject *d = (PyGetSetDescrObject *)descr_new(&PyGetSetDescr_Type, type, getset->name);
    if (d != NULL) {
        d->d_getset = getset;
    }
    return (PyObject *)d;
}

PyObject *
PyDescr_NewGetSet(PyTypeObject *type, PyGetSetDef *getset)
{
    return lu_descr_new_getset(type, getset);
}

PyObject *
lu_method_wrapper_new(PyObject *descr, PyObject *self)
{
    wrapperobject *w = (wrapperobject *)lu_object_new(&lu_method_wrapper_type, sizeof(wrapperobject));
    if (w != NULL) {
        w->descr = (PyWrapperDescrObject *)Py_NewRef(descr);
        w->self = Py_NewRef(self);
    }
    return (PyObject *)w;
}

/* A builtin bound to self (PyCMethod_New); kernel ones keep the kernel
   vectorcall. */
static PyObject *
cfunction_bound(PyMethodDef *ml, PyObject *self, PyObject *module)
{
    PyObject *f = PyCFunction_NewEx(ml, self, module);
    if (f != NULL && lu_kernel_def_of(ml) != NULL) {
        ((PyCFunctionObject *)f)->vectorcall = kernel_cfunction_vectorcall;
    }
    return f;
}

PyObject *
lu_kernel_builtin(PyObject *module, PyObject *name, PyObject *func)
{
    struct lu_kernel_def *k = lu_kernel_def_new(name, func, 0, 0);
    PyObject *f = k != NULL ? cfunction_bound(&k->def, module, ((PyModuleObject *)module)->md_name) : NULL;
    if (f != NULL) {
        lu_set_deferred(f);     /* as the top-level function it wraps: inline caches may name it */
    }
    return f;
}

/* PyMethod (Objects/classobject.c). */

static PyObject *
method_vectorcall(PyObject *callable, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    PyMethodObject *m = (PyMethodObject *)callable;
    return call_prepended(m->im_func, m->im_self, args, nargsf, kwnames);
}

PyObject *
PyMethod_New(PyObject *func, PyObject *self)
{
    if (self == NULL) {
        PyErr_BadInternalCall();
        return NULL;
    }
    PyMethodObject *m = (PyMethodObject *)lu_object_new(&PyMethod_Type, sizeof(PyMethodObject));
    if (m != NULL) {
        m->im_func = Py_NewRef(func);
        m->im_self = Py_NewRef(self);
        m->vectorcall = method_vectorcall;
    }
    return (PyObject *)m;
}

/* The instance method (Objects/classobject.c): a function bound to a class,
   callable through, and attribute-transparent over, the function it wraps. */
typedef struct {
    PyObject_HEAD
    PyObject *func;
} instancemethodobject;

PyObject *
PyInstanceMethod_New(PyObject *func)
{
    instancemethodobject *im = (instancemethodobject *)lu_object_new(&PyInstanceMethod_Type,
                                                                     sizeof(instancemethodobject));
    if (im != NULL) {
        im->func = Py_NewRef(func);
    }
    return (PyObject *)im;
}

PyObject *
PyInstanceMethod_Function(PyObject *im)
{
    if (!Py_IS_TYPE(im, &PyInstanceMethod_Type)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return Py_NewRef(((instancemethodobject *)im)->func);
}

static PyObject *
instancemethod_get_doc(PyObject *self, void *context)
{
    return PyObject_GetAttrString(((instancemethodobject *)self)->func, "__doc__");
}

static PyGetSetDef instancemethod_getset[] = {
    {"__doc__", instancemethod_get_doc, NULL, NULL, NULL},
    {NULL},
};

static PyMemberDef instancemethod_members[] = {
    {"__func__", _Py_T_OBJECT, offsetof(instancemethodobject, func), Py_READONLY, NULL},
    {NULL},
};

static PyObject *
instancemethod_getattro(PyObject *self, PyObject *name)
{
    PyObject *descr = _PyType_LookupRef(Py_TYPE(self), name);
    if (descr != NULL) {
        descrgetfunc get = Py_TYPE(descr)->tp_descr_get;
        PyObject *r = get != NULL ? get(descr, self, (PyObject *)Py_TYPE(self)) : Py_NewRef(descr);
        Py_DECREF(descr);
        return r;
    }
    return PyObject_GetAttr(((instancemethodobject *)self)->func, name);
}

static PyObject *
instancemethod_call(PyObject *self, PyObject *args, PyObject *kw)
{
    return PyObject_Call(((instancemethodobject *)self)->func, args, kw);
}

static PyObject *
instancemethod_descr_get(PyObject *descr, PyObject *obj, PyObject *type)
{
    PyObject *func = ((instancemethodobject *)descr)->func;
    return obj == NULL ? Py_NewRef(func) : PyMethod_New(func, obj);
}

static PyObject *
instancemethod_richcompare(PyObject *self, PyObject *other, int op)
{
    if ((op != Py_EQ && op != Py_NE) || !Py_IS_TYPE(self, &PyInstanceMethod_Type)
        || !Py_IS_TYPE(other, &PyInstanceMethod_Type)) {
        Py_RETURN_NOTIMPLEMENTED;
    }
    int eq = PyObject_RichCompareBool(((instancemethodobject *)self)->func,
                                      ((instancemethodobject *)other)->func, Py_EQ);
    if (eq < 0) {
        return NULL;
    }
    return Py_NewRef((op == Py_EQ) == eq ? Py_True : Py_False);
}

static PyObject *
instancemethod_repr(PyObject *self)
{
    PyObject *funcname = PyObject_GetAttrString(((instancemethodobject *)self)->func, "__name__");
    if (funcname != NULL && !PyUnicode_Check(funcname)) {
        Py_CLEAR(funcname);
    }
    if (funcname == NULL) {
        PyErr_Clear();
    }
    char buf[160];
    snprintf(buf, sizeof buf, "<instancemethod %s at %p>",
             funcname != NULL ? PyUnicode_AsUTF8(funcname) : "?", (void *)self);
    Py_XDECREF(funcname);
    return PyUnicode_FromString(buf);
}

PyObject *
PyDictProxy_New(PyObject *mapping)
{
    PyMappingMethods *mm = Py_TYPE(mapping)->tp_as_mapping;
    if (mm == NULL || mm->mp_subscript == NULL || PyList_Check(mapping) || PyTuple_Check(mapping)) {
        lu_err_format(PyExc_TypeError, "mappingproxy() argument must be a mapping, not %.200s",
                      Py_TYPE(mapping)->tp_name);
        return NULL;
    }
    mappingproxyobject *p = (mappingproxyobject *)lu_object_new(&PyDictProxy_Type,
                                                                sizeof(mappingproxyobject));
    if (p != NULL) {
        p->mapping = Py_NewRef(mapping);
    }
    return (PyObject *)p;
}

static PyObject *
classmethod_like_new(PyTypeObject *tp, PyObject *callable)
{
    classmethod *cm = (classmethod *)PyType_GenericAlloc(tp, 0);
    if (cm != NULL) {
        cm->cm_callable = Py_NewRef(callable);
    }
    return (PyObject *)cm;
}

PyObject *
PyClassMethod_New(PyObject *callable)
{
    return classmethod_like_new(&PyClassMethod_Type, callable);
}

PyObject *
PyStaticMethod_New(PyObject *callable)
{
    return classmethod_like_new(&PyStaticMethod_Type, callable);
}

/* Native members and getsets of these layouts. */

static PyObject *
descr_get_qualname(PyObject *self, void *closure)
{
    PyDescrObject *d = (PyDescrObject *)self;
    PyObject *type_qualname = (d->d_type->tp_flags & Py_TPFLAGS_HEAPTYPE)
        ? Py_NewRef(((PyHeapTypeObject *)d->d_type)->ht_qualname) : lu_type_name(d->d_type);
    if (type_qualname == NULL) {
        return NULL;
    }
    PyObject *dot = PyUnicode_FromString(".");
    PyObject *parts = PyTuple_New(2);
    PyObject *result = NULL;
    if (dot != NULL && parts != NULL) {
        PyTuple_SET_ITEM(parts, 0, type_qualname);
        PyTuple_SET_ITEM(parts, 1, Py_NewRef(d->d_name));
        type_qualname = NULL;
        PyObject *args[2] = { dot, parts };
        result = lu_prim_str_join(NULL, args, 2);
    }
    Py_XDECREF(type_qualname);
    Py_XDECREF(dot);
    Py_XDECREF(parts);
    return result;
}

/* A C function's ml_doc may start with "name(signature)\n--\n\n": the signature
   is __text_signature__ and the rest is __doc__ (typeobject.c's
   _PyType_GetDocFromInternalDoc and _PyType_GetTextSignatureFromInternalDoc). */
static PyObject *
internal_doc(const char *name, const char *doc)
{
    doc = doc != NULL ? lu_doc_without_signature(name, doc) : NULL;
    return doc != NULL && *doc ? PyUnicode_FromString(doc) : Py_NewRef(Py_None);
}

/* The signature of a function that documents none, from its calling convention. */
static const char *
signature_from_flags(int flags)
{
    switch (flags & ~METH_COEXIST) {
    case METH_NOARGS: return "($self, /)";
    case METH_NOARGS | METH_CLASS: return "($type, /)";
    case METH_NOARGS | METH_STATIC: return "()";
    case METH_O: return "($self, object, /)";
    case METH_O | METH_CLASS: return "($type, object, /)";
    case METH_O | METH_STATIC: return "(object, /)";
    default: return NULL;
    }
}

static PyObject *
text_signature(const char *name, const char *doc, int flags)
{
    const char *rest = doc != NULL ? lu_doc_without_signature(name, doc) : NULL;
    if (rest != doc) {          /* the signature: from its "(" through the ")" of the marker */
        const char *dot = strrchr(name, '.');
        const char *start = doc + strlen(dot != NULL ? dot + 1 : name);
        return PyUnicode_FromStringAndSize(start, rest - start - (sizeof ")\n--\n\n" - 2));
    }
    const char *by_flags = signature_from_flags(flags);
    return by_flags != NULL ? PyUnicode_FromString(by_flags) : Py_NewRef(Py_None);
}

static PyObject *
internal_text_signature(const PyMethodDef *ml)
{
    return text_signature(ml->ml_name, ml->ml_doc, ml->ml_flags);
}

/* type.__text_signature__ of a static type: the signature its tp_doc starts with. */
PyObject *
lu_type_text_signature(PyTypeObject *tp)
{
    return text_signature(tp->tp_name, tp->tp_doc, 0);
}

static PyObject *
method_descr_get_doc(PyObject *self, void *closure)
{
    PyMethodDef *ml = ((PyMethodDescrObject *)self)->d_method;
    return internal_doc(ml->ml_name, ml->ml_doc);
}

static PyObject *
method_descr_get_text_signature(PyObject *self, void *closure)
{
    return internal_text_signature(((PyMethodDescrObject *)self)->d_method);
}

static PyObject *
getset_descr_get_doc(PyObject *self, void *closure)
{
    const char *doc = ((PyGetSetDescrObject *)self)->d_getset->doc;
    return doc != NULL ? PyUnicode_FromString(doc) : Py_NewRef(Py_None);
}

static PyObject *
none_doc(PyObject *self, void *closure)
{
    return Py_NewRef(Py_None);
}

#define DESCR_MEMBERS \
    { "__objclass__", _Py_T_OBJECT, offsetof(PyDescrObject, d_type), Py_READONLY, NULL }, \
    { "__name__", _Py_T_OBJECT, offsetof(PyDescrObject, d_name), Py_READONLY, NULL }
static PyMemberDef descr_members[] = { DESCR_MEMBERS, { NULL } };
static PyGetSetDef method_descr_getset[] = {
    { "__doc__", method_descr_get_doc, NULL, NULL, NULL },
    { "__text_signature__", method_descr_get_text_signature, NULL, NULL, NULL },
    { "__qualname__", descr_get_qualname, NULL, NULL, NULL },
    { NULL },
};
static PyGetSetDef getset_descr_getset[] = {
    { "__doc__", getset_descr_get_doc, NULL, NULL, NULL },
    { "__qualname__", descr_get_qualname, NULL, NULL, NULL },
    { NULL },
};
static PyObject *
member_descr_get_doc(PyObject *self, void *closure)
{
    const char *doc = ((PyMemberDescrObject *)self)->d_member->doc;
    return doc != NULL ? PyUnicode_FromString(doc) : Py_NewRef(Py_None);
}
static PyGetSetDef member_descr_getset[] = {
    { "__doc__", member_descr_get_doc, NULL, NULL, NULL },
    { "__qualname__", descr_get_qualname, NULL, NULL, NULL },
    { NULL },
};
static PyGetSetDef plain_descr_getset[] = {
    { "__doc__", none_doc, NULL, NULL, NULL },
    { "__qualname__", descr_get_qualname, NULL, NULL, NULL },
    { NULL },
};

static PyObject *
wrapper_get_name(PyObject *self, void *closure)
{
    return Py_NewRef(((wrapperobject *)self)->descr->d_common.d_name);
}

static PyObject *
wrapper_get_objclass(PyObject *self, void *closure)
{
    return Py_NewRef((PyObject *)((wrapperobject *)self)->descr->d_common.d_type);
}

static PyObject *
wrapper_get_qualname(PyObject *self, void *closure)
{
    return descr_get_qualname((PyObject *)((wrapperobject *)self)->descr, NULL);
}

static PyMemberDef wrapper_members[] = {
    { "__self__", _Py_T_OBJECT, offsetof(wrapperobject, self), Py_READONLY, NULL },
    { NULL },
};
static PyGetSetDef wrapper_getset[] = {
    { "__objclass__", wrapper_get_objclass, NULL, NULL, NULL },
    { "__name__", wrapper_get_name, NULL, NULL, NULL },
    { "__qualname__", wrapper_get_qualname, NULL, NULL, NULL },
    { "__doc__", none_doc, NULL, NULL, NULL },
    { NULL },
};

static PyMemberDef method_members[] = {
    { "__func__", _Py_T_OBJECT, offsetof(PyMethodObject, im_func), Py_READONLY, NULL },
    { "__self__", _Py_T_OBJECT, offsetof(PyMethodObject, im_self), Py_READONLY, NULL },
    { NULL },
};

static PyObject *
cfunction_get_name(PyObject *self, void *closure)
{
    return PyUnicode_FromString(((PyCFunctionObject *)self)->m_ml->ml_name);
}

static PyObject *
cfunction_get_qualname(PyObject *self, void *closure)
{
    PyCFunctionObject *f = (PyCFunctionObject *)self;
    if (f->m_self == NULL || PyModule_Check(f->m_self)) {
        return cfunction_get_name(self, NULL);
    }
    PyTypeObject *tp = PyType_Check(f->m_self) ? (PyTypeObject *)f->m_self : Py_TYPE(f->m_self);
    PyObject *qualname = (tp->tp_flags & Py_TPFLAGS_HEAPTYPE)
        ? Py_NewRef(((PyHeapTypeObject *)tp)->ht_qualname) : lu_type_name(tp);
    PyObject *name = cfunction_get_name(self, NULL);
    PyObject *dot = PyUnicode_FromString(".");
    PyObject *parts = qualname && name && dot ? PyTuple_New(2) : NULL;
    PyObject *result = NULL;
    if (parts != NULL) {
        PyTuple_SET_ITEM(parts, 0, Py_NewRef(qualname));
        PyTuple_SET_ITEM(parts, 1, Py_NewRef(name));
        PyObject *args[2] = { dot, parts };
        result = lu_prim_str_join(NULL, args, 2);
        Py_DECREF(parts);
    }
    Py_XDECREF(qualname);
    Py_XDECREF(name);
    Py_XDECREF(dot);
    return result;
}

static PyObject *
cfunction_get_self(PyObject *self, void *closure)
{
    PyObject *s = ((PyCFunctionObject *)self)->m_self;
    return Py_NewRef(s != NULL ? s : Py_None);
}

static PyObject *
cfunction_get_doc(PyObject *self, void *closure)
{
    PyMethodDef *ml = ((PyCFunctionObject *)self)->m_ml;
    return internal_doc(ml->ml_name, ml->ml_doc);
}

/* Appends item, which it steals; -1 when item is NULL or the append fails. */
static int
append(PyObject *list, PyObject *item)
{
    int err = item == NULL || PyList_Append(list, item) < 0 ? -1 : 0;
    Py_XDECREF(item);
    return err;
}

/* "name" or "name=default"; None when the default has no literal repr that
   inspect could parse back. */
static PyObject *
parameter(PyObject *name, PyObject *deflt)
{
    if (deflt == NULL) {
        return Py_NewRef(name);
    }
    if (!(deflt == Py_None || PyBool_Check(deflt) || PyLong_CheckExact(deflt) || PyUnicode_CheckExact(deflt)
          || PyBytes_CheckExact(deflt) || (PyFloat_CheckExact(deflt) && isfinite(PyFloat_AS_DOUBLE(deflt))))) {
        return Py_NewRef(Py_None);
    }
    PyObject *repr = PyObject_Repr(deflt);
    PyObject *text = repr != NULL ? PyUnicode_FromFormat("%U=%U", name, repr) : NULL;
    Py_XDECREF(repr);
    return text;
}

/* A kernel builtin's __text_signature__, from its code as Argument Clinic
   writes one: $module, or $self when it binds its first argument, is
   positional-only. None when a default has no literal repr. */
static PyObject *
kernel_text_signature(struct lu_kernel_def *k)
{
    PyFunctionObject *f = (PyFunctionObject *)k->func;
    PyCodeObject *co = (PyCodeObject *)f->func_code;
    PyObject *names = co->co_localsplusnames;
    int npos = co->co_argcount, nkw = co->co_kwonlyargcount;
    Py_ssize_t ndefaults = f->func_defaults != NULL ? PyTuple_GET_SIZE(f->func_defaults) : 0;
    Py_ssize_t slash = k->bind ? (co->co_posonlyargcount > 0 ? co->co_posonlyargcount : 1)
                               : 1 + co->co_posonlyargcount;
    PyObject *parts = PyList_New(0);
    int err = parts == NULL || (!k->bind && append(parts, PyUnicode_FromString("$module")) < 0);
    for (int i = 0; !err && i < npos + nkw; i++) {
        PyObject *name = PyTuple_GET_ITEM(names, i), *deflt = NULL;
        if (i == npos) {
            err = append(parts, (co->co_flags & CO_VARARGS)
                         ? PyUnicode_FromFormat("*%U", PyTuple_GET_ITEM(names, npos + nkw))
                         : PyUnicode_FromString("*")) < 0;
        }
        if (i < npos && i >= npos - ndefaults) {
            deflt = PyTuple_GET_ITEM(f->func_defaults, i - (npos - ndefaults));
        }
        else if (i >= npos && f->func_kwdefaults != NULL) {
            deflt = PyDict_GetItemWithError(f->func_kwdefaults, name);
            err = err || PyErr_Occurred() != NULL;
        }
        PyObject *text = err ? NULL : i == 0 && k->bind ? PyUnicode_FromString("$self") : parameter(name, deflt);
        if (text == Py_None) {
            Py_DECREF(parts);
            return text;
        }
        err = err || append(parts, text) < 0;
    }
    if (!err && nkw == 0 && (co->co_flags & CO_VARARGS)) {
        err = append(parts, PyUnicode_FromFormat("*%U", PyTuple_GET_ITEM(names, npos))) < 0;
    }
    if (!err && (co->co_flags & CO_VARKEYWORDS)) {
        int at = npos + nkw + ((co->co_flags & CO_VARARGS) != 0);
        err = append(parts, PyUnicode_FromFormat("**%U", PyTuple_GET_ITEM(names, at))) < 0;
    }
    PyObject *slash_text = err ? NULL : PyUnicode_FromString("/");
    err = err || slash_text == NULL || PyList_Insert(parts, slash, slash_text) < 0;
    Py_XDECREF(slash_text);
    PyObject *sep = err ? NULL : PyUnicode_FromString(", ");
    PyObject *joined = sep != NULL ? PyUnicode_Join(sep, parts) : NULL;
    PyObject *result = joined != NULL ? PyUnicode_FromFormat("(%U)", joined) : NULL;
    Py_XDECREF(joined);
    Py_XDECREF(sep);
    Py_XDECREF(parts);
    return result;
}

/* A kernel builtin documents no signature: it has its code's. */
static PyObject *
cfunction_get_text_signature(PyObject *self, void *closure)
{
    PyMethodDef *ml = ((PyCFunctionObject *)self)->m_ml;
    struct lu_kernel_def *k = lu_kernel_def_of(ml);
    return k != NULL ? kernel_text_signature(k) : internal_text_signature(ml);
}

static PyMemberDef cfunction_members[] = {
    { "__module__", _Py_T_OBJECT, offsetof(PyCFunctionObject, m_module), 0, NULL },
    { NULL },
};
static PyGetSetDef cfunction_getset[] = {
    { "__name__", cfunction_get_name, NULL, NULL, NULL },
    { "__qualname__", cfunction_get_qualname, NULL, NULL, NULL },
    { "__self__", cfunction_get_self, NULL, NULL, NULL },
    { "__doc__", cfunction_get_doc, NULL, NULL, NULL },
    { "__text_signature__", cfunction_get_text_signature, NULL, NULL, NULL },
    { NULL },
};

static PyMemberDef classmethod_members[] = {
    { "__func__", _Py_T_OBJECT, offsetof(classmethod, cm_callable), Py_READONLY, NULL },
    { "__wrapped__", _Py_T_OBJECT, offsetof(classmethod, cm_callable), Py_READONLY, NULL },
    { NULL },
};
static PyGetSetDef classmethod_getset[] = {
    { "__dict__", PyObject_GenericGetDict, PyObject_GenericSetDict, NULL, NULL },
    { NULL },
};

static PyMemberDef property_members[] = {
    { "fget", _Py_T_OBJECT, offsetof(propertyobject, prop_get), Py_READONLY, NULL },
    { "fset", _Py_T_OBJECT, offsetof(propertyobject, prop_set), Py_READONLY, NULL },
    { "fdel", _Py_T_OBJECT, offsetof(propertyobject, prop_del), Py_READONLY, NULL },
    { "__doc__", _Py_T_OBJECT, offsetof(propertyobject, prop_doc), 0, NULL },
    { NULL },
};

static PyMemberDef super_members[] = {
    { "__thisclass__", _Py_T_OBJECT, offsetof(superobject, type), Py_READONLY, NULL },
    { "__self__", _Py_T_OBJECT, offsetof(superobject, obj), Py_READONLY, NULL },
    { "__self_class__", _Py_T_OBJECT, offsetof(superobject, obj_type), Py_READONLY, NULL },
    { NULL },
};

/* Deallocation: every field these layouts hold is an object reference
   after the header, except property's trailing int. A heap subclass's
   subtype_dealloc releases the type. */
static void
fields_dealloc(PyObject *op, Py_ssize_t first, Py_ssize_t end)
{
    lu_fields_clear(op, first, end);
    lu_object_free(op);
}

/* One dealloc, traverse and clear per layout of object fields. */
#define FIELDS(name, type, first, last) \
    LU_GC_FIELDS(name, type, first, last) \
    static void name##_dealloc(PyObject *op) \
    { fields_dealloc(op, offsetof(type, first), offsetof(type, last) + sizeof(PyObject *)); }

FIELDS(descr, PyDescrObject, d_type, d_qualname)
FIELDS(bufwrap, lu_buffer_wrapper, mv, obj)

/* map, filter, zip, enumerate, reversed and callable_iterator hold up to
   three object fields, which their kernel methods reach by index. */
#define LU_ITER_FIELDS 3
typedef struct { PyObject_HEAD PyObject *f[LU_ITER_FIELDS]; } iterfields;
FIELDS(iter, iterfields, f[0], f[LU_ITER_FIELDS - 1])

/* The next (index, item) of an exact enumerate over a tuple, list, range or dict
   iterator: 1 with *v the pair, or NULL when it is exhausted or failed; 0 when
   the kernel must step it. The index is the field_get/field_set field, swapped
   under the enumerate's lock: tasks may step one enumerate at once. */
int
lu_enum_next(PyObject *e, PyObject **v)
{
    iterfields *f = (iterfields *)e;
    PyObject *inner = f->f[0];
    PyTypeObject *t = Py_TYPE(inner);
    PyObject *item, *i, *next;
    if (!PyLong_CheckExact(__atomic_load_n(&f->f[1], __ATOMIC_RELAXED))) {
        return 0;               /* an exact int index stays one */
    }
    if (t == &PyListIter_Type || t == &PyTupleIter_Type) {
        item = lu_seqiter_next(inner);
    }
    else if (t == &PyRangeIter_Type) {
        item = lu_rangeiter_next(inner);
    }
    else if (t == &PyDictIterKey_Type || t == &PyDictIterValue_Type || t == &PyDictIterItem_Type) {
        item = lu_dictiter_next(inner);
        if (item == LU_MISSING) {
            Py_DECREF(item);
            item = NULL;
        }
    }
    else {
        return 0;
    }
    *v = NULL;
    if (item == NULL) {
        return 1;
    }
    Py_BEGIN_CRITICAL_SECTION(e);
    i = f->f[1];
    next = lu_long_binary(NB_ADD, i, lu_small_int(1));
    if (next != NULL) {
        f->f[1] = next;         /* the field's reference to i is the pair's */
    }
    Py_END_CRITICAL_SECTION();
    PyObject *pair = next != NULL ? PyTuple_New(2) : NULL;
    if (pair == NULL) {
        if (next != NULL) {
            Py_DECREF(i);
        }
        Py_DECREF(item);
        return 1;
    }
    PyTuple_SET_ITEM(pair, 0, i);
    PyTuple_SET_ITEM(pair, 1, item);
    *v = pair;
    return 1;
}

/* types.GenericAlias and typing.Union hold four object fields: origin,
   args, parameters and starred; args, hashable_args, unhashable_args and
   parameters. */
#define LU_ALIAS_FIELDS 4
typedef struct { PyObject_HEAD PyObject *f[LU_ALIAS_FIELDS]; } aliasfields;
FIELDS(alias, aliasfields, f[0], f[LU_ALIAS_FIELDS - 1])

FIELDS(wrapper, wrapperobject, descr, self)
/* A bound method is weakly referenceable (im_weakreflist). */
LU_GC_FIELDS(method, PyMethodObject, im_func, im_self)
static void
method_dealloc(PyObject *op)
{
    lu_clear_weakrefs(op);
    fields_dealloc(op, offsetof(PyMethodObject, im_func), offsetof(PyMethodObject, im_self) + sizeof(PyObject *));
}
FIELDS(instancemethod, instancemethodobject, func, func)
FIELDS(classmethod, classmethod, cm_callable, cm_dict)
FIELDS(property, propertyobject, prop_get, prop_name)
FIELDS(super, superobject, type, obj_type)
FIELDS(proxy, mappingproxyobject, mapping, mapping)

static void
layout_gc(PyTypeObject *tp, Py_ssize_t size, unsigned long flags, destructor dealloc,
          traverseproc traverse, inquiry clear, PyMemberDef *members, PyGetSetDef *getset)
{
    tp->tp_basicsize = size;
    tp->tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | flags;
    tp->tp_dealloc = dealloc;
    tp->tp_traverse = traverse;
    tp->tp_clear = clear;
    tp->tp_members = members;
    tp->tp_getset = getset;
}

#define layout(tp, size, flags, name, members, getset) \
    layout_gc(tp, size, flags, name##_dealloc, name##_traverse, name##_clear, members, getset)

void
lu_descr_init(void)
{
    layout(&PyMethodDescr_Type, sizeof(PyMethodDescrObject),
           Py_TPFLAGS_HAVE_VECTORCALL | Py_TPFLAGS_METHOD_DESCRIPTOR, descr,
           descr_members, method_descr_getset);
    PyMethodDescr_Type.tp_vectorcall_offset = offsetof(PyMethodDescrObject, vectorcall);
    PyMethodDescr_Type.tp_call = PyVectorcall_Call;
    layout(&PyClassMethodDescr_Type, sizeof(PyMethodDescrObject), 0, descr,
           descr_members, method_descr_getset);
    layout(&PyWrapperDescr_Type, sizeof(PyWrapperDescrObject), Py_TPFLAGS_METHOD_DESCRIPTOR,
           descr, descr_members, plain_descr_getset);
    PyWrapperDescr_Type.tp_call = wrapperdescr_call;
    layout(&PyMemberDescr_Type, sizeof(PyMemberDescrObject), 0, descr,
           descr_members, member_descr_getset);
    layout(&PyGetSetDescr_Type, sizeof(PyGetSetDescrObject), 0, descr,
           descr_members, getset_descr_getset);
    layout(&lu_method_wrapper_type, sizeof(wrapperobject), 0, wrapper,
           wrapper_members, wrapper_getset);
    lu_method_wrapper_type.tp_call = method_wrapper_call;
    layout(&lu_buffer_wrapper_type, sizeof(lu_buffer_wrapper), 0, bufwrap, NULL, NULL);
    lu_buffer_wrapper_type.tp_as_buffer = &lu_buffer_wrapper_procs;
    layout(&PyMethod_Type, sizeof(PyMethodObject), Py_TPFLAGS_HAVE_VECTORCALL, method,
           method_members, NULL);
    PyMethod_Type.tp_vectorcall_offset = offsetof(PyMethodObject, vectorcall);
    PyMethod_Type.tp_call = PyVectorcall_Call;
    PyMethod_Type.tp_weaklistoffset = offsetof(PyMethodObject, im_weakreflist);
    layout(&PyInstanceMethod_Type, sizeof(instancemethodobject), 0, instancemethod,
           instancemethod_members, instancemethod_getset);
    PyInstanceMethod_Type.tp_repr = instancemethod_repr;
    PyInstanceMethod_Type.tp_call = instancemethod_call;
    PyInstanceMethod_Type.tp_getattro = instancemethod_getattro;
    PyInstanceMethod_Type.tp_setattro = PyObject_GenericSetAttr;
    PyInstanceMethod_Type.tp_richcompare = instancemethod_richcompare;
    PyInstanceMethod_Type.tp_descr_get = instancemethod_descr_get;
    PyCFunction_Type.tp_members = cfunction_members;
    PyCFunction_Type.tp_getset = cfunction_getset;
    PyCFunction_Type.tp_call = PyVectorcall_Call;
    PyCMethod_Type.tp_members = cfunction_members;
    PyCMethod_Type.tp_getset = cfunction_getset;
    PyCMethod_Type.tp_call = PyVectorcall_Call;
    layout(&PyClassMethod_Type, sizeof(classmethod), 0, classmethod,
           classmethod_members, classmethod_getset);
    PyClassMethod_Type.tp_dictoffset = offsetof(classmethod, cm_dict);
    layout(&PyStaticMethod_Type, sizeof(classmethod), 0, classmethod,
           classmethod_members, classmethod_getset);
    PyStaticMethod_Type.tp_dictoffset = offsetof(classmethod, cm_dict);
    layout(&PyProperty_Type, sizeof(propertyobject), 0, property, property_members, NULL);
    layout(&PySuper_Type, sizeof(superobject), 0, super, super_members, NULL);
    layout(&PyDictProxy_Type, sizeof(mappingproxyobject), 0, proxy, NULL, NULL);
#define ITER_LAYOUT(tp) layout(&tp, sizeof(iterfields), 0, iter, NULL, NULL)
    ITER_LAYOUT(PyMap_Type);
    ITER_LAYOUT(PyFilter_Type);
    ITER_LAYOUT(PyZip_Type);
    ITER_LAYOUT(PyEnum_Type);
    ITER_LAYOUT(PyReversed_Type);
    ITER_LAYOUT(PyCallIter_Type);
    ITER_LAYOUT(lu_memory_iterator_type);
    layout(&Py_GenericAliasType, sizeof(aliasfields), 0, alias, NULL, NULL);
    layout(&_PyUnion_Type, sizeof(aliasfields), 0, alias, NULL, NULL);
}

/* The _lucifer primitives over descriptors. */

#define ARGS(n) do { if (nargs != (n)) { \
        lu_err_format(PyExc_TypeError, "%s() takes %d arguments (%zd given)", __func__ + 5, (n), nargs); \
        return NULL; } } while (0)

static int
is_descr(PyObject *d, PyTypeObject *tp)
{
    if (!Py_IS_TYPE(d, tp)) {
        lu_err_format(PyExc_TypeError, "expected a %s, not '%.200s'", tp->tp_name, Py_TYPE(d)->tp_name);
        return 0;
    }
    return 1;
}

/* make_function(owner, name, func, kind): a kernel function as a descriptor
   of type `owner`: kind 'method', 'classmethod', 'wrapper', or 'new', the
   builtin __new__ bound to the type. */
static PyObject *
prim_make_function(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(4);
    PyObject *owner = args[0], *name = args[1], *func = args[2];
    const char *kind = PyUnicode_Check(args[3]) ? PyUnicode_AsUTF8(args[3]) : "";
    if (!PyUnicode_CheckExact(name) || !PyFunction_Check(func)) {
        lu_err_format(PyExc_TypeError, "make_function(owner, str, function, kind)");
        return NULL;
    }
    if (!PyType_Check(owner)) {
        lu_err_format(PyExc_TypeError, "make_function: owner must be a type");
        return NULL;
    }
    int flags = !strcmp(kind, "classmethod") ? METH_CLASS : !strcmp(kind, "new") ? METH_STATIC : 0;
    /* A bound method from a descriptor passes its self; __new__ does not. */
    int bind = strcmp(kind, "new") != 0;
    struct lu_kernel_def *k = lu_kernel_def_new(name, func, flags, bind);
    if (k == NULL) {
        return NULL;
    }
    if (!strcmp(kind, "method")) {
        return lu_descr_new_method((PyTypeObject *)owner, &k->def);
    }
    if (!strcmp(kind, "classmethod")) {
        return lu_descr_new_classmethod((PyTypeObject *)owner, &k->def);
    }
    if (!strcmp(kind, "wrapper")) {
        return lu_descr_new_wrapper((PyTypeObject *)owner, name, k);
    }
    if (!strcmp(kind, "new")) {
        return cfunction_bound(&k->def, owner, NULL);
    }
    lu_err_format(PyExc_SystemError, "make_function: bad kind '%s'", kind);
    return NULL;
}

/* _tuplegetter.__get__ for a tuple that has the item: its _index slot is the position. */
static PyObject *
tuplegetter_get(PyObject *descr, PyObject *obj)
{
    PyObject *slot = _PyType_Lookup(Py_TYPE(descr), LU_ID(_index));
    if (!PyTuple_Check(obj) || slot == NULL || !Py_IS_TYPE(slot, &PyMemberDescr_Type)) {
        return NULL;
    }
    PyObject *index = *(PyObject **)((char *)descr + ((PyMemberDescrObject *)slot)->d_member->offset);
    if (index == NULL || !PyLong_CheckExact(index) || !_PyLong_IsCompact((PyLongObject *)index)) {
        return NULL;
    }
    Py_ssize_t i = _PyLong_CompactValue((PyLongObject *)index);
    return 0 <= i && i < Py_SIZE(obj) ? Py_NewRef(PyTuple_GET_ITEM(obj, i)) : NULL;
}

int
lu_descr_get_native(PyObject *descr, PyObject *obj, PyObject **out)
{
    PyObject *v;
    if (Py_TYPE(descr) == (PyTypeObject *)PyInterpreterState_Get()->kernel[LU_K_tuplegetter]) {
        return (*out = tuplegetter_get(descr, obj)) != NULL;
    }
    if (Py_IS_TYPE(descr, &PyMemberDescr_Type)) {
        v = PyMember_GetOne((const char *)obj, ((PyMemberDescrObject *)descr)->d_member);
    }
    else if (Py_IS_TYPE(descr, &PyGetSetDescr_Type)
             && ((PyGetSetDescrObject *)descr)->d_getset->get != NULL
             && ((PyGetSetDescrObject *)descr)->d_getset->get != kernel_getset_get) {
        PyGetSetDef *def = ((PyGetSetDescrObject *)descr)->d_getset;
        v = def->get(obj, def->closure);
    }
    else {
        return 0;
    }
    if (v == NULL) {
        PyErr_Clear();      /* AttributeError and __getattr__ are the kernel's */
        return 0;
    }
    *out = v;
    return 1;
}

const Py_ssize_t lu_classmethod_callable_offset = offsetof(classmethod, cm_callable);

PyObject *
lu_staticmethod_get(PyObject *descr)
{
    return Py_IS_TYPE(descr, &PyStaticMethod_Type) ? lu_try_xget_ref(&((classmethod *)descr)->cm_callable) : NULL;
}

PyObject *
lu_property_fget(PyObject *prop)
{
    if (Py_IS_TYPE(prop, &PyGetSetDescr_Type)) {
        PyGetSetDef *def = ((PyGetSetDescrObject *)prop)->d_getset;
        return def->get == kernel_getset_get ? ((struct lu_kernel_getset *)def->closure)->fget : NULL;
    }
    return __atomic_load_n((PyObject **)((char *)prop + lu_field_offset(&PyProperty_Type, 0)), __ATOMIC_ACQUIRE);
}

static PyObject *descr_bound(PyObject *d, PyObject *obj);

PyObject *
lu_bind_method(PyObject *descr, PyObject *obj)
{
    return PyFunction_Check(descr) ? PyMethod_New(descr, obj) : lu_method_descr_bind(descr, obj);
}

PyObject *
lu_method_descr_bind(PyObject *descr, PyObject *obj)
{
    if (!(Py_IS_TYPE(descr, &PyMethodDescr_Type) || Py_IS_TYPE(descr, &PyWrapperDescr_Type))
        || !PyObject_TypeCheck(obj, PyDescr_TYPE(descr))) {
        return NULL;
    }
    return descr_bound(descr, obj);
}

int
lu_descr_set_native(PyObject *descr, PyObject *obj, PyObject *value)
{
    if (Py_IS_TYPE(descr, &PyMemberDescr_Type)) {
        return PyMember_SetOne((char *)obj, ((PyMemberDescrObject *)descr)->d_member, value) < 0 ? -1 : 1;
    }
    if (Py_IS_TYPE(descr, &PyGetSetDescr_Type)) {
        PyGetSetDef *def = ((PyGetSetDescrObject *)descr)->d_getset;
        if (def->set != NULL && def->set != kernel_getset_set) {
            return def->set(obj, value, def->closure) < 0 ? -1 : 1;
        }
    }
    return 0;
}

/* type_setattro (_type_set in lib/kernel/type.py) without Python: a plain
   value (a function is the kernel's: it makes it deferred) stored under a
   name no slot reads into a mutable class whose metaclass keeps type's
   __setattr__ and has no data descriptor of that name. The store takes the
   class's tag back (lu_attr_store). 1 when done, 0 for the kernel, -1. */
static int
type_setattr_native(PyTypeObject *cls, PyObject *name, PyObject *value)
{
    PyTypeObject *meta = Py_TYPE(cls);
    Py_ssize_t n = PyUnicode_CheckExact(name) ? PyUnicode_GET_LENGTH(name) : 0;
    if (value == NULL || PyFunction_Check(value) || (cls->tp_flags & Py_TPFLAGS_IMMUTABLETYPE) || n == 0
        || (n >= 4 && PyUnicode_READ_CHAR(name, 0) == '_' && PyUnicode_READ_CHAR(name, 1) == '_'
            && PyUnicode_READ_CHAR(name, n - 1) == '_' && PyUnicode_READ_CHAR(name, n - 2) == '_')
        || _PyType_Lookup(meta, LU_ID(__setattr__)) != _PyType_Lookup(&PyType_Type, LU_ID(__setattr__))) {
        return 0;
    }
    PyObject *descr = _PyType_LookupRef(meta, name);
    int data = descr != NULL && Py_TYPE(descr)->tp_descr_set != NULL;
    Py_XDECREF(descr);
    return data ? 0 : lu_attr_store((PyObject *)cls, name, value) < 0 ? -1 : 1;
}

int
lu_setattr_native(PyObject *obj, PyObject *name, PyObject *value)
{
    PyTypeObject *tp = Py_TYPE(obj);
    if (PyType_Check(obj) && (tp->tp_setattro == lu_slot_tp_setattro || tp->tp_setattro == PyObject_GenericSetAttr)) {
        return type_setattr_native((PyTypeObject *)obj, name, value);
    }
    if ((tp->tp_setattro != lu_slot_tp_setattro && tp->tp_setattro != PyObject_GenericSetAttr)
        || !PyUnicode_CheckExact(name)
        || _PyType_Lookup(tp, LU_ID(__setattr__)) != _PyType_Lookup(&PyBaseObject_Type, LU_ID(__setattr__))) {
        return 0;
    }
    PyObject *descr = _PyType_LookupRef(tp, name);
    if (descr != NULL && Py_TYPE(descr)->tp_descr_set != NULL) {
        int done = lu_descr_set_native(descr, obj, value);
        Py_DECREF(descr);
        return done;
    }
    Py_XDECREF(descr);
    if (value == NULL || !(tp->tp_flags & Py_TPFLAGS_MANAGED_DICT)) {
        return 0;
    }
    if (lu_inline_live(obj)) {
        return lu_attr_store(obj, name, value);
    }
    PyObject *dict = PyObject_GenericGetDict(obj, NULL);       /* made on demand */
    if (dict == NULL) {
        return -1;
    }
    int done = !PyDict_CheckExact(dict) ? 0 : lu_ns_store(dict, name, value) < 0 ? -1 : 1;
    Py_DECREF(dict);
    return done;
}

/* setattr_native(obj, name, value) -> True when done, else False */
static PyObject *
prim_setattr_native(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3);
    int done = lu_setattr_native(args[0], args[1], args[2]);
    return done < 0 ? NULL : Py_NewRef(done ? Py_True : Py_False);
}

/* make_getset(type, name, fget, fset or None) */
static PyObject *
prim_make_getset(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(4);
    if (!PyType_Check(args[0]) || !PyUnicode_CheckExact(args[1])) {
        lu_err_format(PyExc_TypeError, "make_getset(type, str, fget, fset)");
        return NULL;
    }
    struct lu_kernel_getset *g = PyMem_Calloc(1, sizeof *g);
    if (g == NULL) {
        return PyErr_NoMemory();
    }
    g->name = Py_NewRef(args[1]);
    g->fget = Py_NewRef(args[2]);
    g->fset = args[3] != Py_None ? Py_NewRef(args[3]) : NULL;
    g->def.name = PyUnicode_AsUTF8(args[1]);
    g->def.get = kernel_getset_get;
    g->def.set = kernel_getset_set;
    PyObject *doc = PyFunction_Check(args[2]) ? ((PyFunctionObject *)args[2])->func_doc : NULL;
    g->def.doc = doc != NULL && PyUnicode_Check(doc) ? PyUnicode_AsUTF8(doc) : NULL;
    g->def.closure = g;
    return lu_descr_new_getset((PyTypeObject *)args[0], &g->def);
}

/* getset_kernel(d): (fget, fset or None) of a kernel getset, or MISSING. */
static PyObject *
prim_getset_kernel(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    if (!is_descr(args[0], &PyGetSetDescr_Type)) {
        return NULL;
    }
    PyGetSetDef *def = ((PyGetSetDescrObject *)args[0])->d_getset;
    if (def->get != kernel_getset_get) {
        return Py_NewRef(LU_MISSING);
    }
    struct lu_kernel_getset *g = def->closure;
    PyObject *pair = PyTuple_New(2);
    if (pair != NULL) {
        PyTuple_SET_ITEM(pair, 0, Py_NewRef(g->fget));
        PyTuple_SET_ITEM(pair, 1, Py_NewRef(g->fset != NULL ? g->fset : Py_None));
    }
    return pair;
}

/* getset_get(d, obj), getset_set(d, obj, value or MISSING): the native
   getter and setter; getset_has_set(d). */
static PyObject *
prim_getset_get(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    if (!is_descr(args[0], &PyGetSetDescr_Type)) {
        return NULL;
    }
    PyGetSetDef *def = ((PyGetSetDescrObject *)args[0])->d_getset;
    if (def->get == NULL) {
        lu_err_format(PyExc_AttributeError, "attribute '%s' of '%.100s' objects is not readable",
                      def->name, ((PyDescrObject *)args[0])->d_type->tp_name);
        return NULL;
    }
    return def->get(args[1], def->closure);
}

static PyObject *
prim_getset_set(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3);
    if (!is_descr(args[0], &PyGetSetDescr_Type)) {
        return NULL;
    }
    PyGetSetDef *def = ((PyGetSetDescrObject *)args[0])->d_getset;
    if (def->set == NULL) {
        lu_err_format(PyExc_AttributeError, "attribute '%s' of '%.100s' objects is not writable",
                      def->name, ((PyDescrObject *)args[0])->d_type->tp_name);
        return NULL;
    }
    PyObject *value = args[2] == LU_MISSING ? NULL : args[2];
    return def->set(args[1], value, def->closure) < 0 ? NULL : Py_NewRef(Py_None);
}

/* member_get(d, obj), member_set(d, obj, value or MISSING) */
static PyObject *
prim_member_get(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    if (!is_descr(args[0], &PyMemberDescr_Type)) {
        return NULL;
    }
    return PyMember_GetOne((const char *)args[1], ((PyMemberDescrObject *)args[0])->d_member);
}

static PyObject *
prim_member_set(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3);
    if (!is_descr(args[0], &PyMemberDescr_Type)) {
        return NULL;
    }
    PyObject *value = args[2] == LU_MISSING ? NULL : args[2];
    return PyMember_SetOne((char *)args[1], ((PyMemberDescrObject *)args[0])->d_member, value) < 0
        ? NULL : Py_NewRef(Py_None);
}

/* descr_type(d): d_type of any descriptor. */
static PyObject *
prim_descr_type(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    PyTypeObject *tp = Py_TYPE(args[0]);
    if (tp != &PyMethodDescr_Type && tp != &PyClassMethodDescr_Type && tp != &PyWrapperDescr_Type
        && tp != &PyMemberDescr_Type && tp != &PyGetSetDescr_Type) {
        lu_err_format(PyExc_TypeError, "descr_type() needs a descriptor");
        return NULL;
    }
    return Py_NewRef((PyObject *)((PyDescrObject *)args[0])->d_type);
}

/* bind(d, obj): a method descriptor bound to obj (a builtin method), a
   classmethod descriptor bound to the type obj, a slot wrapper bound to
   obj (a method-wrapper). The kernel has checked obj's type. */
static PyObject *
descr_bound(PyObject *d, PyObject *obj)
{
    if (Py_IS_TYPE(d, &PyMethodDescr_Type) || Py_IS_TYPE(d, &PyClassMethodDescr_Type)) {
        PyMethodDef *ml = ((PyMethodDescrObject *)d)->d_method;
        /* A METH_METHOD function takes the class the descriptor was made for. */
        return ml->ml_flags & METH_METHOD
            ? PyCMethod_New(ml, obj, NULL, PyDescr_TYPE(d))
            : cfunction_bound(ml, obj, NULL);
    }
    if (Py_IS_TYPE(d, &PyWrapperDescr_Type)) {
        return lu_method_wrapper_new(d, obj);
    }
    return NULL;
}

static PyObject *
prim_bind(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    PyObject *r = descr_bound(args[0], args[1]);
    if (r == NULL && !PyErr_Occurred()) {
        lu_err_format(PyExc_TypeError, "bind() needs a method or wrapper descriptor");
    }
    return r;
}

/* method_new(func, self): PyMethod_New. */
static PyObject *
prim_method_new(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    return PyMethod_New(args[0], args[1]);
}

/* instancemethod_new(func): PyInstanceMethod_New, for the kernel's __new__. */
static PyObject *
prim_instancemethod_new(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    return PyInstanceMethod_New(args[0]);
}

/* field_get(obj, i), field_set(obj, i, value): the object fields after the
   header of the layouts whose behaviour is kernel Python. */
Py_ssize_t
lu_field_offset(PyTypeObject *tp, long i)
{
    extern PyTypeObject lu_coro_wrapper_type, lu_async_gen_athrow_type, lu_async_gen_wrapped_type;
    static PyTypeObject *const types[] = { &PyProperty_Type, &PyClassMethod_Type,
                                           &PyStaticMethod_Type, &PySuper_Type, &PyDictProxy_Type,
                                           &lu_coro_wrapper_type, &_PyAsyncGenASend_Type,
                                           &lu_async_gen_athrow_type, &lu_async_gen_wrapped_type,
                                           &PyMap_Type, &PyFilter_Type, &PyZip_Type,
                                           &PyEnum_Type, &PyReversed_Type, &PyCallIter_Type,
                                           &lu_memory_iterator_type, &PyFrameLocalsProxy_Type,
                                           &Py_GenericAliasType, &_PyUnion_Type };
    static const int counts[] = { 5, 2, 2, 3, 1, 1, 3, 3, 1, 3, 3, 3, 3, 3, 3, 3, 1, 4, 4 };
    /* the exact types first: a subclass instance is the rare case */
    size_t t = 0, n = sizeof types / sizeof types[0];
    while (t < n && tp != types[t]) {
        t++;
    }
    for (t = t < n ? t : 0; t < n && !PyType_IsSubtype(tp, types[t]); t++) {
    }
    return t < n && i >= 0 && i < counts[t] ? (Py_ssize_t)(sizeof(PyObject) + i * sizeof(PyObject *)) : -1;
}

static Py_ssize_t
field_offset(PyObject *obj, PyObject *index)
{
    long i;
    if (!PyLong_CheckExact(index) || lu_long_as_long(index, &i) < 0) {
        return -1;
    }
    Py_ssize_t off = lu_field_offset(Py_TYPE(obj), i);
    if (off < 0) {
        lu_err_format(PyExc_SystemError, "field %ld of a '%s' is not kernel-writable", i,
                      Py_TYPE(obj)->tp_name);
    }
    return off;
}

PyObject *
lu_field_get(PyObject *obj, Py_ssize_t off)
{
    PyObject *v;
    Py_BEGIN_CRITICAL_SECTION(obj);
    v = *(PyObject **)((char *)obj + off);
    v = Py_NewRef(v != NULL ? v : Py_None);
    Py_END_CRITICAL_SECTION();
    return v;
}

void
lu_field_set(PyObject *obj, Py_ssize_t off, PyObject *value)
{
    PyObject *old;
    Py_BEGIN_CRITICAL_SECTION(obj);
    old = *(PyObject **)((char *)obj + off);
    *(PyObject **)((char *)obj + off) = Py_NewRef(value);
    Py_END_CRITICAL_SECTION();
    Py_XDECREF(old);            /* outside the lock: a finalizer may run */
}

static PyObject *
prim_field_get(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    Py_ssize_t off = field_offset(args[0], args[1]);
    return off < 0 ? NULL : lu_field_get(args[0], off);
}

static PyObject *
prim_field_set(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3);
    Py_ssize_t off = field_offset(args[0], args[1]);
    if (off < 0) {
        return NULL;
    }
    lu_field_set(args[0], off, args[2]);
    return Py_NewRef(Py_None);
}

/* proxy_new(mapping): PyDictProxy_New. */
static PyObject *
prim_proxy_new(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    return PyDictProxy_New(args[0]);
}

/* The kernel function table's names, by enum lu_kernel_id. */
static const char *const kernel_names[] = {
#define LU_KERNEL_NAME(name) #name,
    LU_KERNEL_FUNCTIONS(LU_KERNEL_NAME)
#undef LU_KERNEL_NAME
};

static int
kernel_id(PyObject *name)
{
    const char *n = PyUnicode_Check(name) ? PyUnicode_AsUTF8(name) : "";
    for (int i = 0; i < LU_K_COUNT; i++) {
        if (!strcmp(n, kernel_names[i])) {
            return i;
        }
    }
    lu_err_format(PyExc_SystemError, "no kernel function '%s'", n);
    return -1;
}

/* kernel_set(name, func): registers a kernel function the runtime calls. */
static PyObject *
prim_kernel_set(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    int id = kernel_id(args[0]);
    if (id < 0) {
        return NULL;
    }
    Py_XSETREF(PyInterpreterState_Get()->kernel[id], Py_NewRef(args[1]));
    return Py_NewRef(Py_None);
}

/* kernel_twin(f, g): g is a twin of the kernel function f for calls with
   g's fixed arity, which the tier compiles instead of f (lu_kernel_twin):
   f binds *args and **kwargs, g positional parameters only, with f's
   meaning for them (the design of kernel customization, section 5). The
   twins are a list of (f's code, g), made at boot and only read after. */
static PyObject *
prim_kernel_twin(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    if (!PyFunction_Check(args[0]) || !PyFunction_Check(args[1])) {
        lu_err_format(PyExc_TypeError, "kernel_twin() needs two functions");
        return NULL;
    }
    PyInterpreterState *interp = PyInterpreterState_Get();
    if (interp->kernel_twins == NULL && (interp->kernel_twins = PyList_New(0)) == NULL) {
        return NULL;
    }
    PyObject *pair = PyTuple_Pack(2, PyFunction_GET_CODE(args[0]), args[1]);
    int ok = pair != NULL && PyList_Append(interp->kernel_twins, pair) == 0;
    Py_XDECREF(pair);
    return ok ? Py_NewRef(Py_None) : NULL;
}

PyFunctionObject *
lu_kernel_twin(PyFunctionObject *f, int nargs)
{
    PyObject *twins = PyInterpreterState_Get()->kernel_twins;
    for (Py_ssize_t i = 0; twins != NULL && i < PyList_GET_SIZE(twins); i++) {
        PyObject *pair = PyList_GET_ITEM(twins, i);
        PyFunctionObject *g = (PyFunctionObject *)PyTuple_GET_ITEM(pair, 1);
        PyCodeObject *co = (PyCodeObject *)g->func_code;
        if (PyTuple_GET_ITEM(pair, 0) == f->func_code && co->co_argcount == nargs && co->co_kwonlyargcount == 0
            && !(co->co_flags & (CO_VARARGS | CO_VARKEYWORDS))) {
            return g;
        }
    }
    return NULL;
}

/* next_missing_set(t, f): f(it) is the step of an iterator of exact type
   t that gives MISSING at its end instead of raising StopIteration
   (iter.py's next_item); next_missing_of(t): that f, or None. Made at boot
   and only read after, so the tier folds the lookup on a known type. */
static PyObject *
prim_next_missing_set(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    PyInterpreterState *interp = PyInterpreterState_Get();
    if (interp->kernel_nexts == NULL && (interp->kernel_nexts = PyList_New(0)) == NULL) {
        return NULL;
    }
    PyObject *pair = PyTuple_Pack(2, args[0], args[1]);
    int ok = pair != NULL && PyList_Append(interp->kernel_nexts, pair) == 0;
    Py_XDECREF(pair);
    return ok ? Py_NewRef(Py_None) : NULL;
}

PyObject *
lu_next_missing_of(PyObject *t)
{
    PyObject *nexts = PyInterpreterState_Get()->kernel_nexts;
    for (Py_ssize_t i = 0; nexts != NULL && i < PyList_GET_SIZE(nexts); i++) {
        PyObject *pair = PyList_GET_ITEM(nexts, i);
        if (PyTuple_GET_ITEM(pair, 0) == t) {
            return PyTuple_GET_ITEM(pair, 1);
        }
    }
    return Py_None;
}

static PyObject *
prim_next_missing_of(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    return Py_NewRef(lu_next_missing_of(args[0]));
}

/* kernel_get(name): a registered kernel function, for the kernel modules
   after the one that defines it. */
static PyObject *
prim_kernel_get(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    int id = kernel_id(args[0]);
    PyObject *func = id >= 0 ? PyInterpreterState_Get()->kernel[id] : NULL;
    if (id >= 0 && func == NULL) {
        lu_err_format(PyExc_SystemError, "kernel function '%s' is not registered yet",
                      kernel_names[id]);
    }
    return Py_XNewRef(func);
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_descr_methods[] = {
    FAST(kernel_get), FAST(setattr_native),
    FAST(make_function), FAST(make_getset), FAST(getset_kernel), FAST(getset_get),
    FAST(getset_set), FAST(member_get), FAST(member_set), FAST(descr_type), FAST(bind),
    FAST(method_new), FAST(instancemethod_new), FAST(field_get), FAST(field_set), FAST(proxy_new), FAST(kernel_set), FAST(kernel_twin), FAST(next_missing_set), FAST(next_missing_of),
    { NULL, NULL, 0, NULL },
};
