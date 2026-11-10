# An mmap with memoryviews exported cannot be closed or resized; once they
# are released it can. Threads export and release concurrently.
import mmap
import threading

m = mmap.mmap(-1, 42)
m.write(b"CPython")
v = memoryview(m)
for op in (m.close, lambda: m.resize(100)):
    try:
        op()
    except BufferError as e:
        print('BufferError', e)
print(m.closed, bytes(v[:7]))
v.release()


def work():
    for _ in range(200):
        with memoryview(m) as mv:
            assert mv[:7] == b"CPython"
        mv = memoryview(m)
        try:
            m.close()
        except BufferError:
            pass
        assert mv[:7] == b"CPython"
        mv.release()


threads = [threading.Thread(target=work) for _ in range(6)]
for t in threads:
    t.start()
for t in threads:
    t.join()
m.close()
print(m.closed)
