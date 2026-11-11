---
id: luc-tch7
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Builtins written in Python report Python-signature argument errors instead of CPython's C messages

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): mathint_float_diff fns m.log, m.remainder, m.atan2 (37 cases), typeobj_exc_diff exception methods (about 210 cases), typeobj_diff builtin constructors (about 40 cases) over seeds 1 and 2 at 1,000 cases each.

```python
import math
for f, args in ((math.log, ()), (math.log, (1, 2, 3)), (math.remainder, ()), (math.atan2, (1.0,)), (math.fmod, (1.0,)), (math.copysign, ()), (math.hypot, ()), (math.pow, (1,))):
    try: f(*args)
    except Exception as e: print(f.__name__, len(args), type(e).__name__, e)
```
CPython 3.14t:
```
log 0 TypeError log expected at least 1 argument, got 0
log 3 TypeError log expected at most 2 arguments, got 3
remainder 0 TypeError remainder expected 2 arguments, got 0
atan2 1 TypeError atan2 expected 2 arguments, got 1
fmod 1 TypeError fmod expected 2 arguments, got 1
copysign 0 TypeError copysign expected 2 arguments, got 0
pow 1 TypeError pow expected 2 arguments, got 1
```
Lucifer:
```
log 0 TypeError log() missing 1 required positional argument: 'x'
log 3 TypeError log() takes from 1 to 2 positional arguments but 3 were given
remainder 0 TypeError remainder() missing 2 required positional arguments: 'x' and 'y'
atan2 1 TypeError atan2() missing 1 required positional argument: 'y'
fmod 1 TypeError fmod() missing 1 required positional argument: 'y'
copysign 0 TypeError copysign() missing 2 required positional arguments: 'x' and 'y'
pow 1 TypeError pow() missing 1 required positional argument: 'y'
```

```python
def t(f, *a):
    try: print(f(*a))
    except Exception as e: print(type(e).__name__, e)
e = ValueError()
t(e.add_note); t(e.add_note, 1); t(e.add_note, None)
t(e.with_traceback); t(e.__setstate__); t(e.__reduce__, 1)
g = BaseExceptionGroup('m', [ValueError()])
t(g.split); t(g.subgroup, 1, 2); t(g.split, 1, 2)
```
CPython 3.14t:
```
TypeError ValueError.add_note() takes exactly one argument (0 given)
TypeError add_note() argument must be str, not int
TypeError add_note() argument must be str, not None
TypeError ValueError.with_traceback() takes exactly one argument (0 given)
TypeError ValueError.__setstate__() takes exactly one argument (0 given)
TypeError ValueError.__reduce__() takes no arguments (1 given)
TypeError ExceptionGroup.split() takes exactly one argument (0 given)
TypeError ExceptionGroup.subgroup() takes exactly one argument (2 given)
TypeError ExceptionGroup.split() takes exactly one argument (2 given)
```
Lucifer:
```
TypeError BaseException.add_note() missing 1 required positional argument: 'note'
TypeError note must be a str, not 'int'
TypeError note must be a str, not 'NoneType'
TypeError BaseException.with_traceback() missing 1 required positional argument: 'tb'
TypeError BaseException.__setstate__() missing 1 required positional argument: 'state'
TypeError BaseException.__reduce__() takes 1 positional argument but 2 were given
TypeError BaseExceptionGroup.split() missing 1 required positional argument: 'matcher'
TypeError BaseExceptionGroup.subgroup() takes 2 positional arguments but 3 were given
TypeError BaseExceptionGroup.split() takes 2 positional arguments but 3 were given
```

```python
import types
def t(f, *a, **k):
    try: print(f(*a, **k))
    except Exception as e: print(type(e).__name__, e)
t(types.MethodType, 1)
t(staticmethod)
t(classmethod)
t(super, 1, 2, 3)
t(int.__add__.__get__)
t(object.__str__.__get__)
t(property().__get__)
```
CPython 3.14t:
```
TypeError method expected 2 arguments, got 1
TypeError staticmethod expected 1 argument, got 0
TypeError classmethod expected 1 argument, got 0
TypeError super() expected at most 2 arguments, got 3
TypeError __get__ expected at least 1 argument, got 0
TypeError __get__ expected at least 1 argument, got 0
TypeError __get__ expected at least 1 argument, got 0
```
Lucifer:
```
TypeError method.__new__() missing 1 required positional argument: 'instance'
TypeError staticmethod.__init__() missing 1 required positional argument: 'f'
TypeError classmethod.__init__() missing 1 required positional argument: 'f'
TypeError super.__init__() takes from 1 to 3 positional arguments but 4 were given
TypeError wrapper_descriptor.__get__() missing 1 required positional argument: 'instance'
TypeError wrapper_descriptor.__get__() missing 1 required positional argument: 'instance'
TypeError property.__get__() missing 1 required positional argument: 'instance'
```

One mechanism: math functions, BaseException methods and builtin type constructors are Python functions, so arity and type errors use the "missing 1 required positional argument" form and the defining class name.


