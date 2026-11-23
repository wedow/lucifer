# Releases compiled code makes in line: hot loops that drop the last
# reference to exact ints, floats and compact ascii strs, to tuples of a
# few items (one item in two slots, with references enough that it
# survives), and to instances of a plain class whose attributes hold
# small ints and a shared object: nothing they hold dies with them. Each result must be the interpreter's;
# tools/jitcheck also runs this at two sizes and requires that releases
# calling C are not more frequent with more iterations (-X lucifer_jit_stats).
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Point:
    def __init__(self, x, y, tag):
        self.x = x
        self.y = y
        self.tag = tag


def releases(n):
    ints = [None] * 8
    floats = [None] * 8
    strs = [None] * 8
    tuples = [None] * 8
    points = [None] * 8
    tag = object()
    keep = [tag] * 16
    s = 0
    for i in range(n):
        j = i & 7
        ints[j] = i * 1000 + 7
        floats[j] = i * 0.5 + 0.25
        strs[j] = 'k' + str(i)
        tuples[j] = (j, tag, tag)
        points[j] = Point(j, j + 1, tag)
        s += ints[j] + len(strs[j]) + points[j].y - tuples[j][0]
    return s, floats[3], strs[2], len(keep)


print(releases(N))
