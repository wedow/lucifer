"""Bounded builtin operation trace; replay does not need Hypothesis."""
# difftest-state
# hypothesis-seed: 31410
from support import loops


def hot(x):
    xs = [-24, -100, -45, 4, -63, -47, 36, -82]
    alias = xs
    d = {0: x, 1: -x}
    s = '\x00\xdf\xe9\xe9\xe9'
    trace = []
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
    try:
        result = d.pop(-8)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.extend([-4])
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    values = []
    try:
        values.append(tuple(xs))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(bytes([x & 255]))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append((abs((x & -522)) << 4))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(abs(-159))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(tuple(xs))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(x / 3)
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append((((x & 1267650600228229401496703205377) << 1) & ((len(xs) << 3) << 8)))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(((1267650600228229401496703205377 // 9223372036854775807) % (len(s) << 6)))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(sorted(xs))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(((-869 << 2) << 6))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    return {"xs": xs, "alias": alias, "d": d, "trace": trace, "values": values}


for _ in range(loops()):
    STATE = hot(7)
STATE = hot(1361129467683753853853498429727072845824)
print("state captured")
