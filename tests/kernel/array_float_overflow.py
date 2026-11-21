from array import array
import struct


def show(label, operation):
    try:
        result = operation()
        print(label, result.tolist(), result.tobytes().hex())
    except Exception as exc:
        print(label, type(exc).__name__, str(exc))


def mutate(method, *args):
    result = array('f', [1.0])
    getattr(result, method)(*args)
    return result


# The last two finite doubles straddle the rounding boundary for binary32.
values = [1e300, -1e300, 0.0, -0.0,
          float.fromhex('0x1.fffffefffffffp+127'), float.fromhex('0x1.ffffffp+127')]
show('list', lambda: array('f', values))
show('tuple', lambda: array('f', tuple(values)))
show('iterator', lambda: array('f', iter(values)))
show('append', lambda: mutate('append', -1e300))
show('extend', lambda: mutate('extend', iter(values)))
show('fromlist', lambda: mutate('fromlist', values))
show('insert', lambda: mutate('insert', 0, 1e300))

# Overflow when obtaining the double still raises.
show('oversized integer', lambda: array('f', [10**400]))

# Standard-size struct formats reject overflow in both packing paths.
for fmt, args in (('<f', (1e300,)), ('>2f', (1.0, -1e300))):
    try:
        print(fmt, struct.pack(fmt, *args).hex())
    except OverflowError as exc:
        print(fmt, type(exc).__name__, str(exc))
