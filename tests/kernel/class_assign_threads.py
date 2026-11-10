# __class__ assignment from several threads at once: each swap takes the
# old class's reference exactly once, so both classes stay alive and whole.
import gc
import threading


class Foo:
    pass


class Bar:
    pass


thing = Foo()
errors = []


def work():
    foo = thing
    try:
        for _ in range(2000):
            foo.__class__ = Bar
            type(foo)
            foo.__class__ = Foo
            type(foo)
    except Exception as e:
        errors.append(e)


threads = [threading.Thread(target=work) for _ in range(6)]
for t in threads:
    t.start()
for t in threads:
    t.join()
thing.__class__ = Foo
gc.collect()
print(errors, Foo.__name__, Bar.__name__, type(thing).__name__, Foo.__mro__, Bar.__mro__)
