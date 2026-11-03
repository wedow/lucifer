"""list.sort's algorithm (Objects/listobject.c, CPython 3.14): the natural
runs, binary insertion to minrun, the powersort merge order and the galloping
merges, so results, stability and the comparisons users can observe equal
CPython's. PyPy's rpython/rlib/listsort.py is the same timsort with the
older stack-invariant merge order; CPython 3.14 merges by powersort, which
decides the comparison counts, so that is what this follows.

sort(items, keyfunc, reverse) sorts the exact list `items` in place and calls
keyfunc once per item, in order, before any comparison. With a key it sorts
indexes into the key list, so keys never move; comparisons are the same. The
list is some permutation of its input however a comparison fails.
"""

from _lucifer import MISSING, type_lookup, list_splice
from _lucifer import _boot, operators

call_special = _boot.call_special
truth = operators.truth

__all__ = ()

MIN_GALLOP = 7
MAX_MINRUN = 64


def _safe(a, b):
    """safe_object_compare: PyObject_RichCompareBool(a, b, Py_LT)."""
    return True if a < b else False


def _choose(keys):
    """The pre-sort check: the comparison for keys, and for the first
    elements when the keys are tuples (unsafe_tuple_compare)."""
    n = len(keys)
    first = keys[0]
    in_tuples = type(first) is tuple and len(first) > 0
    key_type = type(first[0]) if in_tuples else type(first)
    same = True
    for k in keys:
        if in_tuples:
            if not (type(k) is tuple and len(k) != 0):
                in_tuples = False
                same = False
                break
            k = k[0]
        if type(k) is not key_type:
            same = False
            if not in_tuples:
                break
    compare = _safe
    if same:
        if key_type is int or key_type is str or key_type is float:
            compare = _safe
        else:
            lt = type_lookup(key_type, '__lt__')
            if lt is not MISSING:
                def compare(a, b):
                    """unsafe_object_compare: the type's own __lt__, then the generic path."""
                    r = call_special(lt, a, b)
                    if r is NotImplemented:
                        return True if a < b else False
                    return truth(r)
    if in_tuples:
        elem = _safe if key_type is tuple else compare

        def compare(v, w):
            """unsafe_tuple_compare"""
            vlen = len(v)
            wlen = len(w)
            i = 0
            while i < vlen and i < wlen:
                x = v[i]
                y = w[i]
                if not (x is y or x == y):
                    break
                i += 1
            if i >= vlen or i >= wlen:
                return vlen < wlen
            if i == 0:
                return elem(v[i], w[i])
            return True if v[i] < w[i] else False
    return compare


def sort(items, keyfunc, reverse):
    n = len(items)
    if keyfunc is None:
        keys = items
        order = items
    else:
        keys = []
        for x in items:
            keys.append(keyfunc(x))
        order = list(range(n))
    compare = _choose(keys) if n > 1 else _safe
    if keyfunc is None:
        islt = compare
    else:
        def islt(x, y):
            return compare(keys[x], keys[y])
    try:
        if n >= 2:
            _timsort(order, n, islt, reverse)
    finally:
        if keyfunc is not None:
            list_splice(items, 0, n, [items[i] for i in order])


