class Slotted:
    __slots__ = ()
    readonly = 1


class CustomDelete(Slotted):
    __slots__ = ()

    def __delattr__(self, name):
        object.__delattr__(self, name)


def delete(obj):
    del obj.missing


def report(obj, operation):
    try:
        operation(obj)
    except AttributeError as exc:
        print(type(exc).__name__, str(exc), exc.name, exc.obj is obj)


for obj in (1, Slotted(), CustomDelete()):
    report(obj, delete)
    report(obj, lambda obj: delattr(obj, 'missing'))
    report(obj, lambda obj: setattr(obj, 'missing', 1))

report(Slotted(), lambda obj: delattr(obj, 'readonly'))
