/* The code image (image.h). build/image.bin is linked into the executable
   read-only; each Py_Initialize maps fresh private memory for its object
   area and copies and relocates segment 0 into it, and each other segment
   when its code is first asked for, so an interpreter starts from untouched
   code whatever the last one did, a process copies only the modules it
   runs, and a fork inherits the copy like any other memory. Earlier copies
   stay mapped: what the last interpreter made is never freed
   (release_interpreter) and may still point into them.

   Image objects are immortal and outside the collector's heaps, so it never
   visits them, as CPython's static objects. Their str, bytes and tuple
   hashes start unset and are computed with the process's secret on first
   use, as for any other object. */

#include "interp/image.h"
#include "interp/specialize.h"
#include "object/set.h"

#include "marshal.h"

#include <sys/mman.h>

/* build/image.o, made from runtime/image.S. imagegen links without it, and
   unmarshals. */
extern const unsigned char lu_image_data[] __attribute__((weak, visibility("hidden")));

struct lu_image {
    struct lu_image *prev;      /* the image an earlier interpreter mapped */
    char *base;                 /* the object area's copy, its segments copied as needed */
    const char *area;           /* the original, in the executable */
    const uint64_t *bitmap, *roots, *segs, *codes, *frozensets;
    uint64_t nsegs, nfrozensets;
    const uint32_t *intern;
    uint64_t intern_mask;
    PyMutex lock;               /* one segment copy at a time */
    struct lu_image_extern ext[LU_IMAGE_MAX_EXTERNS];
    uint8_t loaded[];           /* per segment, set with release once copied */
};

size_t
lu_image_externs(struct lu_image_extern *out)
{
    size_t n = 0;
#define EXTERN(p, size) out[n++] = (struct lu_image_extern){ (p), (size) }
    EXTERN(&_PyRuntime, sizeof _PyRuntime);         /* small ints, Latin-1 strs, empty objects */
    EXTERN(&_Py_NoneStruct, sizeof(PyObject));
    EXTERN(&_Py_EllipsisObject, sizeof(PyObject));
    EXTERN(&_Py_TrueStruct, sizeof(PyLongObject));
    EXTERN(&_Py_FalseStruct, sizeof(PyLongObject));
    EXTERN(PyExc_StopIteration, sizeof(PyTypeObject));
    PyTypeObject *const types[] = {
        &PyCode_Type, &PyUnicode_Type, &PyBytes_Type, &PyTuple_Type, &PyLong_Type,
        &PyFloat_Type, &PyComplex_Type, &PySlice_Type,
    };
    for (size_t i = 0; i < sizeof types / sizeof *types; i++) {
        EXTERN(types[i], sizeof(PyTypeObject));
    }
#undef EXTERN
    return n;
}

uint64_t
lu_image_str_hash(PyObject *s)
{
    const unsigned char *p = PyUnicode_DATA(s);
    size_t n = (size_t)PyUnicode_GET_LENGTH(s) * PyUnicode_KIND(s);
    uint64_t h = 0xcbf29ce484222325ULL ^ (uint64_t)PyUnicode_KIND(s);
    for (size_t i = 0; i < n; i++) {
        h = (h ^ p[i]) * 0x100000001b3ULL;
    }
    return h;
}

/* Copies segment k, relocates its pointers and makes its frozensets: a
   frozenset's table holds its items' hashes, which take the secret. The
   original's pages are dropped from the process, which reads them once. */
static int
load(struct lu_image *img, uint64_t k)
{
    uint64_t start = img->segs[4 * k], end = img->segs[4 * k + 2];
    memcpy(img->base + start, img->area + start, end - start);
    uint64_t *words = (uint64_t *)img->base;
    for (uint64_t w = start / 512; w < (end + 511) / 512; w++) {
        for (uint64_t bits = img->bitmap[w]; bits != 0; bits &= bits - 1) {
            uint64_t *slot = &words[w * 64 + (uint64_t)__builtin_ctzll(bits)];
            uint64_t v = *slot;
            *slot = (v & 1) ? (uintptr_t)img->ext[v >> 33].start + ((v >> 1) & 0xffffffffu)
                            : (uintptr_t)img->base + v;
        }
    }
    for (uint64_t i = 0; i < img->nfrozensets; i++) {
        uint64_t slot = img->frozensets[2 * i];
        if (slot < start || slot >= end) {
            continue;
        }
        PyObject *items = (PyObject *)(img->base + img->frozensets[2 * i + 1]);
        PyObject *set = lu_set_new(&PyFrozenSet_Type);
        for (Py_ssize_t j = 0; set != NULL && j < PyTuple_GET_SIZE(items); j++) {
            if (lu_set_add(set, PyTuple_GET_ITEM(items, j)) < 0) {
                Py_CLEAR(set);
            }
        }
        if (set == NULL) {
            return -1;
        }
        lu_set_immortal(set);
        *(PyObject **)(img->base + slot) = set;
    }
    madvise((void *)(img->area + start), (end - start + 4095) & ~(uint64_t)4095, MADV_DONTNEED);
    __atomic_store_n(&img->loaded[k], 1, __ATOMIC_RELEASE);
    return 0;
}

