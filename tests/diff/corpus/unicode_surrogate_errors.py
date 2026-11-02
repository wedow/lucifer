"""unicode surrogate errors."""
from support import loops


def run():
    s = 'a\ud800z'
    return s.encode('utf-8', 'surrogatepass').hex(), s.encode('ascii', 'backslashreplace'), s.encode('utf-8', 'replace'), s.encode('utf-8', 'surrogatepass').decode('utf-8', 'surrogatepass') == s


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
