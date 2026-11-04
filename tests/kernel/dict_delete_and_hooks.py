# __dict__ of functions and exceptions cannot be deleted; a class instance's can.
import sys


def f():
    pass


for o in (f, ValueError()):
    try:
        del o.__dict__
        print("deleted", type(o).__name__)
    except TypeError as e:
        print("TypeError", e)


class C:
    pass


c = C()
c.x = 1
del c.__dict__
print(c.__dict__)

# an expression statement in interactive code with sys.displayhook gone
saved = sys.displayhook
del sys.displayhook
try:
    exec(compile('1', '<s>', 'single'))
except Exception as e:
    print(type(e).__name__, e)
sys.displayhook = saved

# function(code, globals, ...) argument checks
F = type(f)
for args in [(1, {}), (f.__code__, 1), (f.__code__, {}, 1), (f.__code__, {}, None, 1)]:
    try:
        F(*args)
    except Exception as e:
        print(type(e).__name__, e)
