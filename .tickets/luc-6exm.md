---
id: luc-6exm
status: closed
deps: []
links: []
created: 2026-11-17T12:30:00Z
type: task
priority: 2
---
# units entered from C never grow their inlining budget (raytrace array._pack genexpr)

Compiled code now resumes generators it did not make through C (gen_send nested, lu_jit_run). lu_jit_run never runs the HOT_ENTRIES budget-growth check lu_jit_enter does, so array._pack's genexpr unit (raytrace) keeps Struct.pack/_encode out of line: raytrace +7.7% instructions (30.2G -> 32.5G at -l 10), where the interpreter-resumed unit grew before. Tried: growth at every C entry (raytrace -2.7%, genshi +19%, pygments +12%); growth at C generator resumes only (raytrace -2.5%, genshi -4%, pygments +12%); one step only (raytrace -2.4%, pygments +6.8%: RegexLexer.get_tokens_unprocessed rebuilt 6x at 340 KB). The growth policy (per-code, all entries, kill-and-rebuild) costs more than it saves there; the lever is a budget that follows what the out-of-line calls cost, not entry counts.


## Notes

**2026-11-17T21:28:08Z**

Closed by the turn rule in hot_exit: a unit entered after a generator's yield counts a site called on at least half its entries as in a loop. Core lease, min of 5-7 runs, instructions / task-clock vs d23c71e: raytrace -9.2% / -11%; genshi -1.2% / -0.3..+1% (compile +240 ms, code +3.4 MB); pygments +0.1% / -0.4%; async_generators, xml_etree, richards, deltablue, mako, logging, json_dumps, go, nbody within +-0.2% instr (go and json_dumps show the same hot sites as base). jitcheck lane turns.
