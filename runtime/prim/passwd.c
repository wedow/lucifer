/* The user and group databases: the _lucifer primitives behind pwd and grp.
   Each answers a tuple of the entry's fields, or None for no such entry.

   libc's reentrant calls copy each entry into a record of ours, detached, since
   a name service may block (CPython releases the GIL around getpwnam_r). An
   enumeration's place in the database is libc's, one per process, so one lock
   serializes the enumerations from setpwent or setgrent to the end, as
   CPython's group_db_mutex does. It is held detached and over libc calls alone,
   never Python code: a waiter counts as stopped for a stop-the-world, the
   holder needs nothing from one, and os.fork, which takes every registered
   lock, never leaves its child an enumeration half done. */

#include "prim/prim.h"

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <string.h>

enum kind { PW_NAME, PW_UID, PW_NEXT, GR_NAME, GR_GID, GR_NEXT };

/* An entry, whose strings the reentrant call wrote into buf. */
struct record {
    struct record *next;
    union {
        struct passwd pw;
        struct group gr;
    };
    char buf[];
};

/* The lock of the enumerations, registered at the first. */
static struct {
    struct lu_lock lock;
    pthread_once_t registered;
} enumeration = { { PTHREAD_MUTEX_INITIALIZER, NULL }, PTHREAD_ONCE_INIT };

static void
register_enumeration(void)
{
    lu_lock_register(&enumeration.lock);
}

/* The entry of `kind` for `name` or `id`, or the enumeration's next. NULL for
   none, with errno ENOMEM when memory ran out and 0 otherwise. A buffer too
   small is doubled and the call made again: an enumeration keeps its place on
   ERANGE (glibc's nss_files rereads the line). */
static struct record *
lookup(enum kind kind, const char *name, long id)
{
    for (size_t size = 1024;; size *= 2) {
        struct record *r = malloc(sizeof *r + size);
        if (r == NULL) {
            errno = ENOMEM;
            return NULL;
        }
        struct passwd *pw = NULL;
        struct group *gr = NULL;
        int err;
        switch (kind) {
        case PW_NAME: err = getpwnam_r(name, &r->pw, r->buf, size, &pw); break;
        case PW_UID: err = getpwuid_r((uid_t)id, &r->pw, r->buf, size, &pw); break;
        case PW_NEXT: err = getpwent_r(&r->pw, r->buf, size, &pw); break;
        case GR_NAME: err = getgrnam_r(name, &r->gr, r->buf, size, &gr); break;
        case GR_GID: err = getgrgid_r((gid_t)id, &r->gr, r->buf, size, &gr); break;
        default: err = getgrent_r(&r->gr, r->buf, size, &gr); break;
        }
        if (pw != NULL || gr != NULL) {
            return r;
        }
        free(r);
        if (err != ERANGE) {
            errno = err == ENOMEM ? ENOMEM : 0;
            return NULL;
        }
    }
}

static void
free_records(struct record *r)
{
    while (r != NULL) {
        struct record *next = r->next;
        free(r);
        r = next;
    }
}

/* Every entry of the database (PW_NEXT or GR_NEXT), in libc's order, into *out;
   -1 when memory ran out. */
static int
enumerate(enum kind kind, struct record **out)
{
    pthread_once(&enumeration.registered, register_enumeration);
    PyThreadState *ts = lu_syscall_begin();
    pthread_mutex_lock(&enumeration.lock.mutex);
    kind == PW_NEXT ? setpwent() : setgrent();
    struct record *head = NULL, **tail = &head;
    while ((*tail = lookup(kind, NULL, 0)) != NULL) {
        tail = &(*tail)->next;
    }
    int failed = errno == ENOMEM;
    kind == PW_NEXT ? endpwent() : endgrent();
    pthread_mutex_unlock(&enumeration.lock.mutex);
    lu_syscall_end(ts);
    if (failed) {
        free_records(head);
        return -1;
    }
    *out = head;
    return 0;
}

static PyObject *
text(const char *s)
{
    return PyUnicode_DecodeUTF8(s, (Py_ssize_t)strlen(s), NULL);
}

