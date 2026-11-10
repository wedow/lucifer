"""Attribute loads at sites that meet instances keeping their values in
line and instances whose values moved to a dict (a __dict__ read), of one
class: names read, deleted, set again, a class attribute behind them, and
a property beside them, in hot loops."""
from support import loops


class Cell:
    kind = 'class'

    def __init__(self, pos):
        self.pos = pos
        self.mark = pos * 2

    @property
    def twice(self):
        return self.pos * 2

    def total(self):
        return self.pos + self.mark + len(self.kind)


def run():
    cells = [Cell(i) for i in range(20)]
    for c in cells[::3]:
        c.__dict__['extra'] = 1          # moves its values to a dict
    s = 0
    for _ in range(100):
        for c in cells:
            s += c.total() + c.twice
    del cells[3].mark
    cells[3].mark = -5
    cells[6].kind = 'instance'
    del cells[9].pos
    out = []
    for c in cells[:10]:
        try:
            out.append((c.total(), c.kind))
        except AttributeError as e:
            out.append(str(e))
    return s, out, sorted(vars(cells[0]))


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
