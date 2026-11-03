# The node classes of _ast: shape of every class, construction, repr, replace.
import _ast

classes = sorted((n, c) for n, c in vars(_ast).items() if isinstance(c, type) and issubclass(c, _ast.AST))
print(len(classes), sorted(n for n in vars(_ast) if n.startswith('PyCF')))
for name, cls in classes:
    print(name, [b.__name__ for b in cls.__mro__], cls.__module__, cls.__qualname__)
    print('  ', cls._fields, cls._attributes, cls.__match_args__)
    print('  ', repr(cls.__doc__))
    print('  ', getattr(cls, '_field_types', None))
    print('  ', {k: v for k, v in vars(cls).items() if v is None})


def attempt(label, f):
    try:
        print(label, f())
    except BaseException as e:
        print(label, type(e).__name__, e)


N = _ast
attempt('name', lambda: N.Name('x'))
attempt('name ctx', lambda: N.Name('x').ctx)
attempt('name pos', lambda: N.Name('x', N.Store(), 1))
attempt('name kw', lambda: N.Name(id='x', lineno=1, col_offset=2))
attempt('name twice', lambda: N.Name('x', id='y'))
attempt('call', lambda: N.Call(N.Name('f'), [N.Constant(1), N.Constant(2), N.Constant(3)], []))
attempt('call dict', lambda: sorted(N.Call(N.Name('f')).__dict__))
attempt('const', lambda: N.Constant(1).__dict__)
attempt('const kind', lambda: N.Constant(1).kind)
attempt('module', lambda: N.Module([N.Expr(N.Constant(None))], []))
attempt('dict', lambda: N.Dict().keys)
attempt('func', lambda: sorted(N.FunctionDef('f', N.arguments()).__dict__))
attempt('slice', lambda: N.Slice().lower)
attempt('lineno missing', lambda: N.Name('x').lineno)
attempt('deep', lambda: N.Expression(N.BinOp(N.BinOp(N.Name('a'), N.Add(), N.Name('b')), N.Mult(), N.Constant(2))))
attempt('end_lineno', lambda: N.Name('x', N.Load(), lineno=1).end_lineno)
n = N.Name('x', N.Load(), lineno=3, col_offset=4)
attempt('replace', lambda: (n.__replace__(id='y').__dict__, n.__dict__))
attempt('replace bad', lambda: n.__replace__(foo=1))
attempt('replace pos', lambda: n.__replace__(1))
attempt('replace missing', lambda: N.Name.__new__(N.Name).__replace__())
attempt('replace missing2', lambda: N.BinOp.__new__(N.BinOp).__replace__())
attempt('reduce', lambda: n.__reduce__()[1:])
attempt('reduce type', lambda: n.__reduce__()[0] is N.Name)
attempt('reduce empty', lambda: N.Name.__new__(N.Name).__reduce__()[1:])
attempt('match', lambda: [k for k in (N.Name.__match_args__)])
cyc = N.List([], N.Load())
cyc.elts.append(cyc)
attempt('cyclic', lambda: cyc)
attempt('isinstance', lambda: (isinstance(N.Add(), N.operator), isinstance(N.Add(), N.AST), N.Add() is N.Add()))
attempt('new attr', lambda: setattr(n, 'extra', 5) or n.extra)
attempt('too many', lambda: N.Pass(1))
attempt('subclass', lambda: type('Mine', (N.AST,), {})(1))
attempt('annotations', lambda: N.Name.__annotations__)
