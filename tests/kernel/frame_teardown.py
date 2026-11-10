# A __del__ that runs while a frame is popped sees the stack without that
# frame: a frame object it stashes stays valid after the thread is gone.
import sys
import threading

stash = []


class Sneaky:
    def __del__(self):
        stash.append(sys._getframe())


def run():
    ref = Sneaky()


def teardown_in(thread):
    stash.clear()
    if thread:
        t = threading.Thread(target=run)
        t.start()
        t.join()
    else:
        run()
    f = stash[0]
    names = []
    while f is not None:
        names.append(f.f_code.co_name)
        assert f.f_code is not run.__code__
        f = f.f_back
    print(names[0], 'run' in names, len(names) > 0)


for _ in range(20):
    teardown_in(False)
    teardown_in(True)
print('ok')
