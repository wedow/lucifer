# sched_setaffinity(0, ...) belongs to the calling OS thread, so it pins the calling task to its
# carrier for life (DESIGN 5.4): the task sees one native thread id however often it polls, under
# -X lucifer_handoff as plain.
import _thread
import os

mask = os.sched_getaffinity(0)
os.sched_setaffinity(0, mask)
seen = set()
for i in range(2000):
    seen.add(_thread.get_native_id())
print(len(seen), os.sched_getaffinity(0) == mask)