def _timsort(a, n, islt, reverse):
    mg = [MIN_GALLOP]       # ms.min_gallop
    pbase = []              # the pending runs: start, length and power
    plen = []
    ppower = []

    def gallop_left(key, base, count, hint):
        """gallop_left over a[base:base + count]: where key goes, left of equals."""
        ofs = 1
        lastofs = 0
        if islt(a[base + hint], key):
            maxofs = count - hint
            while ofs < maxofs:
                if islt(a[base + hint + ofs], key):
                    lastofs = ofs
                    ofs = (ofs << 1) + 1
                else:
                    break
            if ofs > maxofs:
                ofs = maxofs
            lastofs += hint
            ofs += hint
        else:
            maxofs = hint + 1
            while ofs < maxofs:
                if islt(a[base + hint - ofs], key):
                    break
                lastofs = ofs
                ofs = (ofs << 1) + 1
            if ofs > maxofs:
                ofs = maxofs
            k = lastofs
            lastofs = hint - ofs
            ofs = hint - k
        lastofs += 1
        while lastofs < ofs:
            m = lastofs + ((ofs - lastofs) >> 1)
            if islt(a[base + m], key):
                lastofs = m + 1
            else:
                ofs = m
        return ofs

    def gallop_right(key, base, count, hint):
        """gallop_right: where key goes, right of equals."""
        ofs = 1
        lastofs = 0
        if islt(key, a[base + hint]):
            maxofs = hint + 1
            while ofs < maxofs:
                if islt(key, a[base + hint - ofs]):
                    lastofs = ofs
                    ofs = (ofs << 1) + 1
                else:
                    break
            if ofs > maxofs:
                ofs = maxofs
            k = lastofs
            lastofs = hint - ofs
            ofs = hint - k
        else:
            maxofs = count - hint
            while ofs < maxofs:
                if islt(key, a[base + hint + ofs]):
                    break
                lastofs = ofs
                ofs = (ofs << 1) + 1
            if ofs > maxofs:
                ofs = maxofs
            lastofs += hint
            ofs += hint
        lastofs += 1
        while lastofs < ofs:
            m = lastofs + ((ofs - lastofs) >> 1)
            if islt(key, a[base + m]):
                ofs = m
            else:
                lastofs = m + 1
        return ofs

    def gallop_left_in(t, key, base, count, hint):
        """gallop_left over the temp array t."""
        ofs = 1
        lastofs = 0
        if islt(t[base + hint], key):
            maxofs = count - hint
            while ofs < maxofs:
                if islt(t[base + hint + ofs], key):
                    lastofs = ofs
                    ofs = (ofs << 1) + 1
                else:
                    break
            if ofs > maxofs:
                ofs = maxofs
            lastofs += hint
            ofs += hint
        else:
            maxofs = hint + 1
            while ofs < maxofs:
                if islt(t[base + hint - ofs], key):
                    break
                lastofs = ofs
                ofs = (ofs << 1) + 1
            if ofs > maxofs:
                ofs = maxofs
            k = lastofs
            lastofs = hint - ofs
            ofs = hint - k
        lastofs += 1
        while lastofs < ofs:
            m = lastofs + ((ofs - lastofs) >> 1)
            if islt(t[base + m], key):
                lastofs = m + 1
            else:
                ofs = m
        return ofs

    def gallop_right_in(t, key, base, count, hint):
        """gallop_right over the temp array t."""
        ofs = 1
        lastofs = 0
        if islt(key, t[base + hint]):
            maxofs = hint + 1
            while ofs < maxofs:
                if islt(key, t[base + hint - ofs]):
                    lastofs = ofs
                    ofs = (ofs << 1) + 1
                else:
                    break
            if ofs > maxofs:
                ofs = maxofs
            k = lastofs
            lastofs = hint - ofs
            ofs = hint - k
        else:
            maxofs = count - hint
            while ofs < maxofs:
                if islt(key, t[base + hint + ofs]):
                    break
                lastofs = ofs
                ofs = (ofs << 1) + 1
            if ofs > maxofs:
                ofs = maxofs
            lastofs += hint
            ofs += hint
        lastofs += 1
        while lastofs < ofs:
            m = lastofs + ((ofs - lastofs) >> 1)
            if islt(key, t[base + m]):
                ofs = m
            else:
                lastofs = m + 1
        return ofs

    def merge_lo(pa, na, pb, nb):
        """Merges the runs a[pa:pa + na] and a[pb:pb + nb] (pb = pa + na,
        na <= nb) through a temp copy of the first."""
        t = a[pa:pa + na]
        ia = 0
        dest = pa
        a[dest] = a[pb]
        dest += 1
        pb += 1
        nb -= 1
        copyb = False
        try:
            if nb == 0:
                return
            if na == 1:
                copyb = True
                return
            min_gallop = mg[0]
            while True:
                acount = 0
                bcount = 0
                while True:
                    if islt(a[pb], t[ia]):
                        a[dest] = a[pb]
                        dest += 1
                        pb += 1
                        bcount += 1
                        acount = 0
                        nb -= 1
                        if nb == 0:
                            return
                        if bcount >= min_gallop:
                            break
                    else:
                        a[dest] = t[ia]
                        dest += 1
                        ia += 1
                        acount += 1
                        bcount = 0
                        na -= 1
                        if na == 1:
                            copyb = True
                            return
                        if acount >= min_gallop:
                            break
                min_gallop += 1
                while True:
                    if min_gallop > 1:
                        min_gallop -= 1
                    mg[0] = min_gallop
                    k = gallop_right_in(t, a[pb], ia, na, 0)
                    acount = k
                    if k:
                        a[dest:dest + k] = t[ia:ia + k]
                        dest += k
                        ia += k
                        na -= k
                        if na == 1:
                            copyb = True
                            return
                        if na == 0:
                            return
                    a[dest] = a[pb]
                    dest += 1
                    pb += 1
                    nb -= 1
                    if nb == 0:
                        return
                    k = gallop_left(t[ia], pb, nb, 0)
                    bcount = k
                    if k:
                        a[dest:dest + k] = a[pb:pb + k]
                        dest += k
                        pb += k
                        nb -= k
                        if nb == 0:
                            return
                    a[dest] = t[ia]
                    dest += 1
                    ia += 1
                    na -= 1
                    if na == 1:
                        copyb = True
                        return
                    if not (acount >= MIN_GALLOP or bcount >= MIN_GALLOP):
                        break
                min_gallop += 1
                mg[0] = min_gallop
        finally:
            if copyb:
                a[dest:dest + nb] = a[pb:pb + nb]
                a[dest + nb] = t[ia]
            elif na:
                a[dest:dest + na] = t[ia:ia + na]

    def merge_hi(pa, na, pb, nb):
        """Merges a[pa:pa + na] and a[pb:pb + nb] (na >= nb) through a temp
        copy of the second, from the right."""
        t = a[pb:pb + nb]
        dest = pb + nb - 1
        ib = nb - 1             # t[ib] is the current last of B
        ia = pa + na - 1        # a[ia] is the current last of A
        a[dest] = a[ia]
        dest -= 1
        ia -= 1
        na -= 1
        copya = False
        try:
            if na == 0:
                return
            if nb == 1:
                copya = True
                return
            min_gallop = mg[0]
            while True:
                acount = 0
                bcount = 0
                while True:
                    if islt(t[ib], a[ia]):
                        a[dest] = a[ia]
                        dest -= 1
                        ia -= 1
                        acount += 1
                        bcount = 0
                        na -= 1
                        if na == 0:
                            return
                        if acount >= min_gallop:
                            break
                    else:
                        a[dest] = t[ib]
                        dest -= 1
                        ib -= 1
                        bcount += 1
                        acount = 0
                        nb -= 1
                        if nb == 1:
                            copya = True
                            return
                        if bcount >= min_gallop:
                            break
                min_gallop += 1
                while True:
                    if min_gallop > 1:
                        min_gallop -= 1
                    mg[0] = min_gallop
                    k = gallop_right(t[ib], pa, na, na - 1)
                    k = na - k
                    acount = k
                    if k:
                        dest -= k
                        ia -= k
                        a[dest + 1:dest + 1 + k] = a[ia + 1:ia + 1 + k]
                        na -= k
                        if na == 0:
                            return
                    a[dest] = t[ib]
                    dest -= 1
                    ib -= 1
                    nb -= 1
                    if nb == 1:
                        copya = True
                        return
                    k = gallop_left_in(t, a[ia], 0, nb, nb - 1)
                    k = nb - k
                    bcount = k
                    if k:
                        dest -= k
                        ib -= k
                        a[dest + 1:dest + 1 + k] = t[ib + 1:ib + 1 + k]
                        nb -= k
                        if nb == 1:
                            copya = True
                            return
                        if nb == 0:
                            return
                    a[dest] = a[ia]
                    dest -= 1
                    ia -= 1
                    na -= 1
                    if na == 0:
                        return
                    if not (acount >= MIN_GALLOP or bcount >= MIN_GALLOP):
                        break
                min_gallop += 1
                mg[0] = min_gallop
        finally:
            if copya:
                a[dest + 1 - na:dest + 1] = a[ia + 1 - na:ia + 1]
                dest -= na
                ia -= na
                a[dest] = t[ib]
            elif nb:
                a[dest + 1 - nb:dest + 1] = t[0:nb]

    def merge_at(i):
        pa = pbase[i]
        na = plen[i]
        pb = pbase[i + 1]
        nb = plen[i + 1]
        plen[i] = na + nb
        if i == len(pbase) - 3:
            pbase[i + 1] = pbase[i + 2]
            plen[i + 1] = plen[i + 2]
            ppower[i + 1] = ppower[i + 2]
        del pbase[-1]
        del plen[-1]
        del ppower[-1]
        k = gallop_right(a[pb], pa, na, 0)
        pa += k
        na -= k
        if na == 0:
            return
        nb = gallop_left(a[pa + na - 1], pb, nb, nb - 1)
        if nb <= 0:
            return
        if na <= nb:
            merge_lo(pa, na, pb, nb)
        else:
            merge_hi(pa, na, pb, nb)

    def powerloop(s1, n1, n2):
        result = 0
        x = 2 * s1 + n1
        y = x + n1 + n2
        while True:
            result += 1
            if x >= n:
                x -= n
                y -= n
            elif y >= n:
                break
            x <<= 1
            y <<= 1
        return result

    def reverse_slice(lo, hi):
        hi -= 1
        while lo < hi:
            a[lo], a[hi] = a[hi], a[lo]
            lo += 1
            hi -= 1

    def count_run(lo, nremaining):
        """The length of the run at lo, made ascending in place."""
        k = 1
        while k < nremaining:
            if islt(a[lo + k], a[lo + k - 1]):
                break
            k += 1
        if k == nremaining:
            return k
        if k > 1:
            if islt(a[lo], a[lo + k - 1]):
                return k
            reverse_slice(lo, lo + k)
        k += 1
        neq = 0
        while k < nremaining:
            if islt(a[lo + k], a[lo + k - 1]):
                if neq:
                    neq += 1
                    reverse_slice(lo + k - neq, lo + k)
                    neq = 0
            else:
                if islt(a[lo + k - 1], a[lo + k]):
                    break
                neq += 1
            k += 1
        if neq:
            neq += 1
            reverse_slice(lo + k - neq, lo + k)
            neq = 0
        reverse_slice(lo, lo + k)
        while k < nremaining:
            if islt(a[lo + k], a[lo + k - 1]):
                break
            k += 1
        return k

    def binarysort(lo, count, ok):
        """Binary insertion of a[lo + ok:lo + count] into the sorted a[lo:lo + ok]."""
        if not ok:
            ok += 1
        while ok < count:
            left = 0
            right = ok
            pivot = a[lo + ok]
            while True:
                m = (left + right) >> 1
                if islt(pivot, a[lo + m]):
                    right = m
                else:
                    left = m + 1
                if left >= right:
                    break
            if ok > left:
                a[lo + left + 1:lo + ok + 1] = a[lo + left:lo + ok]
            a[lo + left] = pivot
            ok += 1

    try:
        if reverse:
            reverse_slice(0, n)
        minrun = n
        r = 0
        while minrun >= MAX_MINRUN:
            r |= minrun & 1
            minrun >>= 1
        minrun += r
        lo = 0
        nremaining = n
        while True:
            run = count_run(lo, nremaining)
            if run < minrun:
                force = nremaining if nremaining <= minrun else minrun
                binarysort(lo, force, run)
                run = force
            if pbase:
                power = powerloop(pbase[-1], plen[-1], run)
                while len(pbase) > 1 and ppower[-2] > power:
                    merge_at(len(pbase) - 2)
                ppower[-1] = power
            pbase.append(lo)
            plen.append(run)
            ppower.append(0)
            lo += run
            nremaining -= run
            if not nremaining:
                break
        while len(pbase) > 1:
            i = len(pbase) - 2
            if i > 0 and plen[i - 1] < plen[i + 1]:
                i -= 1
            merge_at(i)
    finally:
        if reverse:
            reverse_slice(0, n)
