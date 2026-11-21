/* imagegen OUTPUT: writes the code image (runtime/interp/image.h) of the
   frozen tables. A build step, linked without an image: it boots by
   unmarshalling, unmarshals every frozen entry once more into fresh code
   and lays out what that reaches. Not part of the runtime archive.

   Each frozen entry's code gets a segment of what it alone reaches, so that
   a process copies only the modules it runs; segment 0 holds the interned
   strs and whatever more than one entry reaches. */

#include "interp/image.h"
#include "interp/code.h"

#include "marshal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct vec {
    uint64_t *v;
    size_t n, cap;
};

static void
push(struct vec *a, uint64_t x)
{
    if (a->n == a->cap) {
        a->cap = a->cap ? 2 * a->cap : 1024;
        a->v = realloc(a->v, a->cap * sizeof *a->v);
        if (a->v == NULL) {
            abort();
        }
    }
    a->v[a->n++] = x;
}

static void
fail(const char *what, PyObject *op)
{
    fprintf(stderr, "imagegen: %s: %s at %p\n", what, op != NULL ? Py_TYPE(op)->tp_name : "-", (void *)op);
    exit(1);
}

/* Object -> value, open addressing on the address; 0 is no value. */
struct table {
    struct { PyObject *key; uint64_t value; } *e;
    size_t mask, n;
};

static uint64_t *
lookup(struct table *t, PyObject *op)
{
    if (2 * (t->n + 1) > t->mask) {
        struct table old = *t;
        t->mask = old.e == NULL ? 4095 : 2 * old.mask + 1;
        t->e = calloc(t->mask + 1, sizeof *t->e);
        t->n = 0;
        for (size_t i = 0; old.e != NULL && i <= old.mask; i++) {
            if (old.e[i].key != NULL) {
                *lookup(t, old.e[i].key) = old.e[i].value;
            }
        }
        free(old.e);
    }
    size_t i = ((uintptr_t)op >> 4) * 0x9e3779b97f4a7c15ULL >> 20 & t->mask;
    while (t->e[i].key != NULL && t->e[i].key != op) {
        i = (i + 1) & t->mask;
    }
    if (t->e[i].key == NULL) {
        t->e[i].key = op;
        t->n++;
    }
    return &t->e[i].value;
}

static struct {
    struct lu_image_extern ext[LU_IMAGE_MAX_EXTERNS];
    struct table owner;         /* the entry's index plus one, or SHARED */
    struct vec shared;          /* what segment 0 holds */
    char *area;
    size_t size, cap;
    struct table offset;        /* where an object's copy is */
    struct vec todo;            /* (object, offset) pairs whose fields are not yet written */
    struct vec slots;           /* offsets of the pointer words */
    struct vec codes, frozensets, interned, segs;
} w;

#define SHARED UINT64_MAX

static int
is_extern(PyObject *op)
{
    for (size_t i = 0; w.ext[i].start != NULL; i++) {
        if ((uintptr_t)op - (uintptr_t)w.ext[i].start < w.ext[i].size) {
            return 1;
        }
    }
    if (op->ob_flags & _Py_STATICALLY_ALLOCATED_FLAG) {
        fail("a static object lu_image_externs leaves out", op);
    }
    return 0;
}

/* Calls fn on each pointer field of a laid-out object but its type, with the
   field's offset. A frozenset is a leaf: it is made at load time. */
static void
each_field(PyObject *op, void (*fn)(PyObject *, size_t, void *), void *arg)
{
    if (PyTuple_CheckExact(op)) {
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(op); i++) {
            fn(PyTuple_GET_ITEM(op, i), offsetof(PyTupleObject, ob_item) + i * sizeof(PyObject *), arg);
        }
    }
    else if (PySlice_Check(op)) {
        PySliceObject *s = (PySliceObject *)op;
        fn(s->start, offsetof(PySliceObject, start), arg);
        fn(s->stop, offsetof(PySliceObject, stop), arg);
        fn(s->step, offsetof(PySliceObject, step), arg);
    }
    else if (PyCode_Check(op)) {
        PyCodeObject *co = (PyCodeObject *)op;
#define CODE_FIELD(f) fn(co->f, offsetof(PyCodeObject, f), arg)
        CODE_FIELD(co_consts);
        CODE_FIELD(co_names);
        CODE_FIELD(co_exceptiontable);
        CODE_FIELD(co_localsplusnames);
        CODE_FIELD(co_localspluskinds);
        CODE_FIELD(co_filename);
        CODE_FIELD(co_name);
        CODE_FIELD(co_qualname);
        CODE_FIELD(co_linetable);
#undef CODE_FIELD
    }
}

