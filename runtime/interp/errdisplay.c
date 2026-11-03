/* The native renderer of an exception nothing caught (Python/pythonrun.c
   _PyErr_Display, Lib/traceback.py TracebackException.format): the chain of
   causes and contexts, each traceback, the exception line and its notes, and
   exception groups as a tree. It prints frame lines without source text. It
   runs only where traceback._print_exception_bltin cannot, the fallback
   CPython also keeps; lu_err_display, last in this file, tries traceback
   first. */

#include "interp/exceptions.h"
#include "interp/frame.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECURSIVE_CUTOFF 3
#define MAX_GROUP_WIDTH 15
#define MAX_GROUP_DEPTH 10

struct out {
    char *p;
    size_t n, cap;
};

static void
put(struct out *o, const char *s, size_t n)
{
    if (o->n + n > o->cap) {
        o->cap = (o->n + n) * 2;
        o->p = realloc(o->p, o->cap);
        if (o->p == NULL) {
            abort();
        }
    }
    memcpy(o->p + o->n, s, n);
    o->n += n;
}

static void
puts_(struct out *o, const char *s)
{
    put(o, s, strlen(s));
}

static void
put_int(struct out *o, long v)
{
    char b[32];
    put(o, b, (size_t)snprintf(b, sizeof b, "%ld", v));
}

/* A node of the traceback exception tree (Lib/traceback.py
   TracebackException): the exception and what hangs off it. */
struct node {
    PyObject *exc;
    struct node *cause, *context;
    struct node **excs;         /* the members of a group */
    Py_ssize_t nexcs;
};

struct tree {
    PyObject **seen;            /* exceptions with a node */
    size_t nseen, capseen;
    struct node **pending;      /* nodes whose links are still to build */
    size_t npending, cappending;
};

static int
is_group(PyObject *exc)
{
    return PyType_IsSubtype(Py_TYPE(exc), (PyTypeObject *)PyExc_BaseExceptionGroup);
}

static PyObject *
group_members(PyObject *exc)
{
    return ((PyBaseExceptionGroupObject *)exc)->excs;
}

static int
seen(struct tree *t, PyObject *exc)
{
    for (size_t i = 0; i < t->nseen; i++) {
        if (t->seen[i] == exc) {
            return 1;
        }
    }
    return 0;
}

static struct node *
node_new(struct tree *t, PyObject *exc)
{
    if (t->nseen == t->capseen) {
        t->capseen = t->capseen ? t->capseen * 2 : 8;
        t->seen = realloc(t->seen, t->capseen * sizeof *t->seen);
    }
    t->seen[t->nseen++] = exc;
    struct node *n = calloc(1, sizeof *n);
    n->exc = exc;
    return n;
}

static void
push_pending(struct tree *t, struct node *n)
{
    if (t->npending == t->cappending) {
        t->cappending = t->cappending ? t->cappending * 2 : 8;
        t->pending = realloc(t->pending, t->cappending * sizeof *t->pending);
    }
    t->pending[t->npending++] = n;
}

/* The links of the chain, built the order TracebackException builds them: a
   node's cause, then its context, then its members, the last built first
   taken up. An exception met twice appears once. */
static struct node *
build(PyObject *exc)
{
    struct tree t = { 0 };
    struct node *root = node_new(&t, exc);
    push_pending(&t, root);
    while (t.npending > 0) {
        struct node *n = t.pending[--t.npending];
        PyBaseExceptionObject *e = (PyBaseExceptionObject *)n->exc;
        if (e->cause != NULL && !seen(&t, e->cause)) {
            n->cause = node_new(&t, e->cause);
        }
        if (n->cause == NULL && !e->suppress_context && e->context != NULL
            && !seen(&t, e->context)) {
            n->context = node_new(&t, e->context);
        }
        if (is_group(n->exc)) {
            PyObject *members = group_members(n->exc);
            n->nexcs = PyTuple_GET_SIZE(members);
            n->excs = calloc((size_t)n->nexcs, sizeof *n->excs);
            for (Py_ssize_t i = 0; i < n->nexcs; i++) {
                n->excs[i] = node_new(&t, PyTuple_GET_ITEM(members, i));
            }
        }
        if (n->cause != NULL) {
            push_pending(&t, n->cause);
        }
        if (n->context != NULL) {
            push_pending(&t, n->context);
        }
        for (Py_ssize_t i = 0; i < n->nexcs; i++) {
            push_pending(&t, n->excs[i]);
        }
    }
    free(t.seen);
    free(t.pending);
    return root;
}

