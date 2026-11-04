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

/* PyOS_strtoul and PyOS_strtol (Python/mystrtoul.c): libc's, with the 0o and
   0b prefixes of base 0 and overflow reported as ERANGE. */
unsigned long
PyOS_strtoul(const char *str, char **ptr, int base)
{
    const char *p = str;
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (base == 0) {
        int c = p[0] == '0' ? p[1] | 0x20 : 0;
        base = p[0] != '0' ? 10 : c == 'x' ? 16 : c == 'b' ? 2 : 8;
    }
    int prefix = p[0] == '0' && (((p[1] | 0x20) == 'x' && base == 16) || ((p[1] | 0x20) == 'o' && base == 8)
                                 || ((p[1] | 0x20) == 'b' && base == 2));
    if (!prefix) {
        return strtoul(p, ptr, base);
    }
    char *end;
    unsigned long v = strtoul(p + 2, &end, base);
    if (end == p + 2) {
        *ptr = (char *)str;
        return 0;
    }
    *ptr = end;
    return v;
}

long
PyOS_strtol(const char *str, char **ptr, int base)
{
    while (*str && isspace((unsigned char)*str)) {
        str++;
    }
    char sign = *str;
    if (sign == '+' || sign == '-') {
        str++;
    }
    unsigned long uresult = PyOS_strtoul(str, ptr, base);
    if (uresult <= (unsigned long)LONG_MAX) {
        return sign == '-' ? -(long)uresult : (long)uresult;
    }
    if (sign == '-' && uresult == 0 - (unsigned long)LONG_MIN) {
        return LONG_MIN;
    }
    errno = ERANGE;
    return LONG_MAX;
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
