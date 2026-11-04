/* Small runtime services: PyOS string conversions (Python/mystrtoul.c,
   pystrtod.c), the tracemalloc hooks, and interpreter ids. */

#include "lucifer.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

int
PyOS_vsnprintf(char *str, size_t size, const char *format, va_list va)
{
    if (size > INT_MAX - 1) {
        return -666;
    }
    int len = vsnprintf(str, size, format, va);
    if (size > 0) {
        str[size - 1] = '\0';          /* always terminated, even when truncated */
    }
    return len;
}

int
PyOS_snprintf(char *str, size_t size, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int len = PyOS_vsnprintf(str, size, format, ap);
    va_end(ap);
    return len;
}

long
PyOS_strtol(const char *str, char **ptr, int base)
{
    return strtol(str, ptr, base);
}

unsigned long
PyOS_strtoul(const char *str, char **ptr, int base)
{
    return strtoul(str, ptr, base);
}

/* tracemalloc is not running: -2, as when it is not tracing. */
int
PyTraceMalloc_Track(unsigned int domain, uintptr_t ptr, size_t size)
{
    return -2;
}

int
PyTraceMalloc_Untrack(unsigned int domain, uintptr_t ptr)
{
    return -2;
}

int64_t
PyInterpreterState_GetID(PyInterpreterState *interp)
{
    return 0;
}
