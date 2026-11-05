---
id: luc-gt2f
status: closed
deps: []
links: []
created: 2026-11-05T03:04:44Z
type: bug
priority: 2
---
# gate flakes: threads_basic timer race, check-jit havlak timeout


## Notes

**2026-11-05T03:29:18Z**

threads_basic uses an Event; difftest target timeout scales with the oracle's time. havlak always-mode slowness: luc-jh5s
