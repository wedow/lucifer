from types import SimpleNamespace


def show(call):
    try:
        print(call())
    except (KeyError, TypeError) as exc:
        print(type(exc).__name__, str(exc))


# The receiver's name can be a format field, including in a nested spec.
show(lambda: '{self}'.format(self=1))
show(lambda: '{0:{self}}'.format(2, self='04d'))
show(lambda: str.format('{self} {other}', **{'self': 3, 'other': 4}))
show(lambda: '{missing}'.format(self=5))

# Other builtins store or forward arbitrary keywords.
data = {}
show(lambda: data.update(self=6))
print(data)
show(lambda: dict[str, int](self=7))
show(lambda: staticmethod(dict)(self=8))
show(lambda: SimpleNamespace().__replace__(self=9))
