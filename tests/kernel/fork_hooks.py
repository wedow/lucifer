# os.register_at_fork: before hooks run in reverse registration order in the parent, the after hooks
# in registration order; the child inherits the import lock held by the forking thread, which
# imports in the child need; fork is audited.
import os
import sys

log = []
os.register_at_fork(before=lambda: log.append("before 1"), after_in_parent=lambda: log.append("parent 1"),
                    after_in_child=lambda: log.append("child 1"))
os.register_at_fork(before=lambda: log.append("before 2"), after_in_parent=lambda: log.append("parent 2"),
                    after_in_child=lambda: log.append("child 2"))
for bad in ({}, {"before": 1}, {"after_in_child": "x"}):
    try:
        os.register_at_fork(**bad)
    except TypeError as e:
        print("TypeError:", e)

events = []
sys.addaudithook(lambda event, args: events.append(event) if event == "os.fork" else None)

r, w = os.pipe()
pid = os.fork()
if pid == 0:
    import json                      # an import in the child: the import lock must be usable
    os.write(w, json.dumps(log).encode())
    os._exit(0)
os.waitpid(pid, 0)
print(log)
print(json_text := os.read(r, 1000).decode())
print(events)
