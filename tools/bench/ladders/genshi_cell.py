"""genshi's _flatten loop (template/base.py) as a ladder, on the events one table cell makes:
TEXT '<td>', EXPR c, TEXT '</td>'. Usage: genshi_cell.py CASE WARM REPS; a rep is 1000 rows x 10 cells."""
import sys
from genshi.core import START, TEXT
from genshi.template.base import Context, EXPR, SUB, EXEC, _eval_expr
from genshi.template.eval import Expression
import six

numeric_types = (float, int)
number_conv = str
POS = (None, 1, 0)
CELL = [(TEXT, '<td>', POS), (EXPR, Expression('c'), POS), (TEXT, '</td>', POS)]
ROW = [dict(a=1, b=2, c=3, d=4, e=5, f=6, g=7, h=8, i=9, j=10)]

def g0(stream, ctxt, vars):
    for kind, data, pos in stream:
        yield kind, data, pos

def g1(stream, ctxt, vars):
    for kind, data, pos in stream:
        if kind is START and data[1]:
            pass
        elif kind is EXPR:
            yield kind, data, pos
        elif kind is SUB:
            pass
        elif kind is EXEC:
            pass
        else:
            yield kind, data, pos

def g2(stream, ctxt, vars):
    for kind, data, pos in stream:
        if kind is START and data[1]:
            pass
        elif kind is EXPR:
            result = _eval_expr(data, ctxt, vars)
            yield kind, result, pos
        elif kind is SUB:
            pass
        elif kind is EXEC:
            pass
        else:
            yield kind, data, pos

def g3(stream, ctxt, vars):
    for kind, data, pos in stream:
        if kind is START and data[1]:
            pass
        elif kind is EXPR:
            result = _eval_expr(data, ctxt, vars)
            if result is not None:
                if isinstance(result, six.string_types):
                    yield TEXT, result, pos
                elif isinstance(result, numeric_types):
                    yield TEXT, number_conv(result), pos
        elif kind is SUB:
            pass
        elif kind is EXEC:
            pass
        else:
            yield kind, data, pos

def source(ctxt):
    """The two ForDirectives: a scope per item, the body's events per cell."""
    for row in range(1000):
        for c in ROW[0].values():
            ctxt.push({'c': c})
            for ev in CELL:
                yield ev
            ctxt.pop()

def run(level):
    ctxt = Context(table=ROW)
    for ev in level(source(ctxt), ctxt, {}):
        pass

if __name__ == "__main__":
    f = globals()[sys.argv[1]]
    warm, reps = int(sys.argv[2]), int(sys.argv[3])
    for i in range(warm + reps):
        run(f)
