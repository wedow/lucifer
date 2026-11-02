"""unicode split join."""
from support import loops


def run():
    s = ' a\u2003b\u00a0c\t d '
    return s.split(), s.rsplit(None, 2), '|'.join(s.split()), 'aaa'.partition('aa'), 'a\nb\r\nc'.splitlines(True)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
