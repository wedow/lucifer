"""Truth of floats (0.0, -0.0, nan, inf), bytes, sets and frozensets, in a
function that is inlined into its caller and at the caller's level."""
from support import loops


def test(v):
    if v:
        return 1
    return 0


def run():
    values = [0.0, -0.0, float('nan'), float('inf'), 1e-300, b'', b'x', set(), {1}, frozenset(), frozenset([0])]
    out = []
    for _ in range(100):
        out = [test(v) for v in values]
    out += [1 if v else 0 for v in values]
    return out


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
