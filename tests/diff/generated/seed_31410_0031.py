"""Bounded builtin operation trace; replay does not need Hypothesis."""
# difftest-state
# hypothesis-seed: 31410
from support import loops


def hot(x):
    xs = [-45, 21, -100, 100, 68]
    alias = xs
    d = {0: x, 1: -x}
    s = '\xdf\ud800 \x00\x00b\xdf\xdfa\U0001f600\x00a'
    trace = []
    try:
        d[18] = x
        result = None
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.append(-9)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.append(6)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.pop(14)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.extend([18])
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        d[-13] = x
        result = None
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = d.pop(30)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        d[24] = x
        result = None
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    values = []
    try:
        values.append(sorted(xs))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(s.casefold())
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(xs[:])
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(((len(s) // d.get(0, 0)) << 3))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(199)
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(s.casefold())
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append((199 << 6))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    return {"xs": xs, "alias": alias, "d": d, "trace": trace, "values": values}


for _ in range(loops()):
    STATE = hot(7)
STATE = hot(-63)
print("state captured")
