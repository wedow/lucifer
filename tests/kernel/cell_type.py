# The cell type: cell_contents, construction, comparison.
def outer():
    x = 1

    def inner():
        return x
    return inner


f = outer()
c = f.__closure__[0]
print(type(c).__name__, c.cell_contents)
c.cell_contents = 5
print(f(), c.cell_contents)
del c.cell_contents
try:
    f()
except NameError as e:
    print('NameError', e)
try:
    c.cell_contents
except ValueError as e:
    print('ValueError', e)
c.cell_contents = 7
print(f(), repr(c).startswith('<cell at 0x'), repr(c).endswith(': int object at ' + hex(id(7)) + '>'))
e = type(c)()
print(repr(e).endswith(': empty>'), type(c)(3).cell_contents)
print(e < type(c)(1), e == type(c)(), type(c)(1) < type(c)(2), type(c)(2) >= type(c)(2), c == 7, c != type(c)(8))
try:
    hash(c)
except TypeError as err:
    print('TypeError', err)
try:
    type(c)(1, 2)
except TypeError as err:
    print('TypeError')