static void
node_free(struct node *n)
{
    if (n == NULL) {
        return;
    }
    node_free(n->cause);
    node_free(n->context);
    for (Py_ssize_t i = 0; i < n->nexcs; i++) {
        node_free(n->excs[i]);
    }
    free(n->excs);
    free(n);
}

/* Text out: the margin of the exception group levels (_ExceptionPrintContext). */
struct ctx {
    struct out *out;
    int depth;
    int need_close;
};

static void
indent(struct ctx *c)
{
    for (int i = 0; i < 2 * c->depth; i++) {
        puts_(c->out, " ");
    }
}

/* ctx.emit: every line of `text` behind the margin. */
static void
emit(struct ctx *c, const char *text, size_t n, char margin)
{
    int at_start = 1;
    for (size_t i = 0; i < n; i++) {
        if (at_start) {
            indent(c);
            if (c->depth) {
                put(c->out, margin ? &margin : "|", 1);
                puts_(c->out, " ");
            }
            at_start = 0;
        }
        put(c->out, text + i, 1);
        at_start = text[i] == '\n';
    }
}

static void
emit_s(struct ctx *c, const char *text, char margin)
{
    emit(c, text, strlen(text), margin);
}

/* The text of a str, or NULL. */
static const char *
utf8(PyObject *s, Py_ssize_t *n)
{
    return s != NULL && PyUnicode_Check(s) ? PyUnicode_AsUTF8AndSize(s, n) : NULL;
}

/* str(value) through its type's __str__; NULL when that fails. */
static PyObject *
call_str(PyObject *value)
{
    if (PyUnicode_CheckExact(value)) {
        return Py_NewRef(value);
    }
    PyObject *fn = _PyType_Lookup(Py_TYPE(value), LU_ID(__str__));
    PyObject *s = fn != NULL ? lu_call(fn, &value, 1, NULL) : NULL;
    if (s != NULL && !PyUnicode_Check(s)) {
        Py_CLEAR(s);
    }
    PyErr_Clear();
    return s;
}

/* An attribute, or NULL (a missing one is no error here). */
static PyObject *
attr(PyObject *o, const char *name)
{
    PyObject *v = PyObject_GetAttrString(o, name);
    if (v == NULL) {
        PyErr_Clear();
    }
    return v;
}

/* The traceback's frame lines (StackSummary.format), the repeated ones
   summarised. */
static void
repeated(struct ctx *c, long count)
{
    char b[96];
    count -= RECURSIVE_CUTOFF;
    snprintf(b, sizeof b, "  [Previous line repeated %ld more time%s]\n", count,
             count > 1 ? "s" : "");
    emit_s(c, b, 0);
}

static void
format_stack(struct ctx *c, PyObject *tb)
{
    PyObject *last_file = NULL, *last_name = NULL;
    long last_line = -1, count = 0;
    for (; tb != NULL; tb = (PyObject *)((PyTracebackObject *)tb)->tb_next) {
        PyTracebackObject *t = (PyTracebackObject *)tb;
        PyCodeObject *co = lu_frame_code(t->tb_frame);
        long line = PyCode_Addr2Line(co, t->tb_lasti);
        if (last_file == NULL || !lu_str_eq(last_file, co->co_filename) || last_line != line
            || !lu_str_eq(last_name, co->co_name)) {
            if (count > RECURSIVE_CUTOFF) {
                repeated(c, count);
            }
            last_file = co->co_filename;
            last_name = co->co_name;
            last_line = line;
            count = 0;
        }
        if (++count > RECURSIVE_CUTOFF) {
            continue;
        }
        struct out row = { 0 };
        puts_(&row, "  File \"");
        puts_(&row, PyUnicode_AsUTF8(co->co_filename));
        puts_(&row, "\", line ");
        put_int(&row, line);
        puts_(&row, ", in ");
        puts_(&row, PyUnicode_AsUTF8(co->co_name));
        puts_(&row, "\n");
        emit(c, row.p, row.n, 0);
        free(row.p);
    }
    if (count > RECURSIVE_CUTOFF) {
        repeated(c, count);
    }
}

/* "module.QualName" as the exception line names the type
   (TracebackException.exc_type_str). */
static void
put_type_name(struct out *o, PyObject *type)
{
    PyObject *qualname = attr(type, "__qualname__");
    PyObject *module = attr(type, "__module__");
    Py_ssize_t n;
    const char *m = utf8(module, &n);
    if (module != NULL && (m == NULL || (strcmp(m, "__main__") && strcmp(m, "builtins")))) {
        puts_(o, m != NULL ? m : "<unknown>");
        puts_(o, ".");
    }
    const char *q = utf8(qualname, &n);
    puts_(o, q != NULL ? q : ((PyTypeObject *)type)->tp_name);
    Py_XDECREF(qualname);
    Py_XDECREF(module);
}

