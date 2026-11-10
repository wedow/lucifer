"""list slice stores and str indexing on their native paths: constant and
computed bounds, negative and out-of-range bounds, tuple and self sources,
extended slices, and the errors that stay the kernel's."""
from support import loops


def run():
    out = []
    p = list(range(9))
    for k in range(1, 9):
        p[:k + 1] = p[k::-1]
    out.append(p[:])
    q = list(range(6))
    q[:3] = (9, 8)
    q[-2:] = q
    q[10:20] = [7]
    q[4:1] = ['x']
    q[:] = q[::-1]
    out.append(q)
    r = list(range(5))
    r[::2] = 'abc'
    out.append(r)
    try:
        r[::2] = [1]
    except ValueError as exc:
        out.append(str(exc))
    try:
        r[1:2] = 5
    except TypeError as exc:
        out.append(str(exc))
    t = 'héllo wörld' * 3
    out.append(''.join(t[i] for i in range(-len(t), len(t), 3)))
    try:
        t[len(t)]
    except IndexError as exc:
        out.append(str(exc))
    return out


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
