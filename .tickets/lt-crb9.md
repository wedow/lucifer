---
id: lt-crb9
status: open
deps: []
links: []
created: 2026-11-25T14:48:12Z
type: feature
priority: 2
parent: luc-o80b
---
# local events kill only the units that hold the code

Since luc-o80b's B1 fix, sys.monitoring.set_local_events (and f_trace_opcodes, which sets SYS_TRACE's local INSTRUCTION event) bumps the global tools version under its stop-the-world. Every unit in the process then dies at its next entry and every compiled frame on every thread is made real, whatever code the events were set on. Measured (luc-o80b question 4): 100 local-event toggles on a cold code between runs of a hot loop cost about 6.7 builds and 0.7-0.8 ms per toggle; master built nothing. A coverage tool that sets local events once per new code rebuilds the hot units each time.

Mechanism (design question 4's alternative): a code dependency kind in runtime/jit/deps.c, registered at publish for the unit's root code and each code it runs in line (generators adopted in line included); set_local_events reports the code (lu_jit_invalidate) and spends no global version. Running units with the code leave at their frame check after the call (same thread) or their next poll (other threads), as for other dependencies. Needs a dependency with no guard behind it, and the dead checks it brings to units that had none: measure them.

## Acceptance Criteria

A tool enables LINE on X at PY_START and disables it at PY_RETURN, X called from a compiled hot loop L: L's unit is not killed per iteration (builds stay flat with the iterations; tools_moved kills of L stay at zero). 100 local-event toggles on a cold code cause no builds. jit_local_events_inlined, tools_own_call, tools_version_many, threads_local_events_inlined, entry_tools_midcall pass in all lanes. -c pass, pyflakes, Richards flat within 0.3%.

