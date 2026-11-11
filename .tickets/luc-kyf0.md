---
id: luc-kyf0
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# os.times, os.waitid_result, os.sched_param and signal.struct_siginfo are missing

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_structseq_diff cannot load (all 2,000 cases) over seeds 1 and 2 at 1,000 cases each: the generator lists these structseq types at module level.

```python
import os, time, resource, pwd, grp, signal, sys
for mod, name in [(os, 'terminal_size'), (time, 'struct_time'), (os, 'times_result'), (os, 'uname_result'),
                  (os, 'waitid_result'), (resource, 'struct_rusage'), (pwd, 'struct_passwd'), (grp, 'struct_group'),
                  (signal, 'struct_siginfo'), (os, 'stat_result'), (os, 'statvfs_result'), (os, 'sched_param'),
                  (os, 'times')]:
    print(mod.__name__, name, hasattr(mod, name))
```
CPython 3.14t:
```
os terminal_size True
time struct_time True
os times_result True
os uname_result True
os waitid_result True
resource struct_rusage True
pwd struct_passwd True
grp struct_group True
signal struct_siginfo True
os stat_result True
os statvfs_result True
os sched_param True
os times True
```
Lucifer:
```
os terminal_size True
time struct_time True
os times_result False
os uname_result True
os waitid_result False
resource struct_rusage True
pwd struct_passwd True
grp struct_group True
signal struct_siginfo False
os stat_result True
os statvfs_result True
os sched_param False
os times False
```