/* str(value), or the placeholder CPython prints when that raises. */
static void
put_safe_str(struct out *o, PyObject *value, const char *what)
{
    PyObject *s = call_str(value);
    const char *text = utf8(s, &(Py_ssize_t){ 0 });
    if (text != NULL) {
        puts_(o, text);
    }
    else {
        puts_(o, "<");
        puts_(o, what);
        puts_(o, " str() failed>");
    }
    Py_XDECREF(s);
}

/* A SyntaxError's location and source line (_format_syntax_error). */
static void
format_syntax_error(struct ctx *c, PyObject *exc, PyObject *stype_obj)
{
    struct out line = { 0 };
    PyObject *filename = attr(exc, "filename"), *lineno = attr(exc, "lineno");
    PyObject *text = attr(exc, "text"), *offset = attr(exc, "offset");
    PyObject *end_lineno = attr(exc, "end_lineno"), *end_offset = attr(exc, "end_offset");
    PyObject *msg = attr(exc, "msg");
    Py_ssize_t n;
    long ln = -1, off = 0, end_off = 0;
    int has_line = lineno != NULL && lineno != Py_None && lu_long_as_long(lineno, &ln) == 0;
    const char *fname = utf8(filename, &n);
    if (has_line) {
        puts_(&line, "  File \"");
        puts_(&line, fname != NULL && *fname ? fname : "<string>");
        puts_(&line, "\", line ");
        put_int(&line, ln);
        puts_(&line, "\n");
    }
    const char *t = utf8(text, &n);
    if (t != NULL) {
        size_t len = strlen(t);
        while (len > 0 && t[len - 1] == '\n') {
            len--;
        }
        size_t spaces = 0;
        while (spaces < len && (t[spaces] == ' ' || t[spaces] == '\n' || t[spaces] == '\f')) {
            spaces++;
        }
        puts_(&line, "    ");
        put(&line, t + spaces, len - spaces);
        puts_(&line, "\n");
        if (offset != NULL && PyLong_Check(offset) && lu_long_as_long(offset, &off) == 0) {
            long same = end_lineno != NULL && lineno != NULL
                && (end_lineno == lineno || (PyLong_Check(end_lineno) && PyLong_Check(lineno)
                    && lu_eq(end_lineno, lineno)));
            end_off = off;
            if (same) {
                long e;
                if (end_offset != NULL && PyLong_Check(end_offset)
                    && lu_long_as_long(end_offset, &e) == 0 && e != 0) {
                    end_off = e;
                }
            }
            else {
                end_off = (long)len + 1;
            }
            if (off > (long)strlen(t)) {
                off = (long)len + 1;
            }
            if (end_off > (long)strlen(t)) {
                end_off = (long)len + 1;
            }
            if (off >= end_off || end_off < 0) {
                end_off = off + 1;
            }
            long colno = off - 1 - (long)spaces, end_colno = end_off - 1 - (long)spaces;
            if (colno >= 0) {
                puts_(&line, "    ");
                for (long i = 0; i < colno; i++) {
                    char ch = t[spaces + i];
                    put(&line, ch == '\t' ? "\t" : " ", 1);
                }
                for (long i = colno; i < end_colno; i++) {
                    puts_(&line, "^");
                }
                puts_(&line, "\n");
            }
        }
    }
    Py_ssize_t mn;
    const char *m = utf8(msg, &mn);
    put_type_name(&line, stype_obj);
    puts_(&line, ": ");
    puts_(&line, m != NULL && *m ? m : "<no detail available>");
    if (!has_line && fname != NULL) {
        puts_(&line, " (");
        puts_(&line, fname);
        puts_(&line, ")");
    }
    puts_(&line, "\n");
    emit(c, line.p, line.n, 0);
    free(line.p);
    Py_XDECREF(filename);
    Py_XDECREF(lineno);
    Py_XDECREF(text);
    Py_XDECREF(offset);
    Py_XDECREF(end_lineno);
    Py_XDECREF(end_offset);
    Py_XDECREF(msg);
}

