/* PyCapsule (Objects/capsule.c): an opaque C pointer with a name, a context
   and a destructor, for passing C data between extensions. */

#include "lucifer.h"

#include <string.h>

typedef struct {
    PyObject_HEAD
    void *pointer;
    const char *name;
    void *context;
    PyCapsule_Destructor destructor;
} capsule;

static void
capsule_dealloc(PyObject *op)
{
    capsule *c = (capsule *)op;
    if (c->destructor != NULL) {
        c->destructor(op);
    }
    lu_object_free(op);
}

void
lu_capsule_init(void)
{
    PyCapsule_Type.tp_basicsize = sizeof(capsule);
    PyCapsule_Type.tp_flags |= Py_TPFLAGS_DEFAULT;
    PyCapsule_Type.tp_dealloc = capsule_dealloc;
}

PyObject *
PyCapsule_New(void *pointer, const char *name, PyCapsule_Destructor destructor)
{
    if (pointer == NULL) {
        PyErr_SetString(PyExc_ValueError, "PyCapsule_New called with null pointer");
        return NULL;
    }
    capsule *c = (capsule *)lu_object_new(&PyCapsule_Type, sizeof(capsule));
    if (c != NULL) {
        c->pointer = pointer;
        c->name = name;
        c->destructor = destructor;
    }
    return (PyObject *)c;
}

static capsule *
valid(PyObject *op, const char *what)
{
    if (op == NULL || !PyCapsule_CheckExact(op) || ((capsule *)op)->pointer == NULL) {
        PyErr_Format(PyExc_ValueError, "%s called with invalid PyCapsule object", what);
        return NULL;
    }
    return (capsule *)op;
}

int
PyCapsule_IsValid(PyObject *op, const char *name)
{
    capsule *c = (capsule *)op;
    return c != NULL && PyCapsule_CheckExact(op) && c->pointer != NULL
        && (c->name == name || (c->name != NULL && name != NULL && !strcmp(c->name, name)));
}

void *
PyCapsule_GetPointer(PyObject *op, const char *name)
{
    capsule *c = valid(op, "PyCapsule_GetPointer");
    if (c == NULL) {
        return NULL;
    }
    if (!(c->name == name || (c->name != NULL && name != NULL && !strcmp(c->name, name)))) {
        PyErr_SetString(PyExc_ValueError, "PyCapsule_GetPointer called with incorrect name");
        return NULL;
    }
    return c->pointer;
}

const char *
PyCapsule_GetName(PyObject *op)
{
    capsule *c = valid(op, "PyCapsule_GetName");
    return c != NULL ? c->name : NULL;
}

void *
PyCapsule_GetContext(PyObject *op)
{
    capsule *c = valid(op, "PyCapsule_GetContext");
    return c != NULL ? c->context : NULL;
}

PyCapsule_Destructor
PyCapsule_GetDestructor(PyObject *op)
{
    capsule *c = valid(op, "PyCapsule_GetDestructor");
    return c != NULL ? c->destructor : NULL;
}

int
PyCapsule_SetPointer(PyObject *op, void *pointer)
{
    capsule *c = valid(op, "PyCapsule_SetPointer");
    if (c == NULL) {
        return -1;
    }
    if (pointer == NULL) {
        PyErr_SetString(PyExc_ValueError, "PyCapsule_SetPointer called with null pointer");
        return -1;
    }
    c->pointer = pointer;
    return 0;
}

int
PyCapsule_SetName(PyObject *op, const char *name)
{
    capsule *c = valid(op, "PyCapsule_SetName");
    if (c == NULL) {
        return -1;
    }
    c->name = name;
    return 0;
}

int
PyCapsule_SetContext(PyObject *op, void *context)
{
    capsule *c = valid(op, "PyCapsule_SetContext");
    if (c == NULL) {
        return -1;
    }
    c->context = context;
    return 0;
}

int
PyCapsule_SetDestructor(PyObject *op, PyCapsule_Destructor destructor)
{
    capsule *c = valid(op, "PyCapsule_SetDestructor");
    if (c == NULL) {
        return -1;
    }
    c->destructor = destructor;
    return 0;
}

/* "package.module.attribute.attribute": import the longest module prefix
   that imports, then take the attributes. */
void *
PyCapsule_Import(const char *name, int no_block)
{
    char *copy = PyMem_Malloc(strlen(name) + 1);
    if (copy == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    strcpy(copy, name);
    PyObject *object = NULL;
    void *result = NULL;
    for (char *trace = copy, *next; trace != NULL; trace = next) {
        next = strchr(trace, '.');
        if (next != NULL) {
            *next++ = '\0';
        }
        PyObject *part = object == NULL ? PyImport_ImportModule(trace)
                                        : PyObject_GetAttrString(object, trace);
        if (part == NULL && object == NULL) {
            PyErr_Clear();
            PyErr_Format(PyExc_ImportError, "PyCapsule_Import could not import module \"%s\"", trace);
        }
        Py_XDECREF(object);
        object = part;
        if (object == NULL) {
            goto done;
        }
    }
    if (object != NULL && PyCapsule_IsValid(object, name)) {
        result = ((capsule *)object)->pointer;
    }
    else {
        PyErr_Format(PyExc_AttributeError, "PyCapsule_Import \"%s\" is not valid", name);
    }
done:
    Py_XDECREF(object);
    PyMem_Free(copy);
    return result;
}
