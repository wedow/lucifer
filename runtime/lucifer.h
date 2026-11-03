/* Runtime-internal declarations shared across runtime/. */
#ifndef LUCIFER_H
#define LUCIFER_H

#define LU_EXPORT __attribute__((visibility("default")))
#define LU_STUB __attribute__((weak))
#define LU_UNIMPLEMENTED(name) lu_unimplemented(name)

/* Report an unported C-API function by name and abort. */
_Noreturn void lu_unimplemented(const char *name);

#endif
