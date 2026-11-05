import types

FT = types.FunctionType


def f(a, b=2, *, c=3):
    return a, b, c


def mk(x):
    def g():
        return x
    return g


code = f.__code__
g = FT(code, {"k": 1})
print(g(1, 2, c=3), g.__name__, g.__qualname__, g.__defaults__, g.__kwdefaults__, g.__closure__)
g = FT(code, {}, "nm", (5,))
print(g(1, c=1), g.__name__, g.__qualname__)
g = FT(code, {}, name="nm2", argdefs=(7,))
print(g(1, c=1), g.__name__)
print(FT(code, {}, None, (1,), None, {"c": 9})(1))
print(FT(code, {}, None, (1,), None, None)(1, c=0))
cc = mk(4)
cell = cc.__closure__[0]
h = FT(cc.__code__, {}, "h", None, (types.CellType(8),))
print(h(), h.__closure__[0].cell_contents)
h = FT(cc.__code__, {}, closure=(cell,))
print(h())
print(FT(code, {"__name__": "modx"}).__module__)
print(FT(code, {}).__module__)
print(FT(code, {"__builtins__": {"len": len}}).__builtins__ if hasattr(FT(code, {}), "__builtins__") else "nb")


def err(*a, **k):
    try:
        FT(*a, **k)
    except BaseException as e:
        print(type(e).__name__, e)
    else:
        print("no error")


err()
err(code)
err(1, {})
err(code, 1)
err(code, {}, 1)
err(code, {}, None, 1)
err(code, {}, None, [1])
err(code, {}, None, None, 1)
err(code, {}, None, None, None, 1)
err(code, {}, None, None, (1,))
err(cc.__code__, {})
err(cc.__code__, {}, None, None, ())
err(cc.__code__, {}, None, None, (cell, cell))
err(cc.__code__, {}, None, None, (1,))
err(cc.__code__, {}, None, None, 1)
err(code, {}, None, None, (cell,))
err(code, {}, bad=1)
err(code, {}, "n", None, None, None, 1)
err(code, globals=1)
err(code, {}, code="x")
err(code, {}, name=1)
