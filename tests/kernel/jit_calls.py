# Calls the optimizing tier should inline once it has run: a method reached
# through a function that was compiled before the interpreter ever ran it
# (-X lucifer_jit=always compiles at the first call), and builtins whose
# kernel code calls primitives that run no Python (len of a dict or a str),
# and operations the interpreter answers in C without the kernel (subscripts,
# comparisons, attributes of several classes).
# A character loop over a str (json's escapers, which this runtime writes in
# Python: a code point read at a time, then a slice and a concatenation)
# inlines into its caller's loop too, for strs of every width, and so does
# one over the items of a bytes object. So do functions taking *args and
# **kwargs called with positional arguments. So do the kernel's argument
# parsers that loop over or index *args (str.strip's chars, str.startswith's
# prefix and its tuple of prefixes).
# A function compiled before the module binds a new global keeps compiling
# its global loads after (the module's keys are versioned again).
# Each result must be the interpreter's; tools/jitcheck also runs this at two
# sizes and requires that entries into compiled code do not grow with the
# iterations, and that the tier gives up on no function.
import sys
from json.encoder import encode_basestring, encode_basestring_ascii

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Node:
    def __init__(self, v):
        self.v = v

    def get(self):
        return self.v


def visit(n):
    return n.get() + 1


def methods(n):
    node = Node(3)
    s = 0
    for i in range(n):
        s += visit(node)
    return s


def lengths(n):
    d = {'a': 1, 'b': 2}
    t = 'xyz'
    s = 0
    for i in range(n):
        s += len(d) + len(t)
    return s


def generic(n, pair, other):
    """Subscripts and comparisons compiled as generic operations, which the
    interpreter answers in C (a tuple's item, tuples' equality)."""
    s = 0
    for i in range(n):
        s += pair[0]
        if pair == other:
            s += 1
    return s


class A:
    def __init__(self):
        self.x = 1


class B(A):
    pass


class C(A):
    pass


class D(A):
    pass


def attributes(n):
    """Attribute sites that met several classes and stay generic."""
    objs = [A(), B(), C(), D()]
    s = 0
    for i in range(n):
        o = objs[i & 3]
        o.y = i
        s += o.x + o.y
    return s


def pops(n):
    """Kernel code with int constants past one digit (list.pop's index check
    against -sys.maxsize - 1): compiled arithmetic on them never leaves."""
    items = [0]
    s = 0
    for i in range(n):
        items.append(i)
        s += items.pop(0)
    return s


def nonzero(a, b, x):
    return a if x else b


def truths(n):
    """The truth of ints of no known type in an inlined frame (a third
    argument: calls record the kinds of their first two)."""
    items = [0, 1, -5, 1 << 40]
    s = 0
    for i in range(n):
        s += nonzero(1, 0, items[i & 3])
    return s


def emptiness(n):
    """The truth of containers and strs of no known type in an inlined
    frame, which runs no Python either."""
    items = [{}, {'k': 1}, [], [0], '', 'ab', (), (0,)]
    s = 0
    for i in range(n):
        s += nonzero(1, 0, items[i & 7])
    return s


def clamp(x, hi):
    return x if x < hi else hi


def wide(n):
    """Ints past one digit that fit 64 bits, of no known value, through an
    inlined comparison and arithmetic."""
    bounds = [sys.maxsize, 1 << 40]
    s = 0
    for i in range(n):
        s += clamp(i, bounds[i & 1]) - (bounds[i & 1] >> 20)
    return s


def strings(n):
    words = ('a plain key', 'caf\xe9', '\u20ac and \u00a3', 'a clef \U0001d120')
    s = 0
    for i in range(n):
        s += len(encode_basestring_ascii(words[0])) + len(encode_basestring(words[i & 3]))
    return s


def checksum(data):
    s = 0
    i = 0
    while i < len(data):
        s = (s * 31 + data[i]) & 0xffff
        i += 1
    return s


def octets(n):
    blobs = (b'GET / HTTP/1.1', bytes(range(250, 256)))
    s = 0
    for i in range(n):
        s += checksum(blobs[i & 1])
    return s


def spread(t):
    values = [*t]
    return len(values)


def unpacking(n):
    """A list built from a tuple (the kernel's argument parsing), inlined;
    and a key deleted from a dict in the loop itself."""
    t = (1, 2, 3)
    d = {}
    s = 0
    for i in range(n):
        s += spread(t)
        d[i & 7] = i
        del d[i & 7]
    return s + len(d)


def gather(a, *args, **kwargs):
    if kwargs:
        return -1
    return a + len(args)


def keep_rest(*args, **kwargs):
    return args, kwargs


def varargs(n):
    """Functions taking *args and **kwargs, called with positional
    arguments only: str's predicates and upper (the kernel's methods take
    *args and **kwargs), and the program's own, including one whose packed
    arguments are its result."""
    words = ('abc', 'a1', '  x ', '42')
    s = 0
    for i in range(n):
        w = words[i & 3]
        s += w.isalpha() + w.isdigit() + len(w.upper()) + gather(i) + gather(i, 2, 3)
        args, kwargs = keep_rest(i, w)
        s += len(args) + len(kwargs)
    return s


def stripping(n):
    """str methods whose kernel code iterates over and indexes *args."""
    words = ('  a ', 'b', '\tc', 'd  ')
    s = 0
    for i in range(n):
        w = words[i & 3]
        s += len(w.strip()) + len(w.strip(' ')) + w.startswith(' ') + w.startswith(('\t', 'd'))
    return s


def lengths_of(ws):
    s = 0
    for w in ws:
        s += len(w)
    return s


WORDS = ('ab', 'c') * 64
total = 0
for f in [lengths_of]:
    for _ in range(100):
        total += f(WORDS)
    late = total
    for _ in range(100):
        total += f(WORDS)


print(late, total, methods(N), lengths(N), generic(N, (1, 2), (1, 2)), attributes(N), pops(N), truths(N), emptiness(N), wide(N), strings(N), octets(N), unpacking(N), varargs(N), stripping(N))
