"""Bounded builtin operation trace; replay does not need Hypothesis."""
# difftest-state
# hypothesis-seed: 31410
from support import loops


def hot(x):
    xs = [-28, -81, 63]
    alias = xs
    d = {0: x, 1: -x}
    s = '\xdf\xe9a \xdf\xdf \xdf\ud800\xe9'
    trace = []
    try:
        result = xs.sort(key=lambda v: v % 3)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.append(-19)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = d.pop(-20)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.sort(key=lambda v: v % 3)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    values = []
    try:
        values.append(str(x))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(tuple(xs))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append((sum(xs) << 1))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(bytes([x & 255]))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(str(x))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(bytes([x & 255]))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(x / 3)
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append((623 % -1000))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    return {"xs": xs, "alias": alias, "d": d, "trace": trace, "values": values}


for _ in range(loops()):
    STATE = hot(7)
STATE = hot(20)
print("state captured")
