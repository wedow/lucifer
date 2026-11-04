# Children that do not os._exit: each finishes the program the parent started, through sys.exit, so
# its output is flushed and its status is the one it chose. Forking twelve times puts the forks on
# both carriers under -X lucifer_handoff.
import atexit
import os
import sys

atexit.register(lambda: print("atexit in", "parent" if os.getpid() == parent else "child"))
parent = os.getpid()
statuses = []
for i in range(12):
    pid = os.fork()
    if pid == 0:
        print("child", i)
        sys.exit(i)
    statuses.append(os.waitstatus_to_exitcode(os.waitpid(pid, 0)[1]))
print(statuses)
