"""Bounded builtin operation trace; replay does not need Hypothesis."""
# difftest-state
# hypothesis-seed: 31410
from support import loops


def hot(x):
    xs = [-70, -35, 91]
    alias = xs
    d = {0: x, 1: -x}
    s = '\U0001f600'
    trace = []
    try:
        result = xs.insert(18, x)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.insert(3, x)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.insert(12, x)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.pop(-11)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.extend([-13])
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.insert(30, x)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        xs[1:3] = [-21, x]
        result = None
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    values = []
    try:
        values.append((((9223372036854775807 << 4) & abs(-9223372036854775808)) << 6))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(((9223372036854775807 << 8) * ((d.get(0, 0) << 0) << 2)))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append((((156 << 7) // bool(545)) << 3))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append((1267650600228229401496703205377 << 6))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(330)
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(str(x))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(int(9223372036854775807))
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
        values.append(s.casefold())
    except Exception as error:
        values.append((type(error).__name__, error.args))
    return {"xs": xs, "alias": alias, "d": d, "trace": trace, "values": values}


for _ in range(loops()):
    STATE = hot(7)
STATE = hot(28)
print("state captured")
