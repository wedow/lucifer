# t-strings: BUILD_TEMPLATE, BUILD_INTERPOLATION and the Template and Interpolation types.
from string.templatelib import Template, Interpolation, convert
import pickle

name, width = 'world', 7
t = t"hello {name!r:>{width}} and {width=}"
print(type(t).__module__, type(t).__qualname__, type(t.interpolations[0]).__qualname__)
print(t.strings, t.values)
for i in t.interpolations:
    print(i.value, i.expression, i.conversion, repr(i.format_spec))
print(list(t))
print(repr(t))
print(t == t, hash(t) == hash(t), t == t"x")
print(t"" .strings, t"{1}{2}".strings, list(t"{1}{2}"))
a, b = t"a{1}b", t"c{2}d"
print((a + b).strings, (a + b).values, list(a + b))
try:
    a + 'x'
except TypeError as e:
    print(e)
print(Template('x', Interpolation(3, '3'), 'y', 'z', Interpolation(4), Interpolation(5)).strings)
for bad in ((1,), ('a', None)):
    try:
        Template(*bad)
    except TypeError as e:
        print(e)
try:
    Template(a=1)
except TypeError as e:
    print(e)
i = Interpolation(1, 'x', 'a', '>3')
print(i, i.__match_args__)
print(i.value, i.expression, i.conversion, i.format_spec)
for args in ((1, 2), (1, 'e', 'q'), (1, 'e', 5), (1, 'e', None, 3)):
    try:
        Interpolation(*args)
    except (TypeError, ValueError) as e:
        print(type(e).__name__, e)
for cls in (Template, Interpolation):
    try:
        type('Sub', (cls,), {})
    except TypeError as e:
        print(e)
for v in (t, i):
    c = pickle.loads(pickle.dumps(v, 2))
    print(type(c).__name__, c.values if isinstance(c, Template) else c.value)
print(convert(5, None), convert('a', 'r'), convert('é', 'a'), convert(1, 's'))
try:
    convert(1, 'x')
except ValueError as e:
    print(e)
print(Template[int], Interpolation[int])
