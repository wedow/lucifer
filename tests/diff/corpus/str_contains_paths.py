"""`in` and `not in` between strs in hot loops (every width pair, empty
needles and haystacks, needles wider than the text), with a str subclass on
either side and the error for a non-str needle."""
from support import loops


class S(str):
    pass


def run():
    hay = ['plain text', 'café', 'a € sign', 'clef 𝄞 here', '']
    needles = ['text', 'é', '€', '𝄞', '', 'zz', 'a ']
    hits = 0
    for i in range(400):
        h = hay[i % 5]
        n = needles[i % 7]
        hits += (n in h) + (n not in h) * 2
    out = [hits, [(n in h) for h in hay for n in needles], S('a') in 'cat', 'a' in S('cat')]
    for bad in (1, None, b'a'):
        try:
            bad in 'abc'
        except TypeError as e:
            out.append(str(e))
    return out


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
