"""unicode predicates."""
from support import loops


def run():
    return [(s, s.isalpha(), s.isdecimal(), s.isdigit(), s.isnumeric(), s.isidentifier(), s.isspace()) for s in ('Ⅻ', '²', '٣', '变量', ' ', '')]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
