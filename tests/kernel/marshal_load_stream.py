# marshal.load on a non-seekable stream reads exactly one value (Python/marshal.c reads through
# file.readinto): values after it stay in the stream, and a writer that keeps the stream open is
# not waited for past the value.
import marshal
import os
import threading

r, w = os.pipe()
os.write(w, marshal.dumps([1, 2]) + marshal.dumps("two"))
os.close(w)
with os.fdopen(r, "rb") as f:
    try:
        print(marshal.load(f))
        print(marshal.load(f))
    except Exception as e:
        print("error", type(e).__name__)

r, w = os.pipe()
os.write(w, marshal.dumps("first"))
loaded = threading.Event()


def answer():
    print("load returned" if loaded.wait(3) else "load waited for the writer")
    os.write(w, marshal.dumps("second"))
    os.close(w)


t = threading.Thread(target=answer)
t.start()
with os.fdopen(r, "rb") as f:
    try:
        first = marshal.load(f)
        loaded.set()
        t.join()
        print(first, marshal.load(f))
    except Exception as e:
        loaded.set()
        t.join()
        print("error", type(e).__name__)
