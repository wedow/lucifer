"""Bounded builtin operation trace; replay does not need Hypothesis."""
# difftest-state
# hypothesis-seed: 31410
from support import loops


def hot(x):
    xs = [-24, -100, -45, 6]
    alias = xs
    d = {0: x, 1: -x}
    s = ''
    trace = []
    try:
        result = xs.append(0)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.append(0)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.insert(2, x)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        xs[1:3] = [-1, x]
        result = None
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.insert(-13, x)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        d[-8] = x
        result = None
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.extend([-30])
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.extend([-21])
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.insert(0, x)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = d.pop(-10)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        xs[1:3] = [-21, x]
        result = None
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        xs[1:3] = [16, x]
        result = None
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    values = []
    try:
        values.append(-4)
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append((0 - 0))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(abs((x & -522)))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(abs((x & -522)))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    return {"xs": xs, "alias": alias, "d": d, "trace": trace, "values": values}


for _ in range(loops()):
    STATE = hot(7)
STATE = hot(0)
print("state captured")
