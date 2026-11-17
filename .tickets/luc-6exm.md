---
id: luc-6exm
status: open
deps: []
links: []
created: 2026-11-17T12:30:00Z
type: task
priority: 2
---
# units entered from C never grow their inlining budget (raytrace array._pack genexpr)

Compiled code now resumes generators it did not make through C (gen_send nested, lu_jit_run). lu_jit_run never runs the HOT_ENTRIES budget-growth check lu_jit_enter does, so array._pack's genexpr unit (raytrace) keeps Struct.pack/_encode out of line: raytrace +7.7% instructions (30.2G -> 32.5G at -l 10), where the interpreter-resumed unit grew before. Tried: growth at every C entry (raytrace -2.7%, genshi +19%, pygments +12%); growth at C generator resumes only (raytrace -2.5%, genshi -4%, pygments +12%); one step only (raytrace -2.4%, pygments +6.8%: RegexLexer.get_tokens_unprocessed rebuilt 6x at 340 KB). The growth policy (per-code, all entries, kill-and-rebuild) costs more than it saves there; the lever is a budget that follows what the out-of-line calls cost, not entry counts.

