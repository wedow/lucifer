for s in ('', 'abc', '\xe9\xe9\xe9', '€', '\U0001f600', '\U0001f600' * 2, 'x' * 100):
    print(s.__sizeof__())
class S(str):
    pass
print('abc'.__rmod__(1) is NotImplemented, S('abc').__sizeof__() > 'abc'.__sizeof__())
