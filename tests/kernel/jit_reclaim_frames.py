# A dead unit is freed only once no activation can still be in it. Each
# case kills the unit of code that is running (or suspended) and collects
# at once, then goes on in it: returns into a recursion's older
# activations, a walker reading a running frame's locals, an exception
# unwinding through them, a generator resumed after its unit died, and a
# frame object read after its frame returned through a dead unit.
import gc
import sys
import zlib

K = 1


def flip():
    global K
    K = 2 if K == 1 else 1
    gc.collect()


def down(k):
    if k == 0:
        flip()
        return K
    return down(k - 1) + K


def walked():
    flip()
    return sorted(sys._getframe(1).f_locals.items())


def reader(x):
    pair = (x, K)
    seen = walked()
    return pair, seen, K


def raiser(k):
    if k == 0:
        flip()
        raise ValueError(K)
    try:
        return raiser(k - 1) + K
    finally:
        flip()


def counter(n):
    for i in range(n):
        yield i * K


def framed():
    frame = sys._getframe()
    t = [K]
    flip()
    return frame, t


def main():
    out = []
    for r in range(80):
        out.append(down(r % 9))
        out.append(reader(r))
        try:
            raiser(r % 5)
        except ValueError as e:
            out.append(e.args)
        g = counter(5)
        s = next(g) + next(g)
        flip()
        s += sum(g)
        out.append(s)
        frame, t = framed()
        out.append((t, sorted(frame.f_locals)))
    print(len(out), zlib.crc32(repr(out).encode()), out[-6:])


main()
