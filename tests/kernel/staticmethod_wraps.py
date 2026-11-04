def f():
    "doc"


f.extra = 1
sm = staticmethod(f)
print(sm.__name__, sm.__qualname__, sm.__module__, sm.__doc__, sm.__dict__, sm.__dict__ is f.__dict__)
cm = classmethod(f)
print(cm.__name__, cm.__wrapped__ is f, cm.__func__ is f, cm.__dict__ == sm.__dict__)


class A:
    @staticmethod
    def g():
        pass

    @classmethod
    def h(cls):
        return cls.__name__


print(A.__dict__['g'].__name__, vars(A.__dict__['g']), A.__dict__['h'].__qualname__, A.h(), A().g.__name__)
try:
    print(staticmethod(5).__name__)
except Exception as e:
    print(type(e).__name__, e)
print('__name__' in vars(staticmethod(5)), sorted(vars(staticmethod(len))))