int
lu_image_map(PyInterpreterState *interp)
{
    interp->next_func_version = 2;              /* FUNC_VERSION_FIRST_VALID */
    const struct lu_image_header *h = (const struct lu_image_header *)lu_image_data;
    if (h == NULL) {
        return 0;
    }
    if (h->magic != LU_IMAGE_MAGIC) {
        lu_err_format(PyExc_SystemError, "the code image is damaged");
        return -1;
    }
    uint64_t nsegs = h->nroots + 1;
    struct lu_image *img = PyMem_RawCalloc(1, sizeof *img + nsegs);
    char *base = mmap(NULL, h->area_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (img == NULL || base == MAP_FAILED) {
        PyMem_RawFree(img);
        PyErr_NoMemory();
        return -1;
    }
    img->base = base;
    img->area = (const char *)lu_image_data + LU_IMAGE_AREA;
    img->bitmap = (const uint64_t *)(img->area + h->area_size);
    img->roots = img->bitmap + (h->area_size / 8 + 63) / 64;
    img->segs = img->roots + h->nroots;
    img->nsegs = nsegs;
    img->codes = img->segs + 4 * nsegs;
    img->frozensets = img->codes + h->ncodes;
    img->nfrozensets = h->nfrozensets;
    img->intern = (const uint32_t *)(img->frozensets + 2 * h->nfrozensets);
    img->intern_mask = h->intern_size - 1;
    lu_image_externs(img->ext);
    if (load(img, 0) < 0) {
        return -1;
    }
    img->prev = interp->image;
    interp->image = img;
    interp->next_func_version = 2 + (uint32_t)h->ncodes;
    return 0;
}

PyObject *
lu_frozen_code(const struct lu_frozen *m)
{
    struct lu_image *img = PyInterpreterState_Get()->image;
    if (img == NULL) {
        return PyMarshal_ReadObjectFromString((const char *)m->data, m->size);
    }
    uint64_t k = (uint64_t)m->index + 1;
    if (!__atomic_load_n(&img->loaded[k], __ATOMIC_ACQUIRE)) {
        PyMutex_Lock(&img->lock);
        int err = !img->loaded[k] && load(img, k) < 0;
        PyMutex_Unlock(&img->lock);
        if (err) {
            return NULL;
        }
    }
    return (PyObject *)(img->base + img->roots[m->index]);
}

PyObject *
lu_image_interned(PyInterpreterState *interp, PyObject *s)
{
    struct lu_image *img = interp->image;
    if (img == NULL) {
        return NULL;
    }
    for (uint64_t i = lu_image_str_hash(s) & img->intern_mask; img->intern[i] != 0; i = (i + 1) & img->intern_mask) {
        PyObject *t = (PyObject *)(img->base + (uint64_t)(img->intern[i] - 1) * 16);
        if (lu_str_eq(t, s)) {
            return t;
        }
    }
    return NULL;
}

void
lu_image_each_code(void (*fn)(PyCodeObject *, void *), void *arg)
{
    for (struct lu_image *img = _PyRuntime.main_interp.image; img != NULL; img = img->prev) {
        for (uint64_t k = 0; k < img->nsegs; k++) {
            if (!__atomic_load_n(&img->loaded[k], __ATOMIC_ACQUIRE)) {
                continue;
            }
            for (uint64_t i = img->segs[4 * k + 1]; i < img->segs[4 * k + 3]; i++) {
                fn((PyCodeObject *)(img->base + img->codes[i]), arg);
            }
        }
    }
}
