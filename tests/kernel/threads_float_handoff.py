"""Floats made on one thread and dropped on another, through a queue, while
both threads also make and drop floats of their own: every value must
survive the trip intact, whichever thread frees and reuses its memory."""
import threading
import queue

N = 20000
q = queue.Queue(maxsize=64)


def producer(out):
    total = 0.0
    for i in range(N):
        x = i * 0.5
        total += x * 2.0 - x
        q.put([x, x + 0.25])
    q.put(None)
    out.append(total)


def consumer(out):
    total = 0.0
    while True:
        pair = q.get()
        if pair is None:
            break
        a, b = pair
        total += (b - a) * 4.0 + a
    out.append(total)


p_out, c_out = [], []
threads = [threading.Thread(target=producer, args=(p_out,)),
           threading.Thread(target=consumer, args=(c_out,))]
for t in threads:
    t.start()
for t in threads:
    t.join()
print(p_out, c_out)
