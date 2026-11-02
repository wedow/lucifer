"""unicode case expansion."""
from support import loops


def run():
    return [(s, s.lower(), s.upper(), s.casefold(), s.title(), s.swapcase()) for s in ('Straße', 'İıΣσς', 'ﬃ', '𝕒é')]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