/* (name, passwd, uid, gid, gecos, dir, shell) */
static PyObject *
passwd_tuple(const struct passwd *p)
{
    PyObject *name = text(p->pw_name), *pw = text(p->pw_passwd), *gecos = text(p->pw_gecos);
    PyObject *dir = text(p->pw_dir), *shell = text(p->pw_shell);
    PyObject *r = name && pw && gecos && dir && shell
        ? Py_BuildValue("(OOllOOO)", name, pw, (long)p->pw_uid, (long)p->pw_gid, gecos, dir, shell) : NULL;
    Py_XDECREF(name); Py_XDECREF(pw); Py_XDECREF(gecos); Py_XDECREF(dir); Py_XDECREF(shell);
    return r;
}

/* (name, passwd, gid, members) */
static PyObject *
group_tuple(const struct group *g)
{
    PyObject *name = text(g->gr_name), *pw = text(g->gr_passwd), *members = PyList_New(0);
    for (char **m = g->gr_mem; members != NULL && *m != NULL; m++) {
        PyObject *s = text(*m);
        if (s == NULL || PyList_Append(members, s) < 0) {
            Py_CLEAR(members);
        }
        Py_XDECREF(s);
    }
    PyObject *r = name && pw && members ? Py_BuildValue("(OOlO)", name, pw, (long)g->gr_gid, members) : NULL;
    Py_XDECREF(name); Py_XDECREF(pw); Py_XDECREF(members);
    return r;
}

static PyObject *
record_tuple(enum kind kind, const struct record *r)
{
    return kind <= PW_NEXT ? passwd_tuple(&r->pw) : group_tuple(&r->gr);
}

static PyObject *
find(enum kind kind, const char *name, long id)
{
    PyThreadState *ts = lu_syscall_begin();
    struct record *r = lookup(kind, name, id);
    lu_syscall_end(ts);
    if (r == NULL) {
        return errno == ENOMEM ? PyErr_NoMemory() : Py_NewRef(Py_None);
    }
    PyObject *t = record_tuple(kind, r);
    free(r);
    return t;
}

static PyObject *
find_all(enum kind kind)
{
    struct record *records;
    if (enumerate(kind, &records) < 0) {
        return PyErr_NoMemory();
    }
    PyObject *list = PyList_New(0);
    for (struct record *r = records; list != NULL && r != NULL; r = r->next) {
        PyObject *t = record_tuple(kind, r);
        if (t == NULL || PyList_Append(list, t) < 0) {
            Py_CLEAR(list);
        }
        Py_XDECREF(t);
    }
    free_records(records);
    return list;
}

static PyObject *
prim_pwd_name(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    const char *name = nargs == 1 ? PyUnicode_AsUTF8(args[0]) : NULL;
    if (name == NULL) {
        return PyErr_Occurred() ? NULL : PyErr_Format(PyExc_TypeError, "pwd_name(name)");
    }
    return find(PW_NAME, name, 0);
}

static PyObject *
prim_pwd_uid(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long uid = nargs == 1 ? PyLong_AsLong(args[0]) : -1;
    if (uid == -1 && PyErr_Occurred()) {
        return NULL;
    }
    return find(PW_UID, NULL, uid);
}

static PyObject *
prim_pwd_all(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    return find_all(PW_NEXT);
}

static PyObject *
prim_grp_name(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    const char *name = nargs == 1 ? PyUnicode_AsUTF8(args[0]) : NULL;
    if (name == NULL) {
        return PyErr_Occurred() ? NULL : PyErr_Format(PyExc_TypeError, "grp_name(name)");
    }
    return find(GR_NAME, name, 0);
}

static PyObject *
prim_grp_gid(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long gid = nargs == 1 ? PyLong_AsLong(args[0]) : -1;
    if (gid == -1 && PyErr_Occurred()) {
        return NULL;
    }
    return find(GR_GID, NULL, gid);
}

static PyObject *
prim_grp_all(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    return find_all(GR_NEXT);
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_passwd_methods[] = {
    FAST(pwd_name), FAST(pwd_uid), FAST(pwd_all), FAST(grp_name), FAST(grp_gid), FAST(grp_all),
    { NULL, NULL, 0, NULL },
};
