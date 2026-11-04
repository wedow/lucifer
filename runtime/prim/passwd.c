/* The user and group databases: the _lucifer primitives behind pwd and grp.
   Each answers a tuple of the entry's fields, or None for no such entry. */

#include "prim/prim.h"

#include <grp.h>
#include <pwd.h>
#include <string.h>

static PyObject *
text(const char *s)
{
    return PyUnicode_DecodeUTF8(s, (Py_ssize_t)strlen(s), NULL);
}

/* (name, passwd, uid, gid, gecos, dir, shell) */
static PyObject *
passwd_tuple(const struct passwd *p)
{
    if (p == NULL) {
        return Py_NewRef(Py_None);
    }
    PyObject *name = text(p->pw_name), *pw = text(p->pw_passwd), *gecos = text(p->pw_gecos);
    PyObject *dir = text(p->pw_dir), *shell = text(p->pw_shell);
    PyObject *r = name && pw && gecos && dir && shell
        ? Py_BuildValue("(OOllOOO)", name, pw, (long)p->pw_uid, (long)p->pw_gid, gecos, dir, shell) : NULL;
    Py_XDECREF(name); Py_XDECREF(pw); Py_XDECREF(gecos); Py_XDECREF(dir); Py_XDECREF(shell);
    return r;
}

static PyObject *
prim_pwd_name(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    const char *name = nargs == 1 ? PyUnicode_AsUTF8(args[0]) : NULL;
    if (name == NULL) {
        return PyErr_Occurred() ? NULL : PyErr_Format(PyExc_TypeError, "pwd_name(name)");
    }
    return passwd_tuple(getpwnam(name));
}

static PyObject *
prim_pwd_uid(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long uid = nargs == 1 ? PyLong_AsLong(args[0]) : -1;
    if (uid == -1 && PyErr_Occurred()) {
        return NULL;
    }
    return passwd_tuple(getpwuid((uid_t)uid));
}

static PyObject *
prim_pwd_all(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *list = PyList_New(0);
    setpwent();
    for (struct passwd *p; list != NULL && (p = getpwent()) != NULL; ) {
        PyObject *t = passwd_tuple(p);
        if (t == NULL || PyList_Append(list, t) < 0) {
            Py_CLEAR(list);
        }
        Py_XDECREF(t);
    }
    endpwent();
    return list;
}

/* (name, passwd, gid, members) */
static PyObject *
group_tuple(const struct group *g)
{
    if (g == NULL) {
        return Py_NewRef(Py_None);
    }
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
prim_grp_name(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    const char *name = nargs == 1 ? PyUnicode_AsUTF8(args[0]) : NULL;
    if (name == NULL) {
        return PyErr_Occurred() ? NULL : PyErr_Format(PyExc_TypeError, "grp_name(name)");
    }
    return group_tuple(getgrnam(name));
}

static PyObject *
prim_grp_gid(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long gid = nargs == 1 ? PyLong_AsLong(args[0]) : -1;
    if (gid == -1 && PyErr_Occurred()) {
        return NULL;
    }
    return group_tuple(getgrgid((gid_t)gid));
}

static PyObject *
prim_grp_all(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *list = PyList_New(0);
    setgrent();
    for (struct group *g; list != NULL && (g = getgrent()) != NULL; ) {
        PyObject *t = group_tuple(g);
        if (t == NULL || PyList_Append(list, t) < 0) {
            Py_CLEAR(list);
        }
        Py_XDECREF(t);
    }
    endgrent();
    return list;
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_passwd_methods[] = {
    FAST(pwd_name), FAST(pwd_uid), FAST(pwd_all), FAST(grp_name), FAST(grp_gid), FAST(grp_all),
    { NULL, NULL, 0, NULL },
};
