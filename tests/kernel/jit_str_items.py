# A parser's hot method reads one character of a str at a time through an
# attribute (self.s[self.i]), as AWFY Json's _read does: the str's own
# __getitem__, which compiled code inlines for an exact str and an int.
# tools/jitcheck runs this at two sizes and requires that no call per
# iteration reaches a callee compiled code refuses (-X lucifer_jit_stats'
# whynot object: the wrapper descriptor str.__getitem__). After the loop,
# negative and out-of-range indexes, a str subclass and non-int keys must
# be the interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Reader:
    def __init__(self, s):
        self.s = s
        self.i = 0
        self.cur = None

    def read(self):
        self.i += 1
        if self.i < len(self.s):
            self.cur = self.s[self.i]
        else:
            self.i = 0
            self.cur = self.s[-1]


def hot(n):
    r = Reader("abéc一d\U0001f600")
    seen = 0
    for _ in range(n):
        r.read()
        if r.cur == "c":
            seen += 1
    return seen


class S(str):
    def __getitem__(self, i):
        return 'sub'


print(hot(N))
for s, k in (("abc", -1), ("abc", 3), ("abc", -4), (S("abc"), 0), ("abc", 1.0), ("abc", True), ("", 0)):
    try:
        print(repr(s[k]))
    except (IndexError, TypeError) as e:
        print(type(e).__name__, e)
print(hot(7))
