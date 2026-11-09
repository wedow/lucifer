# isinstance() against a runtime-checkable protocol with data members goes
# through abc's subclass check, which typing allows; issubclass() does not.
import abc
from typing import Protocol, runtime_checkable


@runtime_checkable
class HasX(Protocol):
    x: int


class Nominal(HasX):
    def __init__(self):
        self.x = 1


class Structural:
    x = 2


class Empty:
    pass


for obj in (Nominal(), Structural(), Empty()):
    print(type(obj).__name__, isinstance(obj, HasX))
try:
    issubclass(Structural, HasX)
except TypeError as e:
    print(e)


class Base(abc.ABC):
    @abc.abstractmethod
    def f(self): ...


class Impl(Base):
    def f(self):
        return 1


Base.register(int)
print(sorted(Base.__abstractmethods__), Impl.__abstractmethods__, issubclass(int, Base), isinstance(3, Base))
print(isinstance(abc.get_cache_token(), int), [len(s) for s in Base._abc_impl and abc._get_dump(Base)[:3]])
try:
    Base()
except TypeError as e:
    print(e)
