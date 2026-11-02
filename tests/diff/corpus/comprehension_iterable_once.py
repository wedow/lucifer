"""comprehension iterable once."""
from support import loops


def run():
    log = []
    def source(): log.append('source'); return [1, 2, 3]
    gen = (x * 2 for x in source()); before = list(log); values = list(gen)
    return before, log, values


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
