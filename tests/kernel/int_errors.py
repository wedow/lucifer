# Error types and texts of int operations, caught and printed.
class A:
    pass
tests = (
    lambda: 1 // 0, lambda: 1 % 0, lambda: (1).__divmod__(0), lambda: 1 / 0,
    lambda: 0 ** -1, lambda: (2).__pow__(3, 0), lambda: (2).__pow__(-1, 4),
    lambda: 1 << -1, lambda: 1 >> -1, lambda: 1 << 10 ** 30,
    lambda: int('x'), lambda: int(''), lambda: int('1', 99), lambda: int(None), lambda: int([]),
    lambda: int(1, 2), lambda: int(base=2), lambda: int('1', base=2.0), lambda: int(1, 2, 3),
    lambda: (256).to_bytes(1, 'big'), lambda: (-1).to_bytes(2, 'big'), lambda: (1).to_bytes(1, 'x'),
    lambda: 1 + 'a', lambda: 1 + A(), lambda: 1 < 'a', lambda: -'a', lambda: ~None,
    lambda: 10 ** 400 / 1, lambda: (10 ** 400).__float__(),
    lambda: (5).__round__('a'), lambda: 1 in 5, lambda: bool(1, 2), lambda: int.__add__(1.0, 2),
)
for t in tests:
    try:
        print(t())
    except Exception as e:
        print(type(e).__name__, e)
