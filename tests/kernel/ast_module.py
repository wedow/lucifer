# The ast module over the compiler's parser: parse, dump, unparse, walkers, literal_eval.
import ast

SOURCE = '''\
"""Module doc."""
from __future__ import annotations
import os.path as p, sys
from . import sibling
from .. import (a as b, c)

CONST: int = 10
x = y = 1 + 2 * 3 ** -4
a, *b = [1, 2.5, 3j, "s", b"b", None, True, ...]
c = {1: 2, **d}
e = {1, 2} | {*f}
s = f"{x!r:>{width}} and {y=}" "tail"
t = t"template {x}"
l = lambda p, /, q=1, *r, k, **kw: (p, q, r, k, kw)
g = (i for i in range(10) if i % 2 for j in "ab")
h = [k async for k in aiter() if await k]
i = {k: v for k, v in items}
if x and not y or z: pass
elif (w := 5) > 3: pass
else: pass
while x < 10:
    x += 1
    if x == 5: continue
    break
else:
    pass
for i, (j, k) in enumerate(z):
    del i
try:
    raise ValueError("v") from None
except* (TypeError, ValueError) as eg:
    pass
else:
    pass
finally:
    pass
with open("f") as fh, ctx():
    pass
async def coro(a: int = 1, *args: str, b, **kwargs) -> None:
    async with a as b: await b
    async for i in aiter(): yield i
@decorator(1)
class Cls(Base, metaclass=Meta, flag=True):
    """Class doc."""
    def method[T: int, *Ts, **P](self, x: T) -> list[T]:
        return super().method(x)[1:2, ::3]
type Alias[T] = list[T]
match command:
    case [1, 2, *rest]: pass
    case {"k": v, **others}: pass
    case Point(x=0, y=0) | Point(1, 2) as pt: pass
    case "str" | -1 | 1.5 | None if guard: pass
    case _: pass
global_name = 0
def scoped():
    global global_name
    nonlocal_target = 1
    def inner():
        nonlocal nonlocal_target
        nonlocal_target += 1
    assert x, "message"
    return
x.attr.sub = y[1]
print(x if y else z, *args, key=value, **opts)
x: int
(yield_value) if 0 else None
'''
tree = ast.parse(SOURCE)
print(ast.dump(tree))
print(ast.dump(tree, include_attributes=True))
print(ast.dump(tree.body[8], indent=2))
print(ast.dump(tree.body[8], indent='\t', annotate_fields=False))
print(ast.dump(tree, show_empty=True)[:300])
print(ast.unparse(tree))
print(repr(tree), repr(tree.body[1]), repr(tree.body[7]))

# unparse round trips
for source in ('x = 1', 'a + b * c', 'f(*a, **k)', 'lambda: (yield)', 'x if y else z', '(a, b) = c',
               'print("a" "b", f"{x}")', 'try:\n    pass\nexcept:\n    pass', 'async def f(): await x',
               'class A:\n    def f(self): pass', 'for x in y: pass', '{**a, "b": 1}', 'a[1:2, ::3]',
               '-x ** 2', 'not a in b', '(1).real', "'''doc'''", 'x = "a\\nb"', "f'{a!r:^{w}}'"):
    parsed = ast.parse(source)
    text = ast.unparse(parsed)
    print(repr(source), '->', repr(text), ast.dump(ast.parse(text)) == ast.dump(parsed))

# parse modes and options
print(ast.dump(ast.parse('1 + 2', mode='eval')))
print(ast.dump(ast.parse('x = 1', mode='single')))
print(ast.dump(ast.parse('(int, str) -> bool', mode='func_type')))
print(ast.dump(ast.parse('x = 1  # type: int', type_comments=True)))
print(ast.dump(ast.parse('x = 1 + 2 * 3', optimize=1)))
print(ast.dump(ast.parse('assert 0\n"doc"', optimize=2)))
print(ast.dump(ast.parse('match x:\n    case 1: pass', feature_version=(3, 10))))
for bad in ('match x:\n    case 1: pass', 'x = (y := 1)', 'async def f(): pass'):
    try:
        ast.parse(bad, feature_version=(3, 4))
    except SyntaxError as e:
        print('feature', e.msg)
try:
    ast.parse('x = (', filename='bad.py')
except SyntaxError as e:
    print(e.msg, e.filename, e.lineno, e.offset)

# walkers
print(sum(1 for _ in ast.walk(tree)))
print([type(n).__name__ for n in ast.iter_child_nodes(tree.body[7])])
print([name for name, _ in ast.iter_fields(tree.body[6])])
print(ast.get_docstring(tree, clean=False))
cls = [n for n in tree.body if isinstance(n, ast.ClassDef)][0]
print(ast.get_docstring(cls, clean=False))


class Names(ast.NodeVisitor):
    def __init__(self):
        self.names = []

    def visit_Name(self, node):
        self.names.append(node.id)

    def visit_Constant(self, node):
        self.names.append(repr(node.value))


v = Names()
v.visit(ast.parse('a = b + 1\nprint(c, "s")'))
print(v.names)


class Rewrite(ast.NodeTransformer):
    def visit_Name(self, node):
        return ast.copy_location(ast.Name(id=node.id.upper(), ctx=node.ctx), node)

    def visit_Pass(self, node):
        return None


t = Rewrite().visit(ast.parse('a = b\npass\nc = a + b'))
print(ast.unparse(t), ast.dump(t.body[0], include_attributes=True))
node = ast.Expression(ast.BinOp(ast.Constant(1), ast.Add(), ast.Constant(2)))
node = ast.fix_missing_locations(node)
print(ast.dump(node, include_attributes=True))
print(eval(compile(node, 'built', 'eval')))
ast.increment_lineno(node, 10)
print(node.body.lineno, node.body.left.lineno)
print(ast.dump(ast.copy_location(ast.Pass(), ast.parse('x').body[0]), include_attributes=True))

# literal_eval
for text in ('1', '[1, 2, (3, 4)]', "{'a': {1, 2}}", '-5', '1+2j', "b'x'", 'None', '{}', 'set()', "f'x'",
             '[x]', '1 + 2', '__import__("os")', '(1,', '--1', '{1: [2, 3], "k": (4,)}'):
    try:
        print(repr(text), '->', repr(ast.literal_eval(text)))
    except (ValueError, SyntaxError, TypeError) as e:
        print(repr(text), type(e).__name__, e)

# nodes as values
print(ast.Constant(1) == ast.Constant(1), ast.compare(ast.parse('x = 1'), ast.parse('x = 1')),
      ast.compare(ast.parse('x = 1'), ast.parse('x = 2')))
print(ast.Name('a').__replace__(id='b').id)
print(isinstance(ast.parse('1').body[0], ast.stmt), ast.Expr.__mro__[1].__name__, ast.Add.__bases__[0].__name__)
print(ast.AST.__module__, ast.parse.__module__, ast.Module.__match_args__)