/* The exception line and its notes (format_exception_only). */
static void
format_exception_only(struct ctx *c, PyObject *exc)
{
    if (PyType_IsSubtype(Py_TYPE(exc), (PyTypeObject *)PyExc_SyntaxError)) {
        format_syntax_error(c, exc, (PyObject *)Py_TYPE(exc));
    }
    else {
        struct out line = { 0 };
        PyObject *s = call_str(exc);
        const char *text = utf8(s, &(Py_ssize_t){ 0 });
        put_type_name(&line, (PyObject *)Py_TYPE(exc));
        if (text == NULL) {
            puts_(&line, ": <exception str() failed>");
        }
        else if (*text) {
            puts_(&line, ": ");
            puts_(&line, text);
        }
        puts_(&line, "\n");
        emit(c, line.p, line.n, 0);
        free(line.p);
        Py_XDECREF(s);
    }
    PyObject *notes = attr(exc, "__notes__");
    if (notes == NULL) {
        return;
    }
    if ((PyList_Check(notes) || PyTuple_Check(notes))) {
        for (Py_ssize_t i = 0; i < Py_SIZE(notes); i++) {
            PyObject *note = PyList_Check(notes) ? PyList_GET_ITEM(notes, i) : PyTuple_GET_ITEM(notes, i);
            struct out line = { 0 };
            put_safe_str(&line, note, "note");
            puts_(&line, "\n");
            emit(c, line.p, line.n, 0);
            free(line.p);
        }
    }
    else {
        struct out line = { 0 };
        PyObject *repr_fn = _PyType_Lookup(Py_TYPE(notes), LU_ID(__repr__));
        PyObject *r = repr_fn != NULL ? lu_call(repr_fn, &notes, 1, NULL) : NULL;
        PyErr_Clear();
        const char *text = utf8(r, &(Py_ssize_t){ 0 });
        puts_(&line, text != NULL ? text : "<__notes__ repr() failed>");
        puts_(&line, "\n");
        emit(c, line.p, line.n, 0);
        free(line.p);
        Py_XDECREF(r);
    }
    Py_DECREF(notes);
}

static void format_node(struct ctx *c, struct node *n);

/* TracebackException.format: the chain oldest first, each link's own block. */
static void
format_node(struct ctx *c, struct node *top)
{
    struct node *chain[64];
    const char *msgs[64];
    int n = 0;
    for (struct node *e = top; e != NULL && n < 64; ) {
        struct node *next = NULL;
        const char *msg = NULL;
        if (e->cause != NULL) {
            next = e->cause;
            msg = "\nThe above exception was the direct cause of the following exception:\n\n";
        }
        else if (e->context != NULL && !((PyBaseExceptionObject *)e->exc)->suppress_context) {
            next = e->context;
            msg = "\nDuring handling of the above exception, another exception occurred:\n\n";
        }
        chain[n] = e;
        msgs[n++] = msg;
        e = next;
    }
    for (int i = n - 1; i >= 0; i--) {
        struct node *e = chain[i];
        if (msgs[i] != NULL) {
            emit_s(c, msgs[i], 0);
        }
        PyObject *tb = ((PyBaseExceptionObject *)e->exc)->traceback;
        if (e->excs == NULL) {
            if (tb != NULL) {
                emit_s(c, "Traceback (most recent call last):\n", 0);
                format_stack(c, tb);
            }
            format_exception_only(c, e->exc);
        }
        else if (c->depth > MAX_GROUP_DEPTH) {
            emit_s(c, "... (max_group_depth is 10)\n", 0);
        }
        else {
            int toplevel = c->depth == 0;
            if (toplevel) {
                c->depth++;
            }
            if (tb != NULL) {
                emit_s(c, "Exception Group Traceback (most recent call last):\n", toplevel ? '+' : 0);
                format_stack(c, tb);
            }
            format_exception_only(c, e->exc);
            Py_ssize_t shown = e->nexcs <= MAX_GROUP_WIDTH ? e->nexcs : MAX_GROUP_WIDTH + 1;
            c->need_close = 0;
            for (Py_ssize_t k = 0; k < shown; k++) {
                int last = k == shown - 1;
                if (last) {
                    c->need_close = 1;
                }
                int truncated = k >= MAX_GROUP_WIDTH;
                indent(c);
                puts_(c->out, k == 0 ? "+-" : "  ");
                puts_(c->out, "+---------------- ");
                if (truncated) {
                    puts_(c->out, "...");
                }
                else {
                    put_int(c->out, (long)k + 1);
                }
                puts_(c->out, " ----------------\n");
                c->depth++;
                if (!truncated) {
                    format_node(c, e->excs[k]);
                }
                else {
                    char b[64];
                    long remaining = e->nexcs - MAX_GROUP_WIDTH;
                    snprintf(b, sizeof b, "and %ld more exception%s\n", remaining,
                             remaining > 1 ? "s" : "");
                    emit_s(c, b, 0);
                }
                if (last && c->need_close) {
                    indent(c);
                    puts_(c->out, "+------------------------------------\n");
                    c->need_close = 0;
                }
                c->depth--;
            }
            if (toplevel) {
                c->depth = 0;
            }
        }
    }
}

