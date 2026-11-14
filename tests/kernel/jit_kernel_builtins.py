# A builtin the kernel's own code reads (here __import__, which its codec
# fallback calls) is whatever builtins holds when the code runs, in
# compiled kernel code as in the interpreter: rebinding it while that code
# is compiled, and binding it back, leaves nothing of the first binding.
import builtins
import codecs

codecs.lookup('raw-unicode-escape')
real = builtins.__import__


def blocked(name, *args, **kwargs):
    raise ImportError('blocked ' + name)


def primitive(i):
    return ('abc%d' % i).encode('utf-8'), (b'abc%d' % i).decode('ascii'), str(b'abc', 'latin-1')


def fallback(i):
    return ('xሴ%d' % i).encode('raw-unicode-escape'), (b'x\\u1234%d' % i).decode('raw-unicode-escape')


out = []
for turn in range(3):
    builtins.__import__ = blocked
    try:
        for i in range(20):
            out.append(primitive(i))
    finally:
        builtins.__import__ = real
    for i in range(20):
        try:
            out.append(fallback(i))
        except ImportError as e:
            out.append(('ImportError', str(e)))
print(len(out), out[0], out[20], out[-1])
print(sorted({x for x in out if x[0] == 'ImportError'}))
