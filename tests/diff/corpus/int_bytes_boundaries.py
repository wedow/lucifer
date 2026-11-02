"""int bytes boundaries."""
from support import loops


def run():
    results = [(x.to_bytes(12, 'big', signed=True).hex(), int.from_bytes(x.to_bytes(12, 'little', signed=True), 'little', signed=True)) for x in (-2**80, -1, 0, 2**80)]
    try: (256).to_bytes(1)
    except OverflowError as e: results.append(type(e).__name__)
    return results


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
