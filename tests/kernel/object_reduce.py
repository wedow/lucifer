# object.__reduce__, __reduce_ex__ and __getstate__: what pickle and copy ask of an instance.
import copyreg


class P:
    pass


p = P()
print(p.__getstate__(), p.__reduce_ex__(2)[0] is copyreg.__newobj__, p.__reduce_ex__(2)[1] == (P,), p.__reduce_ex__(2)[2:])
p.a = 1
p.b = [2]
print(p.__getstate__(), p.__reduce_ex__(4)[2], p.__reduce_ex__(2)[3:])
r = p.__reduce_ex__(1)
print(r[0] is copyreg._reconstructor, r[1][1] is object, r[1][2], r[2])
print(p.__reduce__()[0] is copyreg._reconstructor)


class S:
    __slots__ = ('x', 'y')

    def __init__(self):
        self.x = 1


s = S()
print(s.__getstate__(), s.__reduce_ex__(2)[2])


class N:
    def __getnewargs__(self):
        return (1, 2)


print(N().__reduce_ex__(2)[1][1:], N().__reduce_ex__(2)[2])


class NX:
    def __getnewargs_ex__(self):
        return ((1,), {'k': 2})


r = NX().__reduce_ex__(2)
print(r[0] is copyreg.__newobj_ex__, r[1][1:])


class L(list):
    pass


class D(dict):
    pass


l = L([1, 2])
d = D(a=1)
print(list(l.__reduce_ex__(2)[3]), l.__reduce_ex__(2)[4], l.__reduce_ex__(2)[2])
print(d.__reduce_ex__(2)[3], list(d.__reduce_ex__(2)[4]))


class O:
    def __reduce__(self):
        return ('custom',)


print(O().__reduce_ex__(2), O().__reduce_ex__(0))


class G:
    def __getstate__(self):
        return {'g': 1}


print(G().__reduce_ex__(2)[2])


class Bad:
    def __getnewargs__(self):
        return 5


try:
    Bad().__reduce_ex__(2)
except TypeError as e:
    print(e)
print(object().__reduce_ex__(2)[1], object.__reduce_ex__ is not None)
