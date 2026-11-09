/* pickle.PickleBuffer (Objects/picklebufobject.c, adapted): a wrapper for an
   out-of-band buffer, which _pickle makes and unpacks through the C API. */

#include "lucifer.h"

typedef struct {
    PyObject_HEAD
    Py_buffer view;             /* the view exported by the original object */
    PyObject *weakreflist;
} picklebuf;

static PyObject *
picklebuf_from(PyTypeObject *type, PyObject *base)
{
    picklebuf *self = (picklebuf *)lu_object_new(type, sizeof(picklebuf));
    if (self != NULL && PyObject_GetBuffer(base, &self->view, PyBUF_FULL_RO) < 0) {
        Py_CLEAR(self);
    }
    return (PyObject *)self;
}

PyObject *
PyPickleBuffer_FromObject(PyObject *base)
{
    return picklebuf_from(&PyPickleBuffer_Type, base);
}

static picklebuf *
checked(PyObject *obj)
{
    if (!PyPickleBuffer_Check(obj)) {
        PyErr_Format(PyExc_TypeError, "expected PickleBuffer, %.200s found", Py_TYPE(obj)->tp_name);
        return NULL;
    }
    return (picklebuf *)obj;
}

static int
released(picklebuf *self)
{
    if (self->view.obj == NULL) {
        PyErr_SetString(PyExc_ValueError, "operation forbidden on released PickleBuffer object");
        return 1;
    }
    return 0;
}

const Py_buffer *
PyPickleBuffer_GetBuffer(PyObject *obj)
{
    picklebuf *self = checked(obj);
    return self == NULL || released(self) ? NULL : &self->view;
}

int
PyPickleBuffer_Release(PyObject *obj)
{
    picklebuf *self = checked(obj);
    if (self == NULL) {
        return -1;
    }
    PyBuffer_Release(&self->view);
    return 0;
}

static PyObject *
picklebuf_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *base;
    char *keywords[] = { "", NULL };
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O:PickleBuffer", keywords, &base)) {
        return NULL;
    }
    return picklebuf_from(type, base);
}

static int
picklebuf_traverse(PyObject *op, visitproc visit, void *arg)
{
    Py_VISIT(((picklebuf *)op)->view.obj);
    return 0;
}

static int
picklebuf_clear(PyObject *op)
{
    PyBuffer_Release(&((picklebuf *)op)->view);
    return 0;
}

static void
picklebuf_dealloc(PyObject *op)
{
    lu_clear_weakrefs(op);
    PyBuffer_Release(&((picklebuf *)op)->view);
    lu_object_free(op);
}

/* The exported buffer is the original object's. */
static int
picklebuf_getbuf(PyObject *op, Py_buffer *view, int flags)
{
    picklebuf *self = (picklebuf *)op;
    return released(self) ? -1 : PyObject_GetBuffer(self->view.obj, view, flags);
}

/* Never called (getbuf redirects); it marks the release as non-trivial. */
static void
picklebuf_releasebuf(PyObject *self, Py_buffer *view)
{
}

static PyBufferProcs picklebuf_as_buffer = {
    .bf_getbuffer = picklebuf_getbuf,
    .bf_releasebuffer = picklebuf_releasebuf,
};

static PyObject *
picklebuf_raw(PyObject *op, PyObject *unused)
{
    picklebuf *self = (picklebuf *)op;
    if (released(self)) {
        return NULL;
    }
    if (self->view.suboffsets != NULL || !PyBuffer_IsContiguous(&self->view, 'A')) {
        PyErr_SetString(PyExc_BufferError, "cannot extract raw buffer from non-contiguous buffer");
        return NULL;
    }
    PyObject *m = PyMemoryView_FromObject(op);
    if (m == NULL) {
        return NULL;
    }
    /* The memoryview made "raw": one dimension of bytes. */
    PyMemoryViewObject *mv = (PyMemoryViewObject *)m;
    mv->view.format = "B";
    mv->view.ndim = 1;
    mv->view.itemsize = 1;
    mv->view.shape = &mv->view.len;
    mv->view.strides = &mv->view.itemsize;
    mv->flags = _Py_MEMORYVIEW_C | _Py_MEMORYVIEW_FORTRAN;
    return m;
}

static PyObject *
picklebuf_release(PyObject *op, PyObject *unused)
{
    PyBuffer_Release(&((picklebuf *)op)->view);
    Py_RETURN_NONE;
}

static PyMethodDef picklebuf_methods[] = {
    { "raw", picklebuf_raw, METH_NOARGS,
      "raw($self, /)\n--\n\nReturn a memoryview of the raw memory underlying this buffer.\n"
      "Will raise BufferError is the buffer isn't contiguous." },
    { "release", picklebuf_release, METH_NOARGS,
      "release($self, /)\n--\n\nRelease the underlying buffer exposed by the PickleBuffer object." },
    { NULL, NULL },
};

void
lu_picklebuf_init(void)
{
    PyTypeObject *t = &PyPickleBuffer_Type;
    t->tp_doc = "Wrapper for potentially out-of-band buffers";
    t->tp_basicsize = sizeof(picklebuf);
    t->tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    t->tp_new = picklebuf_new;
    t->tp_dealloc = picklebuf_dealloc;
    t->tp_traverse = picklebuf_traverse;
    t->tp_clear = picklebuf_clear;
    t->tp_weaklistoffset = offsetof(picklebuf, weakreflist);
    t->tp_as_buffer = &picklebuf_as_buffer;
    t->tp_methods = picklebuf_methods;
}
