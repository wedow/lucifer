"""unicode slice nonbmp."""
from support import loops


def run():
    s = 'a😀é𝄞z'
    return len(s), list(s), s[::-1], s[1:4:2], s.find('𝄞'), s.encode('utf-8').hex()


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
