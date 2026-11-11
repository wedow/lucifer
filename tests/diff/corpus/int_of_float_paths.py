"""int(x) in hot loops for floats (positive, negative, huge, the
infinities and nan, which raise), ints, bools, strs, a float subclass with
its own __int__, an int subclass made from a float, and int() with a base
and with the keyword."""
from support import loops


class F(float):
    def __int__(self):
        return 42


class I(int):
    pass


def run():
    s = 0
    for i in range(400):
        x = i * 0.37 - 50.0
        s += int(x) + int(-x) + int(i) + int(True) + int(F(x))
    out = [s, int(1e30), int(-0.9), I(3.9), type(I(3.9)).__name__, int('12'), int('ff', 16), int('11', base=2), int()]
    for bad in (float('inf'), float('-inf'), float('nan'), None, '1.5'):
        try:
            int(bad)
        except (OverflowError, ValueError, TypeError) as e:
            out.append((type(e).__name__, str(e)))
    return out


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
