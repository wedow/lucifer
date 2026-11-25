# A hot loop calling a staticmethod and a classmethod (which reads a class
# attribute through cls) through their class and a subclass, and through
# an instance (one with no dict: with a dict, the load tests that the
# instance does not shadow the name, as any method load from one does).
# tools/jitcheck (lane classcalls) runs it at two sizes and requires that
# compiled code calls no helper per iteration: each load binds the
# callable at compile time and each call inlines.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class M:
    __slots__ = ()
    k = 3

    @staticmethod
    def st(a):
        return a + 1

    @classmethod
    def cm(cls, a):
        return a + cls.k


class S(M):
    __slots__ = ()
    k = 4


def loop(n):
    s = 0
    for i in range(n):
        s += M.st(i) + M.cm(i) + S.st(i) + S.cm(i)
    return s


def by_instance(n, o):
    s = 0
    for i in range(n):
        s += o.st(i) + o.cm(i)
    return s


print(loop(N), by_instance(N, S()))
