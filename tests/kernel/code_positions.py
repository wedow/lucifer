import sys


def sample(a, b):
    x = a + \
        b
    for i in range(3):
        x += (i *
              2)
    return [x,
            (a, b)][0] if x else None


def nested():
    def inner(q):
        try:
            return q.attr(1, 2)[3]
        except KeyError as e:
            raise ValueError from e
    return inner


for fn in (sample, nested, nested(), lambda: 1, sys.getrecursionlimit if False else sample):
    code = fn.__code__
    print(code.co_name, list(code.co_positions()), list(code.co_lines()))
print(type.__flags__ == type(type).__flags__, bool(int.__flags__ & (1 << 24)), object.__basicsize__, int.__itemsize__,
      bool.__basicsize__ >= 0, type.__dictoffset__ > 0, type.__weakrefoffset__ > 0)


class C:
    pass


class D(C):
    __slots__ = ('a', 'b')


print(C.__basicsize__ >= 16, D.__basicsize__ > object.__basicsize__, C.__dictoffset__ != 0, D.__dictoffset__, C.__flags__ & (1 << 9) != 0,
      C.__type_params__, C.__text_signature__)
C.__type_params__ = (1,)
print(C.__type_params__)
import abc


class A(abc.ABC):
    @abc.abstractmethod
    def f(self): ...


print(A.__flags__ & (1 << 20) != 0, C.__flags__ & (1 << 20) != 0, object().__sizeof__() > 0, C().__sizeof__() > 0)
