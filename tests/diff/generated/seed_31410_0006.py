"""Bounded builtin operation trace; replay does not need Hypothesis."""
# difftest-state
# hypothesis-seed: 31410
from support import loops


def hot(x):
    xs = [-31]
    alias = xs
    d = {0: x, 1: -x}
    s = ' \U0001f600\x00\x00\x00\x00\x00\x00aa\U0001f600\x00'
    trace = []
    try:
        result = xs.append(26)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.insert(13, x)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = d.pop(27)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.extend([27])
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.append(3)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.pop(21)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.extend([-1])
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.sort(key=lambda v: v % 3)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.insert(-8, x)
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    try:
        result = xs.extend([-20])
        trace.append(("ok", result, list(xs), list(d.items())))
    except Exception as error:
        trace.append((type(error).__name__, error.args))
    values = []
    try:
        values.append(x / 3)
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(abs((int(xs[0]) - 94)))
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
        values.append(bool((d.get(0, 0) % xs[0])))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(bool((d.get(0, 0) << 2)))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(int((-57 << 6)))
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(s.split())
    except Exception as error:
        values.append((type(error).__name__, error.args))
    try:
        values.append(x / 3)
    except Exception as error:
        values.append((type(error).__name__, error.args))
    return {"xs": xs, "alias": alias, "d": d, "trace": trace, "values": values}


for _ in range(loops()):
    STATE = hot(7)
STATE = hot(1361129467683753853853498429727072845824)
print("state captured")
