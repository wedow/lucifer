#include "lucifer.h"

void
lu_unimplemented(const char *name)
{
    fprintf(stderr, "lucifer: unimplemented C-API function %s\n", name);
    abort();
}
