"""Bounded builtin operation trace; replay does not need Hypothesis."""
# difftest-state
# hypothesis-seed: 31410
from support import loops


def hot(x):
    xs = [23, 100, 85, -36, 48, -55, 28, 96]
    alias = xs
    d = {0: x, 1: -x}
    s = '\U0001f600b\ud800\xe9\x00  b\xdf\U0001f600ba'
    trace = []
    try:
        xs[1:3] = [21, x]
        result = None
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.reverse()
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = d.setdefault(-20, x)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.reverse()
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.pop(19)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.extend([25])
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        xs[1:3] = [10, x]
        result = None
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.extend([16])
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        d[23] = x
        result = None
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
        values.append((d.get(0, 0) // 386))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(tuple(xs))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(sorted(xs))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(x / 3)
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(s.casefold())
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(s.casefold())
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(bytes([x & 255]))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append((abs(((804 // xs[0]) << 5)) << 0))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append((180 & abs(len(xs))))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append((181 & xs[0]))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    return {"xs": xs, "alias": alias, "d": d, "trace": trace, "values": values}


for _ in range(loops()):
    STATE = hot(7)
STATE = hot(-86)
print("state captured")
