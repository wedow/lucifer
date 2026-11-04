import _operator
import operator

print(_operator.add(1, 2), _operator.itemgetter(1)([5, 6]), _operator.mul is operator.mul or _operator.mul(2, 3))
print(sorted(set(operator.__all__) - set(dir(_operator))), hasattr(_operator, '_compare_digest'))
print(hasattr(operator, 'operator'), hasattr(_operator, 'operator'))
d = _operator._compare_digest
print(d(b'abc', b'abc'), d(b'abc', b'abd'), d(b'abc', b'ab'), d('abc', 'abc'), d(bytearray(b'x'), b'x'), d(b'', b''))
for args in ((1, 2), ('é', 'é'), ('a', b'a')):
    try:
        print(d(*args))
    except TypeError as e:
        print(type(e).__name__, e)
