import abc
import collections.abc
import io
import typing
from typing import Generic, TypeVar, Optional, Union, Callable, Any

print(typing.__name__, collections.abc.Sequence.__module__, abc.ABC.__module__, io.StringIO.__module__)
T = TypeVar('T')
K = TypeVar('K', bound=int)
print(T, K, K.__bound__, T.__module__, typing.TypeVar('C', int, str).__constraints__)


class Box(Generic[T]):
    def __init__(self, item):
        self.item = item


b = Box[int](3)
print(b.item, Box.__parameters__, Box[int].__origin__ is Box, Box[int].__args__, b.__orig_class__.__args__)
print(typing.get_origin(typing.List[int]) is list, typing.get_args(typing.Dict[str, int]))
print(typing.get_origin(Optional[int]) is Union, typing.get_args(Optional[int]) == (int, type(None)))
print(typing.get_origin(Callable[[int], str]) is collections.abc.Callable)
print(Union[int, str] == (int | str), Optional[int] == (int | None), hash(Union[int, str]) == hash(int | str))
print(typing.Any, typing.NoReturn)
print(typing.cast(int, '3'), typing.TYPE_CHECKING, isinstance(3, typing.SupportsInt), callable(typing.final))


class Stack[S]:
    def __init__(self):
        self.items: list[S] = []

    def push(self, item: S) -> None:
        self.items.append(item)


s = Stack[int]()
s.push(1)
print(s.items, Stack.__type_params__[0].__name__, Stack.__parameters__[0].__name__, Stack.__mro__[1].__name__)


def first[X: (int, str), *Ts, **P](x: X) -> X:
    return x


print(first(3), [type(p).__name__ for p in first.__type_params__], first.__type_params__[0].__constraints__)
type Alias[U] = list[U] | None
print(Alias.__name__, Alias.__value__ == (list[Alias.__type_params__[0]] | None), Alias.__type_params__[0].__name__)
print(type(Alias).__name__, Alias[int].__origin__ is Alias, Alias.__module__)
