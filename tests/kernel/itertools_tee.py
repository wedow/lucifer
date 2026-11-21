import itertools
import threading
import weakref


def show(call, *args, **kwargs):
    try:
        result = call(*args, **kwargs)
    except Exception as exc:
        print(type(exc).__name__, exc)
    else:
        print(type(result).__module__, type(result).__name__)


def weak_reference():
    first, second = itertools.tee([1, 2, 3])
    callbacks = []
    ref = weakref.ref(first, lambda ref: callbacks.append(ref() is None))
    print(ref() is first)
    del first
    print(callbacks, ref() is None, list(second))


show(weak_reference)

data_type = getattr(itertools, '_tee_dataobject', None)
if data_type is None:
    print('missing _tee_dataobject')
else:
    class Values(list):
        def __len__(self):
            raise AssertionError

        def __iter__(self):
            raise AssertionError

    tail = data_type(iter([]), [], None)
    for values, link in (([], None), ([0], None), ([0], 3), ([0], tail),
                         ([0] * 57, tail), ([0] * 57, 3), ([0] * 58, None),
                         ((), None), (Values([0]), None)):
        show(data_type, object(), values, link)
    show(data_type, iter([]), [], next=None)
    show(data_type, iter([]), [])
    show(data_type, iter([]), [], None, None)


def reenter(position):
    class Source:
        def __iter__(self):
            return self

        def __next__(self):
            value = next(values)
            if value == position:
                next(other)
            return value

    values = iter(range(position + 2))
    first, other = itertools.tee(Source())
    for _ in range(position):
        next(first)
        next(other)
    show(next, first)
    print(next(first, None), next(other, None))


reenter(0)
reenter(57)


def clone_branches():
    class Copyable:
        def __iter__(self):
            return self

        def __next__(self):
            return next(values)

        def __copy__(self):
            return 'custom copy'

    values = iter(range(120))
    source = Copyable()
    first, second = itertools.tee(source)
    print(type(first).__name__, type(second).__name__, first is source)
    for _ in range(58):
        next(first)
    wrapped = itertools._tee(first)
    third, fourth = itertools.tee(first)
    print(first is third, [next(it) for it in (third, first, fourth, wrapped)])
    print(sum(second), sum(first), sum(wrapped))
    print(itertools.tee(object(), 0))


show(clone_branches)
show(itertools.tee, object(), 1 << 100)
show(itertools.tee, [], -(1 << 100))


def concurrent_read():
    entered = threading.Event()
    release = threading.Event()

    class Source:
        def __iter__(self):
            return self

        def __next__(self):
            value = next(values)
            if value == 1:
                entered.set()
                release.wait()
            return value

    values = iter(range(4))
    first, second, lagging = itertools.tee(Source(), 3)
    print(next(first), next(second))
    results = []
    worker = threading.Thread(target=lambda: results.append(next(first)))
    worker.start()
    try:
        entered.wait()
        print(next(lagging))
        show(next, second)
    finally:
        release.set()
        worker.join()
    print(results, next(second), next(lagging))


concurrent_read()