/* Text for the error output: what sys.stderr's write takes, else (before
   sys.stderr exists, or when it cannot be written to) the C stderr. A
   sys.stderr of None swallows it, as it does in PyErr_Display. */
void
lu_stderr_write(const char *text, size_t n)
{
    PyObject *file = lu_ns_lookup(_PyRuntime.main_interp.sysdict, PyUnicode_InternFromString("stderr"));
    if (file == Py_None) {
        return;
    }
    if (file != NULL) {
        PyObject *exc = PyErr_GetRaisedException();
        PyObject *str = PyUnicode_FromStringAndSize(text, (Py_ssize_t)n);
        PyObject *write = str != NULL ? PyObject_GetAttrString(file, "write") : NULL;
        PyObject *result = write != NULL ? PyObject_CallOneArg(write, str) : NULL;
        Py_XDECREF(str);
        Py_XDECREF(write);
        int done = result != NULL;
        Py_XDECREF(result);
        PyErr_Clear();
        PyErr_SetRaisedException(exc);
        if (done) {
            return;
        }
    }
    fwrite(text, 1, n, stderr);
}

void
lu_err_unraisable(const char *what, PyObject *obj)
{
    char message[128];
    snprintf(message, sizeof message, "Exception ignored while %s", what);
    lu_err_unraisable_text(message, obj);
}

/* PyErr_FormatUnraisable: "message repr(obj):" then the exception being
   handled, which is cleared. */
void
lu_err_unraisable_text(const char *message, PyObject *obj)
{
    PyObject *exc = PyErr_GetRaisedException();
    PyObject *r = obj != NULL ? lu_call_kernel(LU_K_repr, &obj, 1) : NULL;
    const char *text = r != NULL && PyUnicode_Check(r) ? PyUnicode_AsUTF8(r) : NULL;
    PyErr_Clear();
    const char *repr = text != NULL ? text : "<object repr() failed>";
    int n = obj == NULL ? snprintf(NULL, 0, "%s:\n", message)
                        : snprintf(NULL, 0, "%s %s:\n", message, repr);
    char *head = malloc((size_t)n + 1);
    if (head != NULL) {
        if (obj == NULL) {
            snprintf(head, (size_t)n + 1, "%s:\n", message);
        }
        else {
            snprintf(head, (size_t)n + 1, "%s %s:\n", message, repr);
        }
        lu_stderr_write(head, (size_t)n);
        free(head);
    }
    Py_XDECREF(r);
    if (exc != NULL && ((PyBaseExceptionObject *)exc)->traceback == NULL) {
        /* format_unraisable_v: a traceback of the frame running now */
        struct lu_frame *f = lu_frame_user((struct lu_frame *)PyThreadState_Get()->current_frame, 0);
        if (f != NULL) {
            PyErr_SetRaisedException(exc);
            lu_traceback_here(f, lu_frame_lasti(f));
            exc = PyErr_GetRaisedException();
        }
    }
    if (exc != NULL) {
        lu_err_display(exc);
        Py_DECREF(exc);
    }
}

static void
display_native(PyObject *exc)
{
    struct out out = { 0 };
    struct ctx c = { .out = &out };
    struct node *root = build(exc);
    format_node(&c, root);
    node_free(root);
    lu_stderr_write(out.p, out.n);
    free(out.p);
}

/* _PyErr_Display: traceback._print_exception_bltin prints the exception, source
   lines and position markers included. Where the traceback module cannot be
   imported or fails, the native renderer above stands in. */
void
lu_err_display(PyObject *exc)
{
    struct _is *interp = &_PyRuntime.main_interp;
    if (lu_ns_lookup(interp->sysdict, PyUnicode_InternFromString("stderr")) == Py_None) {
        return;                         /* no stream to print on */
    }
    PyObject *name = PyUnicode_InternFromString("traceback");
    PyObject *module = name != NULL && interp->import_func != NULL
        ? lu_call(interp->import_func, &name, 1, NULL) : NULL;
    PyObject *print = module != NULL ? PyObject_GetAttrString(module, "_print_exception_bltin") : NULL;
    PyObject *result = print != NULL ? lu_call(print, &exc, 1, NULL) : NULL;
    if (result == NULL) {
        PyErr_Clear();
        display_native(exc);
    }
    Py_XDECREF(result);
    Py_XDECREF(print);
    Py_XDECREF(module);
    Py_XDECREF(name);
}
