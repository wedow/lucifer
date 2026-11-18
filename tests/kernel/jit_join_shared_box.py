# Two locals name one int that is no object yet: a raw value and the object
# it came in as (st.ptr). At the join after the if, sptr is a raw phi with
# the object beside it, and ptr an object phi, which makes the int real on
# the edge where ptr = sptr. The raw phi must then take the object the int
# became, not the one it came from: that one was released as the edge
# settled, and taking it again released it twice (pyperformance html5lib's
# re matching at steady state).
import sys


class St:
    __slots__ = ('ptr',)


def scan(st, stack, n):
    sptr = st.ptr
    if sptr < 0:
        return 0, None
    ptr = None
    i = 0
    while i < n:
        if i % 3 == 0:
            ptr = sptr
        else:
            ptr = stack[i]
            sptr = sptr + 1
        i += 1
    return sptr, ptr


stack = [object() for _ in range(10)]
counts = {}
total = 0
for k in range(40000):
    st = St()
    x = 100000 + k
    st.ptr = x
    s, p = scan(st, stack, k % 7)
    total += s
    del st, s, p
    c = sys.getrefcount(x)
    counts[c] = counts.get(c, 0) + 1
print(total, sorted(counts.items()))
