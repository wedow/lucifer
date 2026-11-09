# type.__new__ takes a dict subclass as the namespace, through a metaclass's
# super().__new__ as well (pydantic's ModelMetaclass passes one to ABCMeta).
import abc


class Namespace(dict):
    pass


class Meta(abc.ABCMeta):
    def __new__(mcs, name, bases, ns, **kwargs):
        return super().__new__(mcs, name, bases, Namespace(ns), **kwargs)


class A(metaclass=Meta):
    x = 1


print(A.x, type(A.__dict__).__name__, type(A).__name__)
print(type('B', (), Namespace(y=2)).y)
