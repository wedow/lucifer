/* The Mersenne Twister MT19937 step of the _random module (lib/modules/_random.py
   keeps CPython's seeding and state conversion). The state is a bytearray of 624
   32-bit words followed by the index of the next one, so a draw touches no
   Python objects. */

#include "prim/prim.h"

#include <stdint.h>
#include <string.h>

#define MT_N 624
#define MT_M 397
#define MT_STATE_BYTES ((MT_N + 1) * sizeof(uint32_t))

static uint32_t *
state_of(PyObject *o)
{
    if (!PyByteArray_Check(o) || PyByteArray_GET_SIZE(o) != (Py_ssize_t)MT_STATE_BYTES) {
        lu_err_format(PyExc_TypeError, "mt state must be a bytearray of %zu bytes", MT_STATE_BYTES);
        return NULL;
    }
    return (uint32_t *)PyByteArray_AS_STRING(o);
}

static void
regenerate(uint32_t *mt)
{
    static const uint32_t mag[2] = { 0, 0x9908b0dfU };
    int kk;
    uint32_t y;
    for (kk = 0; kk < MT_N - MT_M; kk++) {
        y = (mt[kk] & 0x80000000U) | (mt[kk + 1] & 0x7fffffffU);
        mt[kk] = mt[kk + MT_M] ^ (y >> 1) ^ mag[y & 1];
    }
    for (; kk < MT_N - 1; kk++) {
        y = (mt[kk] & 0x80000000U) | (mt[kk + 1] & 0x7fffffffU);
        mt[kk] = mt[kk + (MT_M - MT_N)] ^ (y >> 1) ^ mag[y & 1];
    }
    y = (mt[MT_N - 1] & 0x80000000U) | (mt[0] & 0x7fffffffU);
    mt[MT_N - 1] = mt[MT_M - 1] ^ (y >> 1) ^ mag[y & 1];
    mt[MT_N] = 0;
}

static uint32_t
next32(uint32_t *mt)
{
    if (mt[MT_N] >= MT_N) {
        regenerate(mt);
    }
    uint32_t y = mt[mt[MT_N]++];
    y ^= y >> 11;
    y ^= (y << 7) & 0x9d2c5680U;
    y ^= (y << 15) & 0xefc60000U;
    y ^= y >> 18;
    return y;
}

/* mt_next32(state): the next 32 random bits. */
static PyObject *
prim_mt_next32(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    uint32_t *mt;
    if (nargs != 1 || (mt = state_of(args[0])) == NULL) {
        return NULL;
    }
    return PyLong_FromUnsignedLong(next32(mt));
}

/* mt_random(state): a float in [0, 1) from 53 random bits. */
static PyObject *
prim_mt_random(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    uint32_t *mt;
    if (nargs != 1 || (mt = state_of(args[0])) == NULL) {
        return NULL;
    }
    uint32_t a = next32(mt) >> 5, b = next32(mt) >> 6;
    return PyFloat_FromDouble((a * 67108864.0 + b) * (1.0 / 9007199254740992.0));
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_mt_methods[] = {
    FAST(mt_next32), FAST(mt_random),
    { NULL, NULL, 0, NULL },
};
