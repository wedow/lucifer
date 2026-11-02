"""Bounded builtin operation trace; replay does not need Hypothesis."""
# difftest-state
# hypothesis-seed: 31410
from support import loops


def hot(x):
    xs = [0]
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
        result = xs.append(0)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.append(0)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    values = []
    try:
        values.append(0)
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(0)
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(0)
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(0)
    except Exception as error:
        values.append((type(error).__name__, error.args))
    return {"xs": xs, "alias": alias, "d": d, "trace": trace, "values": values}


for _ in range(loops()):
    STATE = hot(7)
STATE = hot(0)
print("state captured")
