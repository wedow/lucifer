"""builtins functions, over the _lucifer primitives."""

from _lucifer import write, str_join

__all__ = ('print',)


def print(*args, sep=' ', end='\n'):
    write(1, str_join(sep, args))
    write(1, end)
