# luc-idah: a function whose operand kinds change one site at a time, in
# more phases than any fixed number of recompiles allows. Each phase makes
# one more site meet floats, so one more guard keeps failing; the tier
# must settle each time on a unit that no longer speculates there, never
# give the function up. tools/jitcheck runs this at two sizes and requires
# that exits do not grow with the iterations and that no function here is
# disabled or retired.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
PHASES = 20


def f(v):
    return (v[0] * 3 + v[1] * 3 + v[2] * 3 + v[3] * 3 + v[4] * 3 + v[5] * 3 + v[6] * 3 + v[7] * 3
            + v[8] * 3 + v[9] * 3 + v[10] * 3 + v[11] * 3 + v[12] * 3 + v[13] * 3 + v[14] * 3 + v[15] * 3
            + v[16] * 3 + v[17] * 3 + v[18] * 3 + v[19] * 3)


def phase(v, n):
    s = 0
    for i in range(n):
        s += f(v)
    return s


def run(n):
    s = 0
    v = [1] * PHASES
    for k in range(PHASES):
        v[k] = 1.5
        s += phase(v, 200)
    return s + phase(v, n)


print(run(N))
