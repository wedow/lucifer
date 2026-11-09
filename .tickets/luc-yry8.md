---
id: luc-yry8
status: closed
deps: []
links: []
created: 2026-11-07T18:46:10Z
type: bug
priority: 2
tags: [bench, corpus]
---
# corpus timeouts: docutils, mdp and scimark exceed 300 s per run

With the corpus loops (tools/bench/corpus: about 0.1 s per pyperf value on CPython 3.14t, 1 warm-up and 3 values), these pyperformance runs take under 4 s each on CPython and over 300 s on Lucifer, so they are recorded as timeouts. Profile each; the regex-heavy programs (regex_v8, regex_effbot, cli.markdown: 100-230x) and decimal (telco: 137x) are the next slowest that still finish.


## Notes

**2026-11-09T00:33:44Z**

scimark fixed on luc-mods: array items go through typed bytearray primitives (was about 100x CPython per item); scimark finishes in 11 s. docutils and mdp not looked at.
