/* Platform primitives. */

#include "prim/prim.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

/* write(fd, str): the whole UTF-8 form of str, retrying short writes and
   EINTR. Raises OSError on failure; the errno-carrying OSError layout
   arrives with K1k. */
PyObject *
lu_prim_write(PyObject *module, PyObject *const *args, Py_ssize_t nargs)
{
    long fd;
    if (nargs != 2 || !PyLong_CheckExact(args[0]) || !PyUnicode_CheckExact(args[1])) {
        lu_err_format(PyExc_TypeError, "write(fd, str) takes an int and a str");
        return NULL;
    }
    if (lu_long_as_long(args[0], &fd) < 0) {
        return NULL;
    }
    Py_ssize_t size;
    const char *data = PyUnicode_AsUTF8AndSize(args[1], &size);
    if (data == NULL) {
        return NULL;
    }
    while (size > 0) {
        ssize_t n = write((int)fd, data, (size_t)size);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            lu_err_format(PyExc_OSError, "[Errno %d] %s", errno, strerror(errno));
            return NULL;
        }
        data += n;
        size -= n;
    }
    return Py_None;
}
