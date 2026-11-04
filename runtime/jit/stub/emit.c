/* The target where there is no backend (FOUNDATIONS 2.11): the passes
   compile, nothing is emitted, and every function runs interpreted. */
#include "jit/target.h"

const struct lu_target lu_target = { .name = NULL };

int
lu_target_emit(struct lu_func *f, struct lu_unit *unit)
{
    f->fail = "no backend";
    return -1;
}

void
lu_target_free(struct lu_unit *unit)
{
}
