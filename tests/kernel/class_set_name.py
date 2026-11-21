events = []


class Hook:
    def __init__(self, label):
        self.label = label

    def __set_name__(self, owner, name):
        events.append((self.label, owner.__name__, name))
        if self.label == 'first':
            del owner.second
            owner.added = Hook('added')


class Base:
    def __init_subclass__(cls, *, tag):
        events.append((tag, cls.__name__, hasattr(cls, 'added')))


class Child(Base, tag='init'):
    first = Hook('first')
    second = Hook('second')


print(events)


class StaticHook:
    @staticmethod
    def __set_name__(owner, name):
        print('static', owner.__name__, name)


class StaticOwner:
    item = StaticHook()


failure = ValueError('hook failed')
failure.add_note('existing note')


class BrokenHook:
    def __set_name__(self, owner, name):
        raise failure


try:
    class BrokenOwner(Base, tag='unexpected'):
        item = BrokenHook()
except ValueError as exc:
    print(exc is failure, str(exc), exc.__notes__)

print(events)


class BrokenBinding:
    def __get__(self, obj, owner):
        raise LookupError('binding failed')


class BindingHook:
    __set_name__ = BrokenBinding()


try:
    class BindingOwner:
        item = BindingHook()
except LookupError as exc:
    print(str(exc), getattr(exc, '__notes__', None))
