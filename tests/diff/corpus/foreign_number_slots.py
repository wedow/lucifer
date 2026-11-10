"""Operators on an extension type's numbers (decimal.Decimal) in functions
that inline into one another, with an int operand, in place, and where the
slots decline (a float), so that the error is the interpreter's."""
from decimal import Decimal
from support import loops


def scale(x, k):
    return x * k


def step(acc, x):
    acc += scale(x, 3)
    return acc - x / Decimal(7)


def mixed(x):
    try:
        return x + 1.5
    except TypeError as e:
        return str(e)


def run():
    acc = Decimal(0)
    for i in range(300):
        acc = step(acc, Decimal(i) / 10)
        acc = acc + i
    return [str(acc.quantize(Decimal('0.0001'))), mixed(Decimal(2)), str(Decimal(5) ** 3), str(7 - Decimal('0.5'))]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
