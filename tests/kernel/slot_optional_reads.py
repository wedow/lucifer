# Optional reads of __slots__ members (getattr with a default, hasattr), the
# misses of which make no AttributeError: unset slots, set ones, slots set and
# deleted while the code is hot, a subclass's slot, a member descriptor
# planted on an unrelated class, and classes with __getattr__ or
# __getattribute__ of their own, which still see the miss.

N = 3000
marker = object()


class S:
    __slots__ = ('a', 'b')


class T(S):
    __slots__ = ('c',)


class Planted:
    a = S.__dict__['a']


class Fallback:
    __slots__ = ('a',)

    def __getattr__(self, name):
        return 'fallback ' + name


class Own:
    __slots__ = ('a',)

    def __getattribute__(self, name):
        if name == 'a':
            raise AttributeError(name)
        return object.__getattribute__(self, name)


def peek(objs, name, n):
    seen = []
    for i in range(n):
        v = getattr(objs[i % len(objs)], name, marker)
        seen.append('miss' if v is marker else v)
    return seen


s, t = S(), T()
t.a = 'ta'
print(1, sorted(set(peek([s, t], 'a', N))))
print(1, hasattr(s, 'a'), hasattr(t, 'a'), hasattr(s, 'c'), hasattr(t, 'c'))

s.a = 'sa'
print(2, peek([s, t], 'a', 4))
del s.a
del t.a
print(2, peek([s, t], 'a', 4))
t.c = 'tc'
print(2, peek([t], 'c', 2), peek([s], 'c', 2))

try:
    getattr(Planted(), 'a', marker)
except TypeError as e:
    print(3, type(e).__name__, e)

print(4, peek([Fallback()], 'a', 2))
print(4, peek([Own()], 'a', 2))
o = Own()
object.__setattr__(o, 'a', 'set')
print(4, peek([o], 'a', 2))
