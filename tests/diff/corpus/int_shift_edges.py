"""int shift edges."""
from support import loops


def run():
    results = [(x << n, x >> n) for x in (-17, 0, 17, 2**100) for n in (0, 31, 64, 150)]
    try: 1 << -1
    except ValueError as e: results.append((type(e).__name__, str(e)))
    return results


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