/* Pass 1: which entry reaches each object, or SHARED. */
static void
own(PyObject *op, size_t field, void *arg)
{
    uint64_t entry = *(uint64_t *)arg;
    if (op == NULL || is_extern(op)) {
        return;
    }
    if (PyFrozenSet_CheckExact(op)) {       /* its items, which its tuple will hold */
        PyObject *items = PySequence_Tuple(op);
        if (items == NULL) {
            fail("a frozenset's items", op);
        }
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(items); i++) {
            own(PyTuple_GET_ITEM(items, i), 0, arg);
        }
        Py_DECREF(items);
        return;
    }
    uint64_t *owner = lookup(&w.owner, op);
    if (*owner == SHARED || *owner == entry) {
        return;
    }
    if (*owner != 0 || (PyUnicode_CheckExact(op) && PyUnicode_CHECK_INTERNED(op))) {
        *owner = SHARED;
        push(&w.shared, (uintptr_t)op);
        each_field(op, own, &(uint64_t){ SHARED });
        return;
    }
    *owner = entry;
    each_field(op, own, arg);
}

static size_t
size_of(PyObject *op)
{
    PyTypeObject *tp = Py_TYPE(op);
    if (tp == &PyUnicode_Type) {
        if (!PyUnicode_IS_COMPACT(op)) {
            fail("a str that is not compact", op);
        }
        Py_ssize_t n = PyUnicode_GET_LENGTH(op) + 1;
        return PyUnicode_IS_ASCII(op) ? sizeof(PyASCIIObject) + n
                                      : sizeof(PyCompactUnicodeObject) + n * PyUnicode_KIND(op);
    }
    if (tp == &PyBytes_Type) {
        return offsetof(PyBytesObject, ob_sval) + PyBytes_GET_SIZE(op) + 1;
    }
    if (tp == &PyTuple_Type) {
        return offsetof(PyTupleObject, ob_item) + PyTuple_GET_SIZE(op) * sizeof(PyObject *);
    }
    if (tp == &PyLong_Type) {
        size_t digits = ((PyLongObject *)op)->long_value.lv_tag >> _PyLong_NON_SIZE_BITS;
        return offsetof(PyLongObject, long_value.ob_digit) + (digits ? digits : 1) * sizeof(digit);
    }
    if (tp == &PyFloat_Type) {
        return sizeof(PyFloatObject);
    }
    if (tp == &PyComplex_Type) {
        return sizeof(PyComplexObject);
    }
    if (tp == &PySlice_Type) {
        return sizeof(PySliceObject);
    }
    if (tp == &PyCode_Type) {
        return lu_code_size(Py_SIZE(op));
    }
    fail("an object of a type the image does not hold", op);
    return 0;
}

/* Pass 2: the word a pointer to op is stored as; op is copied into the
   current segment the first time. */
static uint64_t
encode(PyObject *op)
{
    for (size_t i = 0; w.ext[i].start != NULL; i++) {
        uintptr_t off = (uintptr_t)op - (uintptr_t)w.ext[i].start;
        if (off < w.ext[i].size) {
            return ((uint64_t)i << 32 | off) << 1 | 1;
        }
    }
    uint64_t *off = lookup(&w.offset, op);
    if (*off != 0) {
        return *off;
    }
    size_t size = size_of(op);
    *off = w.size;
    if (w.size + size + 4096 > w.cap) {
        w.cap = 2 * (w.size + size + 4096);
        w.area = realloc(w.area, w.cap);
        if (w.area == NULL) {
            abort();
        }
    }
    memcpy(w.area + w.size, op, size);
    push(&w.todo, (uintptr_t)op);
    push(&w.todo, w.size);
    w.size = (w.size + size + 15) & ~(size_t)15;
    return *off;
}

/* Stores a pointer to op in the word at off. A frozenset is made at load
   time from a tuple of its items: its table holds their hashes. */
