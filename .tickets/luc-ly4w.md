---
id: luc-ly4w
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# math: argument conversion differs from PyFloat_AsDouble (__index__ results, __float__ on int subclasses, argument order)

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): mathint_float_diff fns m.tanh, m.fma, m.copysign, m.fsum, m.modf, m.fmod, m.nextafter (11 cases), binary64_diff fn o_asdouble (2 cases) over seeds 1 and 2 at 1,000 cases each.

```python
import math
class F:
    def __index__(self): return 1.5
class T:
    def __index__(self): return object()
for name, f in (("tanh", lambda o: math.tanh(o)), ("copysign", lambda o: math.copysign(o, 1.0)), ("fsum", lambda o: math.fsum([o])), ("modf", math.modf)):
    for cls in (F, T):
        try: print(name, cls.__name__, f(cls()))
        except Exception as e: print(name, cls.__name__, type(e).__name__, e)
```
CPython 3.14t:
```
tanh F TypeError __index__ returned non-int (type float)
tanh T TypeError __index__ returned non-int (type object)
copysign F TypeError __index__ returned non-int (type float)
copysign T TypeError __index__ returned non-int (type object)
fsum F TypeError __index__ returned non-int (type float)
fsum T TypeError __index__ returned non-int (type object)
modf F TypeError __index__ returned non-int (type float)
modf T TypeError __index__ returned non-int (type object)
```
Lucifer:
```
tanh F 0.9051482536448664
tanh T TypeError must be real number, not NoneType
copysign F 1.5
copysign T TypeError must be real number, not NoneType
fsum F 1.5
fsum T TypeError unsupported operand type(s) for -: 'NoneType' and 'NoneType'
modf F (0.5, 1.0)
modf T TypeError must be real number, not NoneType
```

```python
import math
class IS(int):
    def __float__(self): return 7   # int, not float
try: print(float(IS(5)))
except Exception as e: print(type(e).__name__, e)
try: print(math.nextafter(IS(5), IS(5)))
except Exception as e: print(type(e).__name__, e)
class J(int):
    def __float__(self): return J(3)
try: print(math.nextafter(J(9007199254740992), J(9007199254740992)))
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError IS.__float__ returned non-float (type int)
TypeError IS.__float__ returned non-float (type int)
TypeError J.__float__ returned non-float (type J)
```
Lucifer:
```
TypeError IS.__float__ returned non-float (type int)
5.0
9007199254740992.0
```

```python
import math
class BadIndex:
    def __index__(self): return 1.5
class BadFloat:
    def __float__(self): return 3
try: math.fmod(BadIndex(), BadFloat())
except Exception as e: print("fmod", type(e).__name__, e)
try: math.nextafter(BadIndex(), 1.0, steps=-5)
except Exception as e: print("nextafter", type(e).__name__, e)
```
CPython 3.14t:
```
fmod TypeError __index__ returned non-int (type float)
nextafter TypeError __index__ returned non-int (type float)
```
Lucifer:
```
fmod TypeError BadFloat.__float__ returned non-float (type int)
nextafter ValueError steps must be a non-negative integer
```

lib/modules/math.py _to_double: __index__'s result is not checked to be an int, int subclasses skip __float__, and fmod/nextafter convert y (or steps) before x.


