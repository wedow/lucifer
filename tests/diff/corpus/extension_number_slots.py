"""Operators on an extension type's numbers (decimal's C slots): mixed with
ints, in place, subclasses whose reflected method wins, unsupported operands,
and the context each operation reads."""
import decimal
from decimal import Decimal, localcontext
from support import loops


class Sub(Decimal):
    def __radd__(self, other):
        return 'Sub.__radd__'


class Other:
    def __rmul__(self, other):
        return 'Other.__rmul__'


def run():
    out = []
    a, b = Decimal('1.25'), Decimal('3')
    out += [a + b, a - b, a * b, a / b, a // b, a % b, a ** 2, -a, abs(-a)]
    out += [a + 1, 2 * a, 7 // b, 2 ** b]
    x = Decimal('0.1')
    x += a
    x *= 3
    out.append(x)
    out.append(a + Sub('2'))
    out.append(a * Other())
    for bad in ('s', None, 1.5):
        try:
            a + bad
        except TypeError as exc:
            out.append(str(exc))
    with localcontext() as ctx:
        ctx.prec = 3
        out.append(Decimal(1) / Decimal(7))
    out.append(Decimal(1) / Decimal(7))
    decimal.getcontext().rounding = decimal.ROUND_DOWN
    out.append(Decimal('2.675').quantize(Decimal('0.01')))
    decimal.getcontext().rounding = decimal.ROUND_HALF_EVEN
    try:
        Decimal(1) / Decimal(0)
    except decimal.DivisionByZero as exc:
        out.append(type(exc).__name__)
    return [str(v) for v in out]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