static void
store(PyObject *op, size_t field, void *arg)
{
    uint64_t off = *(uint64_t *)arg + field, v = 0;
    if (op != NULL && PyFrozenSet_CheckExact(op)) {
        PyObject *items = PySequence_Tuple(op);
        if (items == NULL) {
            fail("a frozenset's items", op);
        }
        push(&w.frozensets, off);
        push(&w.frozensets, encode(items));
    }
    else if (op != NULL) {
        v = encode(op);
        push(&w.slots, off);
    }
    memcpy(w.area + off, &v, sizeof v);
}

/* Writes the fields of the copy at off of op: immortal, untracked, hashes
   unset, pointers stored, per-process state cleared. */
static void
fill(PyObject *op, uint64_t off)
{
    PyObject *copy = (PyObject *)(w.area + off);
    copy->ob_tid = 0;
    copy->ob_flags = 0;
    copy->ob_mutex = (PyMutex){ 0 };
    copy->ob_gc_bits = LU_GC_DEFERRED;
    copy->ob_ref_local = _Py_IMMORTAL_REFCNT_LOCAL;
    copy->ob_ref_shared = 0;
    store((PyObject *)Py_TYPE(op), offsetof(PyObject, ob_type), &off);
    each_field(op, store, &off);
    copy = (PyObject *)(w.area + off);          /* the stores may have moved the area */
    if (PyUnicode_CheckExact(op)) {
        PyASCIIObject *a = (PyASCIIObject *)copy;
        a->hash = -1;
        if (!PyUnicode_IS_ASCII(op)) {
            ((PyCompactUnicodeObject *)copy)->utf8 = NULL;
            ((PyCompactUnicodeObject *)copy)->utf8_length = 0;
        }
        if (a->state.interned) {
            if (w.segs.n > 2) {
                fail("an interned str outside segment 0", op);
            }
            a->state.interned = SSTATE_INTERNED_IMMORTAL;
            push(&w.interned, off);
            push(&w.interned, lu_image_str_hash(op));
        }
    }
    else if (PyBytes_CheckExact(op)) {
        Py_hash_t unset = -1;
        memcpy(w.area + off + offsetof(PyBytesObject, ob_sval) - sizeof unset, &unset, sizeof unset);
    }
    else if (PyTuple_CheckExact(op)) {
        ((PyTupleObject *)copy)->ob_hash = -1;
    }
    else if (PyCode_Check(op)) {
        PyCodeObject *c = (PyCodeObject *)copy;
        c->co_weakreflist = NULL;
        c->co_executors = NULL;
        c->_co_cached = NULL;
        c->_co_instrumentation_version = 0;
        c->_co_monitoring = NULL;
        c->_co_unique_id = -1;
        c->co_extra = NULL;
        c->co_tlbc = NULL;
        c->co_version = 2 + (uint32_t)w.codes.n;
        push(&w.codes, off);
    }
}

static void
drain(void)
{
    while (w.todo.n != 0) {
        uint64_t at = w.todo.v[--w.todo.n];
        PyObject *op = (PyObject *)(uintptr_t)w.todo.v[--w.todo.n];
        fill(op, at);
    }
}

/* Starts a segment on a page, and ends the last one. */
static void
segment(void)
{
    if (w.segs.n != 0) {
        push(&w.segs, w.size);
        push(&w.segs, w.codes.n);
    }
    w.size = (w.size + 4095) & ~(size_t)4095;
    push(&w.segs, w.size);
    push(&w.segs, w.codes.n);
}

/* Every pointer stays in its segment or goes to segment 0. */
static void
check_segments(void)
{
    size_t nsegs = w.segs.n / 4;
    for (size_t i = 0; i < w.slots.n; i++) {
        uint64_t off = w.slots.v[i], v;
        memcpy(&v, w.area + off, sizeof v);
        if (v & 1) {
            continue;
        }
        for (size_t s = 1; s < nsegs; s++) {
            const uint64_t *seg = &w.segs.v[4 * s];
            if (off >= seg[0] && off < seg[2] && !(v >= seg[0] && v < seg[2]) && v >= w.segs.v[2]) {
                fprintf(stderr, "imagegen: segment %zu points into another\n", s);
                exit(1);
            }
        }
    }
}

