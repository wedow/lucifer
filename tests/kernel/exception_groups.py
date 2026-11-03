class A(Exception):
    pass


class B(Exception):
    pass


class C(A):
    pass


def check(label, cond):
    print(label, 'ok' if cond else 'FAIL')


def one(e):
    msg, = e.args
    return msg


def names(group):
    return [type(e).__name__ for e in group.exceptions]


# construction
a, b, c = A('a'), B('b'), C('c')
eg = ExceptionGroup('grp', [a, b, c])
check('type', type(eg) is ExceptionGroup)

print(eg.message, *names(eg))
print(eg.__str__())
check('base wraps', type(BaseExceptionGroup('m', [a])) is ExceptionGroup)
check('base keeps', type(BaseExceptionGroup('m', [KeyboardInterrupt()])) is BaseExceptionGroup)
check('is Exception', isinstance(eg, Exception) and isinstance(eg, BaseExceptionGroup))
for bad in ((), [], ['not an exception']):
    try:
        ExceptionGroup('m', bad)
    except (ValueError, TypeError) as e:
        print(type(e).__name__, one(e))
try:
    ExceptionGroup('m', [KeyboardInterrupt()])
except TypeError as e:
    print(one(e))
try:
    ExceptionGroup(5, [a])
except TypeError as e:
    print(one(e))

# split and subgroup
match, rest = eg.split(A)
print(*names(match), *names(rest))
match, rest = eg.split((B, C))
print(*names(match), *names(rest))
match, rest = eg.split(KeyError)
check('no match', match is None and rest.message == 'grp' and rest is not eg)
everything, nothing = eg.split(Exception)
check('all match', everything is eg and nothing is None)
print(*names(eg.subgroup(C)))
check('subgroup none', eg.subgroup(KeyError) is None)
match, rest = eg.split(lambda e: type(e) is B)
print(*names(match), *names(rest))
check('metadata', match.message == 'grp' and rest.__traceback__ is eg.__traceback__)

# nested groups keep their shape
inner = ExceptionGroup('inner', [B('b2'), C('c2')])
outer = ExceptionGroup('outer', [A('a2'), inner])
match, rest = outer.split(B)
only, = match.exceptions
print(match.message, only.message)
check('rest shape', rest.message == 'outer')


# derive is used to build the parts of a subclass
class Mine(ExceptionGroup):
    def derive(self, excs):
        return Mine(self.message, excs)


match, rest = Mine('mine', [a, b]).split(A)
check('derive', type(match) is Mine and type(rest) is Mine)

# except*
def star(group):
    try:
        raise group
    except* A as e:
        print('A:', *names(e), type(e).__name__)
    except* B as e:
        print('B:', *names(e))


star(ExceptionGroup('g', [A('x'), B('y'), C('z')]))
star(ExceptionGroup('g', [B('y')]))

# a naked exception is wrapped
try:
    try:
        raise A('naked')
    except* A as e:
        wrapped, = names(e)
        print(type(e).__name__, wrapped)
except Exception:
    print('not reached')

# what no clause handles is raised again as a group
try:
    try:
        raise ExceptionGroup('g', [A('a'), B('b')])
    except* A:
        print('handled A')
except ExceptionGroup as rest:
    print('left over', *names(rest))

# raising inside except* groups the new exception with what was left
try:
    try:
        raise ExceptionGroup('g', [A('a'), B('b')])
    except* A:
        raise KeyError('new')
except ExceptionGroup as combined:
    print('combined', *names(combined))

# bare raise inside except* reraises the matched part
try:
    try:
        raise ExceptionGroup('g', [A('a'), B('b')])
    except* A:
        raise
except ExceptionGroup as again:
    print('reraised', *names(again))

# except* with a tuple, and the invalid forms
try:
    raise ExceptionGroup('g', [A('a'), B('b')])
except* (A, B) as e:
    print('both', *names(e))
for bad in (ExceptionGroup, 5):
    try:
        try:
            raise ExceptionGroup('g', [A('a')])
        except* bad:
            pass
    except TypeError as e:
        print(one(e))
