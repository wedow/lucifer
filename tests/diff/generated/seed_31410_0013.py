"""Bounded builtin operation trace; replay does not need Hypothesis."""
# difftest-state
# hypothesis-seed: 31410
from support import loops


def hot(x):
    xs = [16, 77, 84, -61, -6, 79, -28]
    alias = xs
    d = {0: x, 1: -x}
    s = '\ud800\ud800  \x00\xdf\xdf \x00'
    trace = []
    try:
        d[-14] = x
        result = None
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        xs[1:3] = [-15, x]
        result = None
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.sort(key=lambda v: v % 3)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        d[14] = x
        result = None
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.extend([28])
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.extend([8])
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    values = []
    try:
        values.append(int(1267650600228229401496703205377))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(tuple(xs))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(s.split())
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(s.split())
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(int(-688))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(str(x))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(int((-365 << 7)))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append((abs(-365) << 3))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    return {"xs": xs, "alias": alias, "d": d, "trace": trace, "values": values}


for _ in range(loops()):
    STATE = hot(7)
STATE = hot(51)
print("state captured")