static void
write_all(FILE *f, const void *p, size_t n)
{
    if (n != 0 && fwrite(p, 1, n, f) != n) {
        perror("imagegen");
        exit(1);
    }
}

int
main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: imagegen OUTPUT\n");
        return 2;
    }
    /* A fixed seed: a frozenset's items keep one order from build to build. */
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    config.site_import = 0;
    config.use_hash_seed = 1;
    config.hash_seed = 0;
    PyStatus status = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);
    if (PyStatus_Exception(status)) {
        Py_ExitStatusException(status);
    }
    lu_image_externs(w.ext);
    struct vec roots = { 0 };
    const struct lu_frozen *tables[] = { lu_frozen_kernel, lu_frozen_builtin, lu_frozen_modules };
    for (size_t t = 0; t < 3; t++) {
        for (const struct lu_frozen *m = tables[t]; m->name != NULL; m++) {
            PyObject *code = PyMarshal_ReadObjectFromString((const char *)m->data, m->size);
            if (code == NULL || (size_t)m->index != roots.n) {
                fail("a frozen entry", code);
            }
            push(&roots, (uintptr_t)code);
            own(code, 0, &(uint64_t){ roots.n });
        }
    }
    w.size = 16;                /* offset 0 is no object */
    segment();
    for (size_t i = 0; i < w.shared.n; i++) {
        encode((PyObject *)(uintptr_t)w.shared.v[i]);
        drain();
    }
    for (size_t i = 0; i < roots.n; i++) {
        segment();
        roots.v[i] = encode((PyObject *)(uintptr_t)roots.v[i]);
        drain();
    }
    push(&w.segs, w.size);
    push(&w.segs, w.codes.n);
    check_segments();
    w.size = (w.size + 7) & ~(size_t)7;
    uint64_t nbitmap = (w.size / 8 + 63) / 64;
    uint64_t *bitmap = calloc(nbitmap, sizeof *bitmap);
    for (size_t i = 0; i < w.slots.n; i++) {
        bitmap[w.slots.v[i] / 8 / 64] |= 1ULL << (w.slots.v[i] / 8 % 64);
    }
    uint64_t intern_size = 8;
    while (intern_size < w.interned.n) {     /* (offset, hash) pairs: at most half full */
        intern_size *= 2;
    }
    uint32_t *intern = calloc(intern_size, sizeof *intern);
    for (size_t i = 0; i < w.interned.n; i += 2) {
        uint64_t j = w.interned.v[i + 1] & (intern_size - 1);
        while (intern[j] != 0) {
            j = (j + 1) & (intern_size - 1);
        }
        intern[j] = (uint32_t)(w.interned.v[i] / 16 + 1);
    }
    struct lu_image_header h = {
        .magic = LU_IMAGE_MAGIC, .area_size = w.size, .nroots = roots.n, .ncodes = w.codes.n,
        .nfrozensets = w.frozensets.n / 2, .intern_size = intern_size,
    };
    static const unsigned char pad[LU_IMAGE_AREA] = { 0 };
    FILE *f = fopen(argv[1], "wb");
    if (f == NULL) {
        perror(argv[1]);
        return 1;
    }
    write_all(f, &h, sizeof h);
    write_all(f, pad, LU_IMAGE_AREA - sizeof h);
    write_all(f, w.area, w.size);
    write_all(f, bitmap, nbitmap * sizeof *bitmap);
    write_all(f, roots.v, roots.n * sizeof *roots.v);
    write_all(f, w.segs.v, w.segs.n * sizeof *w.segs.v);
    write_all(f, w.codes.v, w.codes.n * sizeof *w.codes.v);
    write_all(f, w.frozensets.v, w.frozensets.n * sizeof *w.frozensets.v);
    write_all(f, intern, intern_size * sizeof *intern);
    if (fclose(f) != 0) {
        perror(argv[1]);
        return 1;
    }
    fprintf(stderr, "imagegen: %zu bytes, %zu objects (%zu shared, %zu bytes), %zu pointers, %zu code, "
            "%zu interned, %zu frozensets\n", w.size, w.offset.n, w.shared.n, (size_t)w.segs.v[2],
            w.slots.n, w.codes.n, w.interned.n / 2, w.frozensets.n / 2);
    return 0;
}
