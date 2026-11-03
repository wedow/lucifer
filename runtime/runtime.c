/* The runtime record (Python/pylifecycle.c, pystate.c). Everything the whole
   process shares lives in _PyRuntime; the checks in tools/abi/globals.py keep
   other writable globals out. */

#include "lucifer.h"

LU_EXPORT union lu_runtime _PyRuntime = { {
    .debug_offsets = { .version = PY_VERSION_HEX, .free_threaded = 1 },
    .allocators = { LU_ALLOCATOR(lu_raw), LU_ALLOCATOR(lu_mem), LU_ALLOCATOR(lu_obj),
                    { PTHREAD_MUTEX_INITIALIZER, NULL } },
    .locks = { PTHREAD_MUTEX_INITIALIZER, NULL },
} };

_Static_assert(sizeof(struct lu_debug_offsets) == 1024, "reserved head");
_Static_assert(sizeof(_PyRuntime) == LU_RUNTIME_SIZE, "oracle size of _PyRuntime");

void
lu_lock_register(struct lu_lock *lock)
{
    pthread_mutex_lock(&_PyRuntime.locks.mutex);
    lock->next = _PyRuntime.locks.head;
    _PyRuntime.locks.head = lock;
    pthread_mutex_unlock(&_PyRuntime.locks.mutex);
}
