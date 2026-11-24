# Attribute hooks of ast node classes, whose base ast.AST has C slot
# wrappers for __getattribute__, __setattr__ and __delattr__: reads, writes
# and deletes through the bytecodes and through getattr, setattr, delattr
# and hasattr, while a class gains and loses its own __getattribute__,
# __getattr__, __setattr__ and __delattr__ mid-loop, a subclass of it
# included, and while a missing attribute falls back to __getattr__.
import ast


class Sub(ast.Name):
    pass


class SubSub(Sub):
    pass


def hook_getattribute(self, name):
    if name == 'id':
        return 'hooked-' + object.__getattribute__(self, name)
    return object.__getattribute__(self, name)


def hook_getattr(self, name):
    return 'missing-' + name


def hook_setattr(self, name, value):
    object.__setattr__(self, name, value * 2 if type(value) is int else value)


def hook_delattr(self, name):
    object.__setattr__(self, name, 'deleted')


def step(cls, i):
    """The class's hooks as they stand at step i of 12."""
    phase = i % 12
    for name, f, on in (('__getattribute__', hook_getattribute, phase in (1, 2, 7)),
                        ('__getattr__', hook_getattr, phase in (2, 3, 4, 8)),
                        ('__setattr__', hook_setattr, phase in (4, 5, 9)),
                        ('__delattr__', hook_delattr, phase in (5, 6, 10))):
        if on and name not in cls.__dict__:
            setattr(cls, name, f)
        elif not on and name in cls.__dict__:
            delattr(cls, name)


def attempt(f):
    try:
        return f()
    except AttributeError as e:
        return 'AttributeError: ' + str(e)


def run(cls, owner, label):
    out = []
    for i in range(36):
        step(owner, i)
        n = cls('x', ast.Load())
        n.extra = i
        setattr(n, 'more', i)
        row = [n.id, getattr(n, 'id'), n.extra, getattr(n, 'more'),
               attempt(lambda: n.nope), getattr(n, 'nope', 'default'), hasattr(n, 'nope'),
               attempt(lambda: getattr(n, 'nope'))]
        del n.extra
        row.append(attempt(lambda: n.extra))
        delattr(n, 'more')
        row.append(getattr(n, 'more', 'gone'))
        out.append(row)
    step(owner, 0)
    for i in (0, 1, 2, 4, 5, 8, 35):
        print(label, i, out[i])
    print(label, len(set(map(repr, out))))


run(ast.Name, ast.Name, 'name')
run(Sub, Sub, 'sub')
run(SubSub, Sub, 'subsub')
run(SubSub, SubSub, 'own')

# The node constructor sets its fields through the class's __setattr__.
Sub.__setattr__ = hook_setattr
print(Sub('x', ast.Load(), lineno=3).lineno, ast.Name('x', lineno=3).lineno)
del Sub.__setattr__
print(Sub('x', ast.Load(), lineno=3).lineno)
