/* The file C-API (Objects/fileobject.c and the Linux half of Py_fopen in
   Python/fileutils.c): file-like wrappers over _io.open, the std printer
   sys.stderr boots with, and fopen with a Python error on failure. */

#include "lucifer.h"
#include "compiler/include/lu_cc.h"     // _Py_UniversalNewlineFgetsWithSize()

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

/* A blocking call between lu_syscall_begin and lu_syscall_end, which keeps
   errno (runtime/prim/os.c's LU_SYSCALL, local to that file). */
static FILE *
sys_fopen(const char *path, const char *mode)
{
    PyThreadState *ts = lu_syscall_begin();
    FILE *f = fopen(path, mode);
    lu_syscall_end(ts);
    return f;
}

static Py_ssize_t
sys_write(int fd, const void *buf, size_t count)
{
    PyThreadState *ts = lu_syscall_begin();
    Py_ssize_t n = write(fd, buf, count);
    lu_syscall_end(ts);
    return n;
}

/* PyFile_FromFd: _io.open() with the raw C arguments (the name attribute of
   _BufferedIOMixin and TextIOWrapper is read only, so it is ignored). */
PyObject *
PyFile_FromFd(int fd, const char *name, const char *mode, int buffering, const char *encoding,
              const char *errors, const char *newline, int closefd)
{
    PyObject *open = PyImport_ImportModuleAttrString("_io", "open");
    if (open == NULL) {
        return NULL;
    }
    PyObject *stream = PyObject_CallFunction(open, "isisssO", fd, mode,
                                             buffering, encoding, errors,
                                             newline, closefd ? Py_True : Py_False);
    Py_DECREF(open);
    return stream;
}

/* PyFile_GetLine: file.readline(), stripped of "\n" if n < 0. */
PyObject *
PyFile_GetLine(PyObject *f, int n)
{
    PyObject *result;

    if (f == NULL) {
        PyErr_BadInternalCall();
        return NULL;
    }

    if (n <= 0) {
        result = PyObject_CallMethod(f, "readline", NULL);
    }
    else {
        result = PyObject_CallMethod(f, "readline", "i", n);
    }
    if (result != NULL && !PyBytes_Check(result) &&
        !PyUnicode_Check(result)) {
        Py_SETREF(result, NULL);
        PyErr_SetString(PyExc_TypeError,
                   "object.readline() returned non-string");
    }

    if (n < 0 && result != NULL && PyBytes_Check(result)) {
        const char *s = PyBytes_AS_STRING(result);
        Py_ssize_t len = PyBytes_GET_SIZE(result);
        if (len == 0) {
            Py_SETREF(result, NULL);
            PyErr_SetString(PyExc_EOFError,
                            "EOF when reading a line");
        }
        else if (s[len-1] == '\n') {
            (void) _PyBytes_Resize(&result, len-1);
        }
    }
    if (n < 0 && result != NULL && PyUnicode_Check(result)) {
        Py_ssize_t len = PyUnicode_GET_LENGTH(result);
        if (len == 0) {
            Py_SETREF(result, NULL);
            PyErr_SetString(PyExc_EOFError,
                            "EOF when reading a line");
        }
        else if (PyUnicode_READ_CHAR(result, len-1) == '\n') {
            PyObject *v;
            v = PyUnicode_Substring(result, 0, len-1);
            Py_SETREF(result, v);
        }
    }
    return result;
}

/* Interfaces to write objects/strings to file-like objects */

int
PyFile_WriteObject(PyObject *v, PyObject *f, int flags)
{
    PyObject *writer, *value, *result;

    if (f == NULL) {
        PyErr_SetString(PyExc_TypeError, "writeobject with NULL file");
        return -1;
    }
    writer = PyObject_GetAttrString(f, "write");
    if (writer == NULL)
        return -1;
    if (flags & Py_PRINT_RAW) {
        value = PyObject_Str(v);
    }
    else
        value = PyObject_Repr(v);
    if (value == NULL) {
        Py_DECREF(writer);
        return -1;
    }
    result = PyObject_CallOneArg(writer, value);
    Py_DECREF(value);
    Py_DECREF(writer);
    if (result == NULL)
        return -1;
    Py_DECREF(result);
    return 0;
}

