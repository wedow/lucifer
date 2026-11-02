"""conversion int limit."""
from support import loops


def run():
    import sys
    before = sys.get_int_max_str_digits(); out = []
    try:
        sys.set_int_max_str_digits(640)
        for f in (lambda: int('7' * 641), lambda: str(10**640)):
            try: f()
            except ValueError as e: out.append((type(e).__name__, 'limit' in str(e)))
        out.append(len(str(int('7' * 640))))
        out.append(int('f' * 1000, 16).bit_length())
    finally: sys.set_int_max_str_digits(before)
    return out


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
