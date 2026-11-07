/* The id tables behind PyType_AddWatcher, PyDict_AddWatcher,
   PyFunction_AddWatcher and PyCode_AddWatcher (FOUNDATIONS 2.4). Each kind
   keeps its callbacks in the interpreter and notifies from its own place;
   what is shared is the handing out and checking of ids. */

#include "lucifer.h"

/* Dict watcher id 0 is CPython's own, taken at startup: here it marks the
   dicts compiled code relies on (runtime/jit/deps.c), and dict.c reports
   their changes after making them, so the callback has no work. */
static int
reserved_dict_callback(PyDict_WatchEvent event, PyObject *dict, PyObject *key, PyObject *value)
{
    return 0;
}

void
lu_watchers_init(struct _is *interp)
{
    interp->next_keys_version = 2;              /* 0 is none, 1 is reserved, as in 3.14 */
    interp->next_func_version = 2;              /* FUNC_VERSION_FIRST_VALID */
    interp->dict_watchers[0] = (lu_watcher)reserved_dict_callback;
}

int
lu_watcher_add(lu_watcher *table, int first, lu_watcher callback, const char *kind)
{
    for (int i = first; i < LU_MAX_WATCHERS; i++) {
        if (table[i] == NULL) {
            table[i] = callback;
            return i;
        }
    }
    lu_err_format(PyExc_RuntimeError, "no more %s watcher IDs available", kind);
    return -1;
}

int
lu_watcher_check(lu_watcher *table, int id, const char *invalid, const char *unset)
{
    if (id < 0 || id >= LU_MAX_WATCHERS) {
        lu_err_format(PyExc_ValueError, invalid, id);
        return -1;
    }
    if (table[id] == NULL) {
        lu_err_format(PyExc_ValueError, unset, id);
        return -1;
    }
    return 0;
}
