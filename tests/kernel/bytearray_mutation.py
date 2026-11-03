# In-place bytearray mutation, slice assignment and deletion, and resizing with exports.
def show(label, x):
    print(label, repr(x), x.__alloc__())


def attempt(f):
    try:
        print(repr(f()))
    except Exception as e:
        print(type(e).__name__, e)


a = bytearray(b'hello')
show('new', a)
a.append(33)
a.extend(b' world')
a.extend([65, 66])
a.extend(bytearray(b'!'))
show('extend', a)
a.insert(0, 62)
a.insert(-1, 61)
a.insert(100, 60)
a.insert(-100, 59)
show('insert', a)
print(a.pop(), a.pop(0), a.pop(-2), a.pop(1))
show('pop', a)
a.remove(111)
a.reverse()
show('remove', a)
a.clear()
show('clear', a)
a += b'abcdef'
a *= 2
show('iadd', a)
a *= 0
show('imul0', a)
a = bytearray(b'0123456789')
a[2:5] = b'ab'
show('shrink', a)
a[1:1] = b'XYZ'
show('grow', a)
a[::2] = b'12345'
show('step', a)
a[-1:] = b''
a[:2] = [65, 66, 67]
show('slice', a)
del a[0]
del a[-1]
del a[1:3]
show('del', a)
a = bytearray(b'0123456789')
del a[::3]
show('delstep', a)
del a[::-2]
show('delneg', a)
a = bytearray(b'0123456789')
a[::-1] = b'abcdefghij'
show('revassign', a)
a[3] = 90
a[-1] = 0
show('setitem', a)
a[2:2] = a
show('self', a)
a += a
show('selfadd', a)
a.extend(a)
show('selfextend', a)
b = bytearray(b'abc')
b.resize(6)
show('resize up', b)
b.resize(2)
show('resize down', b)
for n in (10, 100, 1000):
    c = bytearray()
    sizes = []
    i = 0
    while i < n:
        c.append(i % 256)
        if i % (n // 5) == 0:
            sizes.append(c.__alloc__())
        i += 1
    print(n, len(c), sizes)

# resizing with exports
d = bytearray(b'abcdef')
m = memoryview(d)
attempt(lambda: d.append(1))
attempt(lambda: d.extend(b'x'))
attempt(lambda: d.pop())
attempt(lambda: d.__setitem__(slice(0, 2), b'q'))
attempt(lambda: d.__delitem__(0))
attempt(lambda: d.clear())
attempt(lambda: d.resize(3))
attempt(lambda: d.__iadd__(b'zz'))
attempt(lambda: d.__imul__(2))
d[0:2] = b'XY'
d[1] = 66
show('same-size ok', d)
print(bytes(m), m[1], len(m))
m.release()
d.append(1)
show('after release', d)
with memoryview(d) as mm:
    attempt(lambda: d.append(2))
d.append(3)
show('after with', d)
attempt(lambda: bytearray(b'ab').pop(5))
attempt(lambda: bytearray(b'ab').insert('x', 1))
attempt(lambda: bytearray().remove(1))
attempt(lambda: hash(bytearray()))
e = bytearray(b'abc')
print(e == b'abc', b'abc' == e, e == bytearray(b'abc'), e < b'abd', e != 'abc')
print(e.__reduce_ex__(3), e.__reduce_ex__(2), e.__reduce__())
for src in ([1, 2, 3], (4, 5), [True, 7], range(4), iter([9, 8]), b'xy', 5, memoryview(b'mv')):
    print(bytearray(src).__alloc__(), bytearray(src))
g = bytearray(b'abc')
g.extend([1, 2])
g.extend(iter([3]))
print(g.__alloc__(), g)
attempt(lambda: g.extend([1, 'x']))
attempt(lambda: g.extend('abc'))
print(g)
