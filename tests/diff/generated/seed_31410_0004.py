"""Bounded builtin operation trace; replay does not need Hypothesis."""
# difftest-state
# hypothesis-seed: 31410
from support import loops


def hot(x):
    xs = []
    alias = xs
    d = {0: x, 1: -x}
    s = 'b\ud800b'
    trace = []
    try:
        d[0] = x
        result = None
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = d.setdefault(26, x)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.sort(key=lambda v: v % 3)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.pop(-27)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        xs[1:3] = [27, x]
        result = None
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = d.setdefault(27, x)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    values = []
    try:
        values.append(s.casefold())
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(((9223372036854775807 << 2) << 1))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append((xs[0] << 2))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append((((1267650600228229401496703205377 & -58) << 3) << 2))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(x / 3)
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(x / 3)
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append((bool(xs[0]) * (712 << 7)))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    return {"xs": xs, "alias": alias, "d": d, "trace": trace, "values": values}


for _ in range(loops()):
    STATE = hot(7)
STATE = hot(-62)
print("state captured")
