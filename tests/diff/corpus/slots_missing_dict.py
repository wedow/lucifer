"""slots missing dict."""
from support import loops


def run():
    class C: __slots__ = ('x',)
    c = C(); c.x = 7
    try: c.y = 3
    except AttributeError as e: return c.x, hasattr(c, '__dict__'), type(e).__name__


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
