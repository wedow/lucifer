"""str joins of lists and tuples (empty, one item, a str subclass item,
mixed widths, a non-str item's error) and concatenations of every width
pair, in hot loops whose operands the tier does not know."""
from support import loops


class S(str):
    pass


def run():
    words = ['a', 'é', '€', '𝄞', '', 'plain']
    out = []
    for i in range(300):
        a, b = words[i % 6], words[(i * 5) % 6]
        out.append(a + b)
        out.append('-'.join(words[i % 6:]))
        out.append(''.join([words[i % 6]]))
        out.append(','.join((a, b)))
    res = [len(out), out[:24], type(''.join([S('x')])).__name__, ''.join([S('x')]), '|'.join([]), ''.join(())]
    try:
        ''.join(['a', b'b'])
    except TypeError as e:
        res.append(str(e))
    return res


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
