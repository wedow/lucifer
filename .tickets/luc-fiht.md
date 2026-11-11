---
id: luc-fiht
status: open
deps: []
links: []
created: 2026-11-11T20:53:04Z
type: chore
priority: 3
assignee: Greg Wedow
---
# set merge: use the dict merge's restart instead of the judged-tuple protocol; fromkeys(dict) copies

With both sections held for the whole merge, the set side can restart the probe when the table changes, as lu_dict_merge_dict does, removing the per-candidate judged tuple. dict.fromkeys(dict) copies the source first (dict.py:326); iterate it natively under the lock instead. Also CPython sets override=1 when the destination is empty in dict_merge; lu_dict_merge_dict keeps 0 or 2.

