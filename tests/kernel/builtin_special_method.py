# A builtin function stored as a special method gets no self: it is not a
# descriptor (typing.NewType sets __call__ = _typing._idfunc).
import typing


class Call:
    __call__ = len


class Hash:
    __hash__ = id
    __len__ = len


UserId = typing.NewType('UserId', int)
print(UserId(5), type(UserId(5)))
print(Call()([1, 2, 3]))
try:
    hash(Hash())
except TypeError as e:
    print('hash', e)
try:
    len(Hash())
except TypeError as e:
    print('len', e)
