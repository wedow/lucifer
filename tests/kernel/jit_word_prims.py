# luc-tax0: kernel dict and set code passes hashes, entry numbers, steps
# and stamps to its primitives as machine words, and takes str hashes and
# dict stamps back as words, without boxing them. Every probe outcome must
# still be the interpreter's: hits and misses of str, int and tuple keys
# whose hashes span the word (negative, -2, past 2**62), candidates that
# need __eq__ (equal hashes of keys that are not equal), an __eq__ that
# changes the dict or set it probes (the lookup starts over), dict and set
# subclasses, and str subclasses as keys.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 400


class Same:
    """Keys that all hash alike: every probe meets candidates."""

    def __init__(self, v):
        self.v = v

    def __hash__(self):
        return 7

    def __eq__(self, other):
        return isinstance(other, Same) and self.v == other.v


class Big(int):
    pass


class Mutating:
    """An __eq__ that adds to its container the first time it runs."""

    def __init__(self, box):
        self.box = box
        self.done = False

    def __hash__(self):
        return 7

    def __eq__(self, other):
        if not self.done:
            self.done = True
            target = self.box[0]
            if isinstance(target, dict):
                target['grown'] = 1
            else:
                target.add('grown')
        return self is other


class S(str):
    pass


class D(dict):
    pass


class FS(frozenset):
    pass


def keys():
    ks = ['a', 'bb', 'ccc', S('dd'), 'é', '\U0001f600', '']
    ks += [0, 1, -1, -2, 2 ** 62, -(2 ** 62), 2 ** 63 - 1, 2 ** 100, Big(5), True]
    ks += [(1, 'a'), (2 ** 70, -2), ('x', ('y', 3))]
    ks += [Same(i) for i in range(5)]
    return ks


def run(n):
    ks = keys()
    out = []
    for i in range(n):
        d = D() if i % 3 == 0 else {}
        s = set() if i % 2 else FS(ks[:4])
        for j, k in enumerate(ks):
            if (i + j) % 4:
                d[k] = j
                if isinstance(s, set):
                    s.add(k)
        hits = 0
        for k in ks:
            hits += k in d
            hits += k in s
            hits += d.get(k, -100) >= 0
        for k in ks[::3]:
            d.pop(k, None)
            if isinstance(s, set):
                s.discard(k)
        d.setdefault('ccc', 99)
        # entry hashes taken back from the table: the set algebra and copies
        t = set(ks[i % 5::2])
        u = set(s)
        alg = (len(u - t), len(t - u), len(u & t), len(u ^ t), len(u | t), u <= t | u, t.isdisjoint(u))
        u -= t
        e = dict(d)
        e.update({k: 0 for k in ks[::5]})
        alg += (len(u), len(e), sum(k in e for k in d), len({**d, **e}))
        out.append((hits, len(d), len(s), sorted(map(repr, d))[:3], d.get('ccc'), alg))
    return out


def mutating():
    out = []
    for kind in (dict, set):
        c = kind()
        box = [c]
        a, b = Mutating(box), Mutating(box)
        if kind is dict:
            c[a] = 1
            out.append((b in c, 'grown' in c, len(c), c.get(a)))
        else:
            c.add(a)
            out.append((b in c, 'grown' in c, len(c)))
    return out


res = run(N)
print(len(res), res[0], res[1], res[2], res[-1])
print(sum(r[0] for r in res), sum(r[1] for r in res), [sum(r[5][j] for r in res) for j in range(11)])
print(mutating())
print(sorted(hash(k) == hash(k) for k in keys()) == [True] * len(keys()))
