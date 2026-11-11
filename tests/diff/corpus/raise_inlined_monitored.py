"""An exception raised in inlined user frames and caught in compiled code
while sys.monitoring watches raises: the tool's events, and the traceback,
which keeps every frame the exception left."""
import sys
import traceback
from support import loops

mon = sys.monitoring
TOOL = 4


def inner(x):
    if x % 3 == 0:
        raise KeyError(x)
    return x


def middle(x):
    return inner(x + 1) + 1


def outer(n):
    hits = []
    for i in range(n):
        try:
            middle(i)
        except KeyError as e:
            hits.append([t.name for t in traceback.extract_tb(e.__traceback__)])
    return hits


def run():
    events = []
    for i in range(100):        # warm the loop before the tool watches
        outer(9)
    mon.use_tool_id(TOOL, 'probe')
    mon.register_callback(TOOL, mon.events.RAISE, lambda code, off, exc: events.append(code.co_name))
    mon.set_events(TOOL, mon.events.RAISE)
    try:
        hits = outer(30)
    finally:
        mon.set_events(TOOL, 0)
        mon.register_callback(TOOL, mon.events.RAISE, None)
        mon.free_tool_id(TOOL)
    return hits[:3], len(hits), sorted(set(events)), len(events)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
