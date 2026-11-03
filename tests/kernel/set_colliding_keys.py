# Keys whose hashes all collide and whose __eq__ runs Python code: lookups end.
calls = 0


class Key:
    def __init__(self, n):
        self.n = n

    def __hash__(self):
        return self.n % 2

    def __eq__(self, other):
        global calls
        calls += 1
        return isinstance(other, Key) and other.n == self.n


keys = [Key(n) for n in range(60)]
s = set(keys)
print(len(s), all(k in s for k in keys), Key(3) in s, Key(100) in s, len(s & set(keys[:20])), len(s - set(keys[:20])), len(s | {Key(200)}))
s.discard(keys[7])
print(len(s), keys[7] in s, len(s ^ set(keys[:10])))
d = dict.fromkeys(keys)
print(len(d), keys[5] in d, Key(99) in d)
