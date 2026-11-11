"""Powers of floats and ints in hot loops whose operand kinds the tier
knows (float ** int, int ** float, float ** float), with the edge cases:
a negative base to a fractional power (complex), zero to a negative power,
an overflow, nan and infinities, and a float subclass."""
from support import loops


class F(float):
    def __pow__(self, other):
        return 'F.__pow__'


def p(a, b):
    return a ** b


def run():
    s = 0.0
    for i in range(1, 400):
        s += p(i * 0.5, 2) + p(2, i * 0.01) + p(i * 0.1, 0.5) + p(1.0001, i)
    out = [round(s, 6)]
    for a, b in ((-8.0, 1 / 3), (0.0, -1.0), (10.0, 1e10), (float('nan'), 0), (1.0, float('nan')),
                 (float('inf'), -1), (-2.0, 3), (-2.0, 0.5), (2, -1.0), (F(2.0), 2)):
        try:
            out.append(p(a, b))
        except (ZeroDivisionError, OverflowError) as e:
            out.append((type(e).__name__, str(e)))
    return out


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
