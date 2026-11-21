/* The code image (runtime/interp/image.c): the frozen tables' code objects,
   laid out at build time by build/imagegen (runtime/imagegen.c) and copied
   into each interpreter at boot instead of being unmarshalled. */
#ifndef LU_IMAGE_H
#define LU_IMAGE_H

#include "lucifer.h"
#include "interp/frozen.h"

/* The file build/image.bin, as imagegen writes it: this header, padded to
   LU_IMAGE_AREA; the object area (offsets 16-aligned and image-relative);
   the relocation bitmap (a bit per 8-byte word of the area: the word is a
   pointer); the roots (a code object's offset per frozen entry, by
   lu_frozen.index); the segments, (start, first code, end, end of codes)
   each; the code objects' offsets; the frozenset fixups, (slot, items
   tuple) offset pairs; the intern table. A pointer word holds an offset in
   the area, or, odd, an object outside the image:
   (lu_image_extern index << 32 | byte offset) << 1 | 1.

   Segment 0 is the interned strs and what several entries reach, copied
   at boot; segment i + 1 is what entry i alone reaches, copied when its
   code is first asked for. Segments start on a page and point only into
   themselves and segment 0. */
#define LU_IMAGE_MAGIC 0x32676d69756cULL        /* "lu" "img2" */
#define LU_IMAGE_AREA 4096
struct lu_image_header {
    uint64_t magic;
    uint64_t area_size;         /* bytes; the bitmap has area_size / 512 words, rounded up */
    uint64_t nroots;            /* the segments are one more */
    uint64_t ncodes;            /* their co_version are 2 .. ncodes + 1 */
    uint64_t nfrozensets;
    uint64_t intern_size;       /* a power of two: uint32 entries, offset / 16 + 1, 0 empty */
};

/* The objects outside the image its pointers may name, by index: the static
   types and singletons. */
struct lu_image_extern {
    const void *start;
    size_t size;
};
size_t lu_image_externs(struct lu_image_extern *out);
#define LU_IMAGE_MAX_EXTERNS 32

/* The FNV-1a hash the intern table is keyed by: no secret, so that it can
   be computed at build time. */
uint64_t lu_image_str_hash(PyObject *s);

/* Copies the image into `interp`, whose interned table exists, and sets its
   next code version past the image's. Without an image (imagegen itself)
   the code comes from unmarshal. Returns -1 with an exception set. */
int lu_image_map(PyInterpreterState *interp);

/* A frozen entry's code object: the image's, or unmarshalled. New reference. */
PyObject *lu_frozen_code(const struct lu_frozen *m);

/* The interned str of the image equal to the exact str `s`, or NULL. */
PyObject *lu_image_interned(PyInterpreterState *interp, PyObject *s);

/* Calls fn on every code object of every image the process has mapped:
   the collector's caches pass (invariant 7) needs them, the heaps do not
   hold them. */
void lu_image_each_code(void (*fn)(PyCodeObject *, void *), void *arg);

#endif
