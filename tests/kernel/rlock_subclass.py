from _thread import RLock


class NamedLock(RLock):
    def __init__(self, name):
        self.name = name

    def release(self):
        raise AssertionError('context exit called the release override')


def exercise():
    lock = NamedLock(name='example')
    other = NamedLock('other')
    print(type(lock) is NamedLock, isinstance(lock, RLock), lock.name)
    print(repr(lock).split(' object ')[0])
    with lock:
        with lock:
            print(lock._recursion_count(), other.locked())
        print(lock._recursion_count())
    print(lock.locked())


exercise()