int
PyFile_WriteString(const char *s, PyObject *f)
{
    if (f == NULL) {
        /* Should be caused by a pre-existing error */
        if (!PyErr_Occurred())
            PyErr_SetString(PyExc_SystemError,
                            "null file for PyFile_WriteString");
        return -1;
    }
    else if (!PyErr_Occurred()) {
        PyObject *v = PyUnicode_FromString(s);
        int err;
        if (v == NULL)
            return -1;
        err = PyFile_WriteObject(v, f, Py_PRINT_RAW);
        Py_DECREF(v);
        return err;
    }
    else
        return -1;
}

/* Py_UniversalNewlineFgets: an fgets variation that understands all of \r,
   \n and \r\n conventions, over the loop _Py_UniversalNewlineFgetsWithSize
   already implements (runtime/compiler/lu_cc.c). */
char *
Py_UniversalNewlineFgets(char *buf, int n, FILE *stream, PyObject *fobj)
{
    size_t size;
    return _Py_UniversalNewlineFgetsWithSize(buf, n, stream, fobj, &size);
}

/* **************************** std printer ****************************
 * The stdprinter is used during the boot strapping phase as a preliminary
 * file like object for sys.stderr.
 */

typedef struct {
    PyObject_HEAD
    int fd;
} PyStdPrinter_Object;

static void
stdprinter_dealloc(PyObject *op)
{
    lu_object_free(op);
}

PyObject *
PyFile_NewStdPrinter(int fd)
{
    PyStdPrinter_Object *self;

    if (fd != fileno(stdout) && fd != fileno(stderr)) {
        /* not enough infrastructure for PyErr_BadInternalCall() */
        return NULL;
    }

    self = (PyStdPrinter_Object *)lu_object_new(&PyStdPrinter_Type, sizeof(PyStdPrinter_Object));
    if (self != NULL) {
        self->fd = fd;
    }
    return (PyObject*)self;
}

static PyObject *
stdprinter_write(PyObject *op, PyObject *args)
{
    PyStdPrinter_Object *self = (PyStdPrinter_Object*)op;
    PyObject *unicode;
    PyObject *bytes = NULL;
    const char *str;
    Py_ssize_t n;
    int err;

    /* The function can clear the current exception */
    assert(!PyErr_Occurred());

    if (self->fd < 0) {
        /* fd might be invalid on Windows
         * I can't raise an exception here. It may lead to an
         * unlimited recursion in the case stderr is invalid.
         */
        Py_RETURN_NONE;
    }

    if (!PyArg_ParseTuple(args, "U", &unicode)) {
        return NULL;
    }

    /* Encode Unicode to UTF-8/backslashreplace */
    str = PyUnicode_AsUTF8AndSize(unicode, &n);
    if (str == NULL) {
        PyErr_Clear();
        bytes = PyUnicode_AsEncodedString(unicode, "utf-8", "backslashreplace");
        if (bytes == NULL)
            return NULL;
        str = PyBytes_AS_STRING(bytes);
        n = PyBytes_GET_SIZE(bytes);
    }

    do {
        n = sys_write(self->fd, str, (size_t)n);
    } while (n < 0 && errno == EINTR && PyErr_CheckSignals() == 0);
    /* save errno, it can be modified indirectly by Py_XDECREF() */
    err = errno;

    Py_XDECREF(bytes);

    if (n == -1) {
        if (err == EAGAIN) {
            PyErr_Clear();
            Py_RETURN_NONE;
        }
        if (!PyErr_Occurred()) {
            errno = err;
            PyErr_SetFromErrno(PyExc_OSError);
        }
        return NULL;
    }

    return PyLong_FromSsize_t(n);
}

static PyObject *
stdprinter_fileno(PyObject *op, PyObject *Py_UNUSED(ignored))
{
    PyStdPrinter_Object *self = (PyStdPrinter_Object*)op;
    return PyLong_FromLong((long) self->fd);
}

static PyObject *
stdprinter_noop(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    Py_RETURN_NONE;
}

