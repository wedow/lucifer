/* Hashes (Python/pyhash.c, Python/bootstrap_hash.c): the process-wide
   secret, SipHash-1-3 over buffers, numeric hashes modulo 2**61 - 1 and the
   xxHash lanes tuples and slices share. Bit-exact with CPython, so hash
   values and table orders match the oracle. */

#include "lucifer.h"

#include <errno.h>
#include <math.h>
#include <string.h>
#include <sys/random.h>

/* pycore_pyhash.h; 24 bytes, exported for _elementtree. */
LU_EXPORT union {
    unsigned char uc[24];
    struct {
        uint64_t k0;
        uint64_t k1;
    } siphash;
} _Py_HashSecret;

_Static_assert(sizeof(_Py_HashSecret) == 24, "oracle size of _Py_HashSecret");

static void
lcg_urandom(unsigned int x, unsigned char *buffer, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        x *= 214013;
        x += 2531011;
        buffer[i] = (x >> 16) & 0xff;
    }
}

/* _Py_HashRandomization_Init. Called once, before the first hash. */
int
lu_hash_secret_init(int use_seed, unsigned long seed)
{
    if (use_seed) {
        if (seed == 0) {
            memset(&_Py_HashSecret, 0, sizeof _Py_HashSecret);
        }
        else {
            lcg_urandom((unsigned int)seed, _Py_HashSecret.uc, sizeof _Py_HashSecret);
        }
        return 0;
    }
    /* pyurandom(blocking=0): startup must not wait for entropy (PEP 524). */
    size_t done = 0;
    while (done < sizeof _Py_HashSecret) {
        ssize_t n = getrandom(_Py_HashSecret.uc + done, sizeof _Py_HashSecret - done,
                              GRND_NONBLOCK);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        done += (size_t)n;
    }
    return 0;
}

#define ROTATE(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))

#define HALF_ROUND(a, b, c, d, s, t) \
    a += b; c += d;                  \
    b = ROTATE(b, s) ^ a;            \
    d = ROTATE(d, t) ^ c;            \
    a = ROTATE(a, 32);

#define SINGLE_ROUND(v0, v1, v2, v3)  \
    HALF_ROUND(v0, v1, v2, v3, 13, 16); \
    HALF_ROUND(v2, v1, v0, v3, 17, 21);

static uint64_t
siphash13(uint64_t k0, uint64_t k1, const void *src, Py_ssize_t src_sz)
{
    uint64_t b = (uint64_t)src_sz << 56;
    const uint8_t *in = src;
    uint64_t v0 = k0 ^ 0x736f6d6570736575ULL;
    uint64_t v1 = k1 ^ 0x646f72616e646f6dULL;
    uint64_t v2 = k0 ^ 0x6c7967656e657261ULL;
    uint64_t v3 = k1 ^ 0x7465646279746573ULL;

    while (src_sz >= 8) {
        uint64_t mi;
        memcpy(&mi, in, sizeof mi);
        in += sizeof mi;
        src_sz -= sizeof mi;
        v3 ^= mi;
        SINGLE_ROUND(v0, v1, v2, v3);
        v0 ^= mi;
    }
    uint64_t t = 0;
    memcpy(&t, in, (size_t)src_sz);     /* little-endian tail, as pyhash.c's switch */
    b |= t;

    v3 ^= b;
    SINGLE_ROUND(v0, v1, v2, v3);
    v0 ^= b;
    v2 ^= 0xff;
    SINGLE_ROUND(v0, v1, v2, v3);
    SINGLE_ROUND(v0, v1, v2, v3);
    SINGLE_ROUND(v0, v1, v2, v3);
    return (v0 ^ v1) ^ (v2 ^ v3);
}

/* _Py_KeyedHash: siphash13 under a caller's 64-bit key (hash-based pycs). */
uint64_t
lu_keyed_hash(uint64_t key, const void *src, Py_ssize_t size)
{
    return siphash13(key, 0, src, size);
}

Py_hash_t
Py_HashBuffer(const void *ptr, Py_ssize_t len)
{
    if (len == 0) {
        return 0;
    }
    Py_hash_t x = (Py_hash_t)siphash13(_Py_HashSecret.siphash.k0,
                                       _Py_HashSecret.siphash.k1, ptr, len);
    return x == -1 ? -2 : x;
}

Py_hash_t
Py_HashPointer(const void *ptr)
{
    uintptr_t y = (uintptr_t)ptr;
    Py_hash_t hash = (Py_hash_t)((y >> 4) | (y << (8 * sizeof y - 4)));
    return hash == -1 ? -2 : hash;
}

Py_hash_t
PyObject_GenericHash(PyObject *obj)
{
    return Py_HashPointer(obj);
}

Py_hash_t
_Py_HashDouble(PyObject *inst, double v)
{
    if (!isfinite(v)) {
        if (isinf(v)) {
            return v > 0 ? PyHASH_INF : -PyHASH_INF;
        }
        return PyObject_GenericHash(inst);
    }
    int e, sign = 1;
    double m = frexp(v, &e);
    if (m < 0) {
        sign = -1;
        m = -m;
    }
    Py_uhash_t x = 0, y;
    while (m) {
        x = ((x << 28) & PyHASH_MODULUS) | x >> (PyHASH_BITS - 28);
        m *= 268435456.0;
        e -= 28;
        y = (Py_uhash_t)m;
        m -= y;
        x += y;
        if (x >= PyHASH_MODULUS) {
            x -= PyHASH_MODULUS;
        }
    }
    e = e >= 0 ? e % PyHASH_BITS : PyHASH_BITS - 1 - ((-1 - e) % PyHASH_BITS);
    x = ((x << e) & PyHASH_MODULUS) | x >> (PyHASH_BITS - e);
    x = x * sign;
    if (x == (Py_uhash_t)-1) {
        x = (Py_uhash_t)-2;
    }
    return (Py_hash_t)x;
}

/* long_hash. */
Py_hash_t
lu_long_hash(PyLongObject *v)
{
    if (_PyLong_IsCompact(v)) {
        Py_uhash_t x = (Py_uhash_t)_PyLong_CompactValue(v);
        return x == (Py_uhash_t)-1 ? -2 : (Py_hash_t)x;
    }
    Py_ssize_t i = (Py_ssize_t)(v->long_value.lv_tag >> _PyLong_NON_SIZE_BITS);
    int sign = 1 - (int)(v->long_value.lv_tag & _PyLong_SIGN_MASK);
    Py_uhash_t x = 0;
    while (--i >= 0) {
        x = ((x << PyLong_SHIFT) & PyHASH_MODULUS) | (x >> (PyHASH_BITS - PyLong_SHIFT));
        x += v->long_value.ob_digit[i];
        if (x >= PyHASH_MODULUS) {
            x -= PyHASH_MODULUS;
        }
    }
    x = x * sign;
    return x == (Py_uhash_t)-1 ? -2 : (Py_hash_t)x;
}

/* Hashes n items with lu_hash; -1 with an error set if one is unhashable.
   `mangle_length` adds the length as tuple_hash does; slice_hash does not. */
Py_hash_t
lu_xxhash_items(PyObject *const *items, Py_ssize_t n, int mangle_length)
{
    Py_uhash_t acc = LU_XXHASH_START;
    for (Py_ssize_t i = 0; i < n; i++) {
        Py_hash_t lane = lu_hash(items[i]);
        if (lane == -1) {
            return -1;
        }
        acc = lu_xxhash_step(acc, lane);
    }
    return lu_xxhash_end(acc, n, mangle_length);
}
