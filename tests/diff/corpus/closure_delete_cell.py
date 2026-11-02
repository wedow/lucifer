"""closure delete cell."""
from support import loops


def run():
    def outer():
        x = 3
        def read(): return x
        def remove():
            nonlocal x
            del x
        remove()
        try: read()
        except NameError as e: return type(e).__name__, read.__closure__[0].cell_contents
    try: outer()
    except ValueError as e: return type(e).__name__, str(e)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
