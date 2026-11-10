---
id: luc-c7ae
status: open
deps: []
links: []
created: 2026-11-10T01:54:16Z
type: task
priority: 2
assignee: Greg Wedow
tags: [conformance, glm]
---
# difftest: harvest python-formalization's case generators as Lucifer fuzzers

github.com/Broyojo/python-formalization (Apache-2.0) models CPython 3.15.2 in Lean and differential-tests each area with 52 Python case generators (difftest/*.py: longobj, longobj_str, binary64 float formatting, complex, decimal, datetime/zoneinfo, dictset + set hooks, listsort, json, pickle, _pyio buffered/state/callback, itertools, ipaddress, fractions, pygrammar, bytecode frames, ...), each run to 50k+ agreeing cases. Fetch them pinned by commit (runtime fetch, as the corpus does; nothing committed but a lock entry), replace the Lean side with Lucifer, and diff against CPython 3.14t as oracle. Skip cases whose behaviour changed between 3.14 and 3.15 (check against 3.14t itself: oracle and model disagree -> skip). Run each generator at its default case count on several seeds; batch mismatches by cause into tickets; add a periodic lane (weekly with libtest) once stable. The Lean model and its 418 CPython bug records are not used: Lucifer matches CPython 3.14t behaviour, bugs included.

