# array('d', values) packs them through a generator expression that
# bytes.join, a C consumer, resumes for each value: compiled code enters the
# generator's unit once a turn of its loop, and each turn calls the struct
# packer, too large for the unit's inlining budget. A call made on most turns
# is in the loop the generator runs, so once hot it is taken in line.
# tools/jitcheck also runs this at two sizes and requires that direct calls
# grow by at most the one a turn the consumer's step makes.
import sys
from array import array

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def floats(n):
    return [i + 0.5 for i in range(n)]


a = array('d', floats(N))
print(len(a), a[N // 2], a.tobytes()[-8:].hex())
