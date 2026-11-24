"""Loads of globals and builtins from hot loops while the dicts behind them
change mid-loop: a global deleted and re-added, a builtin shadowed by a
global and unshadowed, the module dict growing past several resizes, a
builtin replaced and the builtins dict growing, the same code run under
other globals, and a function whose globals are a dict subclass. Every load
must see the dicts as they are at that iteration."""
import builtins
import types

N = 3000
value = 1


def read_value(n, at, change):
    total = 0
    for i in range(n):
        if i == at:
            change()
        try:
            total += value
        except NameError:
            total += 1000
    return total


def delete_value():
    global value
    del value


def readd_value():
    global value
    value = 7


print('deleted', read_value(N, N // 2, delete_value))
print('readded', read_value(N, N // 3, readd_value))


def read_len(n, at, change):
    total = 0
    for i in range(n):
        if i == at:
            change()
        total += len('abc')
    return total


def shadow_len():
    globals()['len'] = lambda s: 100


def unshadow_len():
    del globals()['len']


print('shadowed', read_len(N, N // 2, shadow_len))
print('unshadowed', read_len(N, N // 2, unshadow_len))


def grow_globals():
    for k in range(600):
        globals()['grown_%d' % k] = k


def read_both(n, at, change):
    total = 0
    for i in range(n):
        if i == at:
            change()
        total += value + abs(-2)
    return total


print('grown', read_both(N, N // 2, grow_globals), grown_599)

saved_abs = builtins.abs


def replace_abs():
    builtins.abs = lambda x: 50


def restore_abs():
    builtins.abs = saved_abs


def grow_builtins():
    for k in range(300):
        setattr(builtins, 'guard_grown_%d' % k, k)


print('replaced', read_both(N, N // 2, replace_abs))
print('restored', read_both(N, N // 2, restore_abs))
print('builtins grown', read_both(N, N // 2, grow_builtins), guard_grown_299)
for k in range(300):
    delattr(builtins, 'guard_grown_%d' % k)
print('builtins shrunk', read_both(N, N // 2, lambda: None))


def load_pair():
    return value, abs(-3)


other = {'__builtins__': builtins, 'value': 'other', 'abs': lambda x: 'own abs'}
other_load = types.FunctionType(load_pair.__code__, other)
seen = set()
for i in range(N):
    seen.add(load_pair())
    seen.add(other_load())
    if i == N // 2:
        other['value'] = 'changed'
        del other['abs']
print('shared code', sorted(seen, key=repr))


class Globals(dict):
    pass


sub = Globals(__builtins__=builtins, value=5)
sub_load = types.FunctionType(load_pair.__code__, sub)
seen = set()
for i in range(N):
    seen.add(sub_load())
    if i == N // 2:
        sub['value'] = 6
print('dict subclass', sorted(seen))


# The globals turned into a general-keys table (a non-str key) mid-loop.
def add_int_key():
    globals()[1] = 'int key'


print('general keys', read_both(N, N // 2, add_int_key), globals()[1])
del globals()[1]
print('int key gone', read_both(N, N // 2, lambda: None))

LOOP = '''
def loop(n, at, change):
    total = 0
    for i in range(n):
        if i == at:
            change()
        total += len('abc')
    return total
'''


# A dict subclass as builtins: its __getitem__ answers.
class Builtins(dict):
    def __getitem__(self, key):
        if key == 'len':
            return lambda s: 10
        return dict.__getitem__(self, key)


ns = {'__builtins__': Builtins(vars(builtins))}
exec(LOOP, ns)
print('builtins subclass', ns['loop'](N, N // 2, lambda: None))

# exec with a builtins dict of its own, changed mid-loop.
own = {'range': range, 'len': len}
ns = {'__builtins__': own}
exec(LOOP, ns)


def own_len():
    own['len'] = lambda s: 20


print('own builtins', ns['loop'](N, N // 2, own_len))

# __builtins__ rebound: functions made before keep the builtins they were
# made with; functions made after take the new ones.
ns = {'__builtins__': builtins}
exec(LOOP, ns)
before = ns['loop']
ns['__builtins__'] = {'range': range, 'len': lambda s: 30}
print('rebound, made before', before(N, N // 2, lambda: None))
exec(LOOP, ns)
print('rebound, made after', ns['loop'](N, N // 2, lambda: None))
