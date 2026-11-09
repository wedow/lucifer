# Dict keys that keep object's __hash__ and __eq__ hash and compare by
# identity; a class that gains __eq__ or __hash__ later is asked again.
class Plain:
    pass

class Equal:
    def __init__(self, k):
        self.k = k
    def __eq__(self, other):
        return isinstance(other, (Equal, Plain))
    __hash__ = object.__hash__

def run():
    a, b = Plain(), Plain()
    d = {a: 1, 'a': 2, 3: 3}
    out = [d[a], d.get(b), a in d, b in d, d.get('a')]
    d[b] = 4
    del d[a]
    out += [len(d), d.get(a, 'gone'), d[b]]
    e = Equal(1)
    d2 = {e: 'e'}
    out += [d2.get(Equal(2)), d2.get(a)]
    Plain.__eq__ = lambda self, other: True
    Plain.__hash__ = lambda self: 0
    q, r = Plain(), Plain()
    d3 = {q: 'q'}
    out += [d3.get(r), r in d3, len({q, r})]
    del Plain.__eq__, Plain.__hash__
    return out

for i in range(200):
    r = run()
print(r)
try:
    {[]: 1}
except TypeError as e:
    print('TypeError', e)
