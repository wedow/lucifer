# Four threads compile, parse and tokenize at once: the compiler keeps no
# state between calls, so every thread gets the oracle's bytes.
import ast, gc, io, marshal, threading, tokenize, zlib
gc.disable()

SOURCES = [
    "x = 1\nprint(x + 2)\n",
    "def f(a, /, b=2, *c, d, **e):\n    return [i * a for i in c if i]\n",
    "class C(B, metaclass=M):\n    def m(self):\n        return super().m() + (yield)\n",
    "async def g():\n    async with a as b, c:\n        await d\n    return {k: v async for k, v in e}\n",
    "try:\n    pass\nexcept* E as e:\n    raise\nfinally:\n    del x\n",
    "match p:\n    case [1, *r] | {'k': r}:\n        pass\n    case C(a=1) if a:\n        pass\n",
    "f'{a!r:>{w}} {b=}' + t'{c}'\n",
    "lambda: (a := 1)\n",
    "def f(:\n",
    "x = '\\N{BULLET} \\x41 \\u00e9' b'\\x00\\n'\n",
    "type A[T: int, *U] = list[T]\n",
    "with a:\n    pass\nelse_ = 1 if b else 2\n",
]


def work(results, n):
    out = []
    for i in range(40):
        for j, src in enumerate(SOURCES):
            try:
                code = compile(src, f"<s{j}>", "exec")
                out.append((j, zlib.crc32(marshal.dumps(code))))
                tree = ast.parse(src)
                out.append((j, zlib.crc32(ast.dump(tree, include_attributes=True).encode())))
                out.append((j, zlib.crc32(marshal.dumps(compile(tree, f"<s{j}>", "exec")))))
                out.append((j, len(list(tokenize.generate_tokens(io.StringIO(src).readline)))))
            except SyntaxError as e:
                out.append((j, e.msg, e.lineno, e.offset))
    results[n] = out


results = {}
threads = [threading.Thread(target=work, args=(results, n)) for n in range(4)]
for t in threads:
    t.start()
for t in threads:
    t.join()
print(all(results[n] == results[0] for n in range(4)), len(results[0]))
print(zlib.crc32(repr(results[0]).encode()))
