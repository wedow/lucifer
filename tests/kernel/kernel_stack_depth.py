"""Builtins over nested containers reach the depths CPython's C stack
allows and raise RecursionError, with the text of CPython's C check, past
them, in threads of several stack sizes (luc-s3tf). Compiled code's frames
hide the kernel function that names the check, so json.dumps prints only
the start of its message."""
import json
import re
import threading


def nest(kind, depth):
    value = 1
    for _ in range(depth):
        value = kind(value)
    return value


CASES = {
    'repr list': lambda d: repr(nest(lambda v: [v], d)),
    'repr dict': lambda d: repr(nest(lambda v: {'k': v}, d)),
    'repr tuple': lambda d: repr(nest(lambda v: (v,), d)),
    'json.dumps': lambda d: json.dumps(nest(lambda v: [v], d)),
    'json.loads': lambda d: json.loads('[' * d + '1' + ']' * d),
    '==': lambda d: nest(lambda v: [v], d) == nest(lambda v: [v], d),
}


def run(depth):
    for name, case in CASES.items():
        try:
            case(depth)
            print(depth, name, 'ok')
        except RecursionError as e:
            text = re.sub(r'\d+ kB', 'N kB', str(e))
            print(depth, name, text[:len('Stack overflow (used N kB)')] if name == 'json.dumps' else text)


# CPython 3.14t reaches 104 (==) to 151 (repr of a list) levels in a 64 KiB
# thread, 4,830 to 6,978 in 1 MiB and 40,000 to 58,000 in 8 MiB.
for size, depths in ((64 << 10, (100,)), (256 << 10, (1000,)), (1 << 20, (4000,)), (8 << 20, (35000, 200000))):
    threading.stack_size(size)
    for depth in depths:
        t = threading.Thread(target=run, args=(depth,))
        t.start()
        t.join()
