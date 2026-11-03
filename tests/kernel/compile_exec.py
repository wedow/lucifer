# compile, exec and eval of source text: modes, flags, namespaces, errors.
import _ast


def attempt(label, f):
    try:
        print(label, f())
    except BaseException as e:
        print(label, type(e).__name__, e)


def syntax(label, source, mode='exec'):
    try:
        compile(source, '<test>', mode)
        print(label, 'compiled')
    except SyntaxError as e:
        print(label, type(e).__name__, e.msg, e.lineno, e.offset, e.end_lineno, e.end_offset, repr(e.text))


def ast_dump(tree):
    return type(tree).__name__, [type(n).__name__ for n in getattr(tree, 'body', ())], tree._fields


g = {}
exec('x = 5\ny = x * 2\ndef f(a, b=2, *args, c=3, **kw):\n    return (a, b, args, c, kw)\n', g)
print(g['x'], g['y'], g['f'](1), g['f'](1, 2, 3, c=4, d=5))
print(eval('1 + 2 * 3'), eval('  \t4 ** 2'), eval(b'"bytes" + "!"'), eval(bytearray(b'7 // 2')))
print(eval('x + y', {'x': 10, 'y': 20}), eval('x', {'x': 1}, {'x': 2}))
local = {}
exec('a = 1\nb = a + 1', {}, local)
print(sorted(local))
exec('class C:\n    z = 3\n    def m(self): return self.z\nr = C().m()', g)
print(g['r'], g['C'].__module__, g['__builtins__'] is not None)
print(eval(compile('[i * i for i in range(5)]', '<c>', 'eval')))
c = compile('a = 1\nb = a + 1\nc = [a, b]', 'file.py', 'exec')
print(c.co_name, c.co_filename, c.co_firstlineno, c.co_argcount, c.co_flags, c.co_names, c.co_varnames,
      c.co_consts, c.co_stacksize, c.co_nlocals, len(c.co_code), c.co_freevars, c.co_cellvars)
ns = {}
exec(c, ns)
print(ns['c'])
single = compile('1 + 1', '<single>', 'single')
print(single.co_consts, single.co_names)
print(compile('def f(x): return lambda: x', 'f', 'exec').co_consts[0].co_cellvars)

# source forms
attempt('bytes cookie', lambda: eval(compile(b'# coding: latin-1\n"caf\xe9"', 'f', 'eval')))
attempt('bytes bom', lambda: eval(compile(b'\xef\xbb\xbf"bom"', 'f', 'eval')))
attempt('memoryview', lambda: eval(compile(memoryview(b'1 + 1'), 'f', 'eval')))
attempt('null byte', lambda: compile('1\x00', 'f', 'eval'))
attempt('not source', lambda: compile(42, 'f', 'eval'))
attempt('eval not source', lambda: eval(42))
attempt('exec not source', lambda: exec(None))
attempt('pathlike', lambda: compile('1', b'bytes-name', 'eval').co_filename)

# arguments
attempt('bad mode', lambda: compile('1', 'f', 'bogus'))
attempt('bad flags', lambda: compile('1', 'f', 'eval', 0x40000000 << 4))
attempt('bad optimize', lambda: compile('1', 'f', 'eval', 0, False, 3))
attempt('func_type', lambda: compile('(int) -> str', 'f', 'func_type'))
attempt('func_type ast', lambda: ast_dump(compile('(int, str) -> bool', 'f', 'func_type', _ast.PyCF_ONLY_AST)))
attempt('no mode', lambda: compile('1', 'f'))
attempt('keyword args', lambda: compile(source='1', filename='f', mode='eval').co_filename)


# the AST route
tree = compile('x = 1\nif x:\n    print(x)\n', 'f', 'exec', _ast.PyCF_ONLY_AST)
print(type(tree).__name__, [type(n).__name__ for n in tree.body], tree.body[0].lineno, tree.body[1].end_lineno)
print(compile('1 + 2', 'f', 'eval', _ast.PyCF_ONLY_AST).body.op)
tree = compile('x = 1 + 2', 'f', 'exec', _ast.PyCF_OPTIMIZED_AST)
print(type(tree.body[0].value).__name__, tree.body[0].value.value)
code = compile(compile('y = 40 + 2', 'f', 'exec', _ast.PyCF_ONLY_AST), 'f', 'exec')
ns = {}
exec(code, ns)
print(ns['y'])
attempt('type comments', lambda: compile('x = 1  # type: int\n', 'f', 'exec',
                                         _ast.PyCF_ONLY_AST | _ast.PyCF_TYPE_COMMENTS).body[0].type_comment)
attempt('top-level await flag', lambda: compile('await x', 'f', 'exec', _ast.PyCF_ALLOW_TOP_LEVEL_AWAIT).co_flags & 0x80)
attempt('await without flag', lambda: compile('await x', 'f', 'exec'))

# optimize levels
for level in (-1, 0, 1, 2):
    c = compile('"""doc"""\nassert False, "msg"\nx = __debug__', 'f', 'exec', optimize=level)
    ns = {}
    try:
        exec(c, ns)
        print('optimize', level, ns.get('x'), ns.get('__doc__'))
    except AssertionError as e:
        print('optimize', level, 'AssertionError', e)

# future statements: the caller's are inherited unless dont_inherit
exec('from __future__ import annotations\nprint(compile("def f(x: int): pass", "f", "exec").co_flags & 0x1000000)')
print(compile('def f(x: int): pass', 'f', 'exec', 0, True).co_flags & 0x1000000)
print(compile('from __future__ import annotations\n1', 'f', 'exec').co_flags & 0x1000000)

# errors
syntax('bad syntax', 'x = (1,\n')
syntax('invalid', 'def f(:\n    pass\n')
syntax('indent', 'if 1:\npass\n')
syntax('unexpected indent', 'x = 1\n    y = 2\n')
syntax('return', 'return 1')
syntax('f-string', 'f"{1 +}"')
syntax('assign', '1 = x')
syntax('await', 'def f():\n    await x\n')
syntax('nonlocal', 'nonlocal x')
syntax('duplicate arg', 'def f(a, a): pass')
syntax('star', '*a')
syntax('eval statement', 'x = 1', 'eval')
syntax('unclosed', '"abc')
syntax('unclosed triple', '"""abc')
syntax('non-utf8', 'x = "\u00e9"\n\u00e9 = 1')
syntax('continue', 'continue')
syntax('walrus', '(x := 1) = 2')
syntax('match', 'match x:\n    case 1 | y: pass')
syntax('too deep', '(' * 300 + ')' * 300)
attempt('exec syntax', lambda: exec('def'))
attempt('eval syntax', lambda: eval('1 +'))
attempt('name error', lambda: exec('undefined_name'))
attempt('runtime error', lambda: exec('1 / 0'))
attempt('exec returns', lambda: exec('x = 1'))

# scoping through exec and eval
def outer():
    v = 'outer-local'
    exec('captured = v', globals(), locals())
    return eval('v + "!"')


attempt('function locals', outer)
class K:
    attr = 1
    value = eval('attr + 1', {'attr': 100})
print(K.value)
attempt('globals not dict', lambda: exec('1', []))
attempt('eval locals mapping', lambda: eval('a', {}, {'a': 3}))
attempt('modifying globals', lambda: exec('import_me = __name__', g) or g['import_me'])
