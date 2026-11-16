---
id: luc-3ot9
status: open
deps: []
links: []
created: 2026-11-16T15:18:14Z
type: bug
priority: 2
---
# threads_container_protocol runs 6-50x slower on lucifer under oversubscription and times out

Quiet: 2.2 s wall, 5.5 s user (plain lane; CPython 2.85 s). Eight copies at once on 12 cores with host load ~25: 14 s to 57 s each, and past kernelcheck's 120 s in 2 of 16 plain-lane runs (luc-svqm verification), while CPython under the same load stays deterministic and in time. Its slowest section is 'shared list iterator' (6.5 s of 14 s under load): five threads racing next() on one list iterator while another copies the list. Suspect waiting that spins or holds on to a preempted owner under oversubscription; check how lock and park waits behave when carriers outnumber cores. The kernel gate's retry absorbs it as FLAKY today.

