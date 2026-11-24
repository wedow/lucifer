# Instances of a class made from a C spec with a tp_finalize, made and
# dropped in a hot loop, with and without attributes: each is finalized
# once, as the interpreter's release finalizes it.
import spec_finalize


def churn(n):
    for i in range(n):
        t = spec_finalize.T()
        t.x = i


def churn_bare(n):
    for i in range(n):
        spec_finalize.T()


for k in range(3):
    churn(5000)
    churn_bare(5000)
    print(spec_finalize.count())
