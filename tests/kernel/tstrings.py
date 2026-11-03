# t-strings: Template and Interpolation (the types string.templatelib exports).
name = "world"
width = 8
value = 3.14159
t = t"hello {name}!"
Template = type(t)
Interpolation = type(t.interpolations[0])
print(Template.__module__, Template.__name__, Interpolation.__module__, Interpolation.__name__)
print(t)
print(t.strings, t.values)
i = t.interpolations[0]
print(i, i.value, i.expression, i.conversion, repr(i.format_spec))
print(list(t))

u = t"{name!r:>{width}} and {value:.2f} {name!s} {name!a} {'q' + name}"
for part in u:
    if isinstance(part, Interpolation):
        print("I", part.value, part.expression, part.conversion, part.format_spec)
    else:
        print("S", repr(part))
print(u.strings, u.values)
print(list(t"{name}{name}"), list(t""), list(t"{name}"), list(t"a"))
print(t"{name}" + t"x{width}y")

try:
    t + "str"
except TypeError as e:
    print(e)

n = Template("a", Interpolation(1, "x"), "b", "c", Interpolation(2), Interpolation(3, "z", "r", "5"))
print(n)
print(n.strings, n.values)
print(Interpolation(1, "x", None, "") .conversion, Interpolation(1).expression)
for bad in ((lambda: Template(1)), (lambda: Template(a=1)), (lambda: Interpolation(1, 2)),
            (lambda: Interpolation(1, "e", "x")), (lambda: Interpolation(1, "e", 1)),
            (lambda: Interpolation(1, "e", "s", 3)), (lambda: Interpolation())):
    try:
        bad()
    except (TypeError, ValueError) as e:
        print(type(e).__name__, e)

match i:
    case Interpolation(v, e):
        print("match", v, e)
print(Interpolation(1, "x", "s", "y").__reduce__()[1])
print(Template[int], Interpolation[str])
print(t"{name}".strings == ("", ""), t"{{braces}} {name}".strings)
print(t"""multi
{name}
line""")
print(rf"{name}" , tr"\d{name}".strings)


class Init:
    def __init__(self):
        return 1


try:
    Init()
except TypeError as e:
    print(e)