static PyObject *
stdprinter_isatty(PyObject *op, PyObject *Py_UNUSED(ignored))
{
    PyStdPrinter_Object *self = (PyStdPrinter_Object*)op;
    long res;
    if (self->fd < 0) {
        Py_RETURN_FALSE;
    }

    PyThreadState *ts = lu_syscall_begin();
    res = isatty(self->fd);
    lu_syscall_end(ts);

    return PyBool_FromLong(res);
}

static PyMethodDef stdprinter_methods[] = {
    {"close", stdprinter_noop, METH_NOARGS, ""},
    {"flush", stdprinter_noop, METH_NOARGS, ""},
    {"fileno", stdprinter_fileno, METH_NOARGS, ""},
    {"isatty", stdprinter_isatty, METH_NOARGS, ""},
    {"write", stdprinter_write, METH_VARARGS, ""},
    {NULL, NULL, 0, NULL}  /*sentinel */
};

static PyObject *
get_closed(PyObject *self, void *Py_UNUSED(closure))
{
    Py_RETURN_FALSE;
}

static PyObject *
get_mode(PyObject *self, void *Py_UNUSED(closure))
{
    return PyUnicode_FromString("w");
}

static PyObject *
get_encoding(PyObject *self, void *Py_UNUSED(closure))
{
    Py_RETURN_NONE;
}

static PyGetSetDef stdprinter_getsetlist[] = {
    {"closed", get_closed, NULL, "True if the file is closed", NULL},
    {"encoding", get_encoding, NULL, "Encoding of the file", NULL},
    {"mode", get_mode, NULL, "String giving the file mode", NULL},
    {NULL, NULL, NULL, NULL, NULL},
};

/* The layout and dict entries of the stderrprinter static type, filled in
   boot_native once the static type dicts exist (the capsule type's
   lu_capsule_init pattern, with lu_types_init's native descriptor inserts:
   the kernel's dict functions are not up yet). tp_new stays NULL: a static
   type with no tp_new cannot be instantiated. */
int
lu_fileobject_init(void)
{
    PyStdPrinter_Type.tp_basicsize = sizeof(PyStdPrinter_Object);
    PyStdPrinter_Type.tp_dealloc = stdprinter_dealloc;
    PyObject *dict = lu_type_dict(&PyStdPrinter_Type);
    for (PyMethodDef *ml = stdprinter_methods; ml->ml_name != NULL; ml++) {
        PyObject *d = PyDescr_NewMethod(&PyStdPrinter_Type, ml);
        PyObject *name = d != NULL ? PyUnicode_InternFromString(ml->ml_name) : NULL;
        int err = name == NULL || lu_ns_insert(dict, name, d) < 0;
        Py_XDECREF(name);
        Py_XDECREF(d);
        if (err) {
            return -1;
        }
    }
    PyStdPrinter_Type.tp_getset = stdprinter_getsetlist;
    return lu_type_add_descriptors(&PyStdPrinter_Type);
}

/* ************************** Py_fopen/Py_fclose **************************
   The Linux half of Python/fileutils.c's Py_fopen: encode the path with the
   filesystem encoding, fopen() with the GIL released and EINTR retried, and
   raise OSError (with the filename) from errno on failure. */

static int
set_cloexec(int fd)
{
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return -1;
    }
    return 0;
}

FILE *
Py_fopen(PyObject *path, const char *mode)
{
    if (PySys_Audit("open", "Osi", path, mode, 0) < 0) {
        return NULL;
    }

    PyObject *bytes;
    if (!PyUnicode_FSConverter(path, &bytes)) {
        return NULL;
    }
    const char *path_bytes = PyBytes_AS_STRING(bytes);

    FILE *f;
    int async_err = 0;
    int saved_errno;
    do {
        f = sys_fopen(path_bytes, mode);
    } while (f == NULL
             && errno == EINTR && !(async_err = PyErr_CheckSignals()));
    saved_errno = errno;
    Py_DECREF(bytes);

    if (async_err) {
        return NULL;
    }

    if (f == NULL) {
        errno = saved_errno;
        PyErr_SetFromErrnoWithFilenameObject(PyExc_OSError, path);
        return NULL;
    }

    if (set_cloexec(fileno(f)) < 0) {
        fclose(f);
        return NULL;
    }
    return f;
}

int
Py_fclose(FILE *file)
{
    return fclose(file);
}
