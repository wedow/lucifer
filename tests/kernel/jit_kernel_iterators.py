# The iterators the kernel makes and steps for enumerate, zip and map, and
# a list or tuple iterator a loop leaves behind, hot. An iterator that is
# exhausted stays exhausted when its list grows after; an exit in the
# middle of an enumerate, zip or map finds each iterator where it stands;
# an exhausted iterator that escapes behaves as CPython's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
SHORT = [3, 1, 4, 1, 5, 9, 2, 6]


def short_loops(n):
    s = 0
    for k in range(n):
        for i, v in enumerate(SHORT):
            s += i * v
        for a, b in zip(SHORT, (1, 2, 3)):
            s += a - b
        for v in map(abs, SHORT):
            s += v
        for i, v in enumerate(SHORT, k):
            s += i
    return s


def exhausted_then_grown(n):
    out = []
    for k in range(n):
        items = [k, k + 1]
        it = iter(items)
        for x in it:
            pass
        items.append(k + 2)
        out.append(next(it, 'end'))
        more = [k]
        e = enumerate(more)
        for p in e:
            pass
        more.append(k)
        out.append(next(e, 'end'))
        z = zip(items, [1])
        for p in z:
            pass
        items.append(0)
        out.append(next(z, 'end'))
    return out[-6:], len(out)


def partial(n):
    # Stepped by hand, so the iterators outlive the steps.
    out = []
    for k in range(n):
        items = [1, 2, 3]
        e = enumerate(items)
        out.append(next(e))
        items.append(4)
        out.append(list(e))
        m = map(str, (k, k + 1))
        out.append(next(m))
        out.append(next(m))
        out.append(next(m, 'end'))
    return out[-5:]


def leaves_mid(n, at):
    # The body changes type at `at`, so compiled code leaves in the middle
    # of the loop; the interpreter must go on from the same items.
    s = []
    for i, (a, b) in enumerate(zip(range(n), map(lambda v: v * 2, range(n)))):
        x = a + b
        if i == at:
            x = str(x)
        s.append(x)
    return s[at - 1:at + 2], len(s)


def escaped_exhausted(n):
    r = None
    for k in range(n):
        it = iter([k, k])
        for x in it:
            pass
        r = it
        e = enumerate((k,))
        list(e)
    return r.__length_hint__(), r.__reduce__(), list(r), list(e)


print(short_loops(N))
print(exhausted_then_grown(N))
print(partial(N))
print(leaves_mid(N, N // 2))
print(leaves_mid(N, 1))
print(escaped_exhausted(N))
