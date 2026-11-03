# f-strings: FORMAT_SIMPLE, FORMAT_WITH_SPEC, CONVERT_VALUE and BUILD_STRING.
name = 'world'
n = 42
neg = -17
big = 2 ** 70
print(f'hello {name}!', f'{n}', f'{n:5}|', f'{n:<5}|', f'{n:^5}|', f'{n:05}', f'{neg:+}', f'{n:+}', f'{n: }')
print(f'{n:x}', f'{n:X}', f'{n:#x}', f'{n:#X}', f'{n:o}', f'{n:#o}', f'{n:b}', f'{n:#b}', f'{n:08b}', f'{neg:#010x}')
print(f'{big:,}', f'{big:_}', f'{n:,}', f'{1234567:,}', f'{1234567:_}', f'{1234567:_x}', f'{255:_b}', f'{1234567:012,}', f'{-1234567:012,}')
print(f'{name!r}', f'{name!s}', f'{name!a}', f'{"caf\xe9"!a}', f'{"caf\xe9"!r}', f'{name!r:>10}', f'{name!s:*^11}')
print(f'{name}{n}{neg}', f'{name:>10}', f'{name:<10}|', f'{name:^10}|', f'{name:*^10}', f'{name:.3}', f'{name:>10.3}', f'{name:s}')
width = 8
prec = 2
print(f'{name:{width}}|', f'{name:>{width}}|', f'{name:{width}.{prec}}|', f'{n:{width}x}|', f'{n:0{width}d}')
print(f'{{literal}}', f'{{{n}}}', f'a{n}b{n}c', f'', f'{n}{n}{n}', f'{n!r}', f'{True}', f'{None}', f'{True:d}', f'{False:5}|', f'{True:>6}|')
print(f'{n=}', f'{n = }', f'{name=!r}', f'{n=:>6}', f'{n + 1=}')
print(f"{'nested' + f'{n}'}", f'{n:{"<"}{6}}|', f'{(lambda: 5)():3}', f'{name.upper()}', f'{n * 2 + 1}')
print(f'{"a" "b"}', f'{"x"!r}', f'{n:c}', f'{97:c}', f'{0x20ac:c}')
print(f"""multi
{name}
line""", f'{name}\n{name}', rf'\{name}')

class Spec:
    def __format__(self, spec):
        return '<' + spec + '>'
    def __repr__(self):
        return 'Spec()'
    def __str__(self):
        return 'SpecStr'
sp = Spec()
print(f'{sp}', f'{sp:abc}', f'{sp!r}', f'{sp!s:>9}', f'{sp:{width}}', f'{sp!a}')

class Plain:
    pass
p = Plain()
print(f'{p}'.startswith('<__main__.Plain object at '), f'{p!s}'.startswith('<__main__.Plain object'), f'{p:}'.endswith('>'))

class Sub(str):
    def __str__(self):
        return 'sub-str'
class SubInt(int):
    def __str__(self):
        return 'sub-int'
print(f'{Sub("x")}', f'{SubInt(3)}', f'{SubInt(3):}', f'{SubInt(3):4}|', f'{Sub("x"):>4}|', f'{Sub("x")!s}')

def f(x, y=3):
    return f'{x}-{y}:{x * y:3}'
print(f(2), f(2, 4), f('ab', 2))
total = ''
for i in (1, 2, 3):
    total += f'[{i}]'
print(total)
