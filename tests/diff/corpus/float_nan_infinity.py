"""float nan infinity."""
from support import loops


def run():
    import math
    nan, inf = float('nan'), float('inf')
    return nan == nan, nan != nan, nan < 0, math.isnan(nan + 1), repr(inf), repr(-inf), repr(inf - inf)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
