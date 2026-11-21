# Optional positional arguments in text and binary methods. The growth lane
# checks that calls in the hot loop do not build padding tuples and lists.
import sys


def scan(n):
    hits = 0
    for _ in range(n):
        if 'abc'.startswith('bc', 1):
            hits += 1
        if b'abc'.startswith(b'ab'):
            hits += 1
    return hits


n = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
print(scan(n))
