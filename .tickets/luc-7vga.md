---
id: luc-7vga
status: open
deps: []
links: []
created: 2026-11-19T12:00:49Z
type: task
priority: 3
---
# json scanner: recursive descent runs out of inline budget

json_loads after the 11-19 libmods lane: _parse_array/Scanner.__call__ units are 90-200 KB; _skip, _match_number, _scanstring, dict.setdefault and the recursive _scan_once are left as calls ('over budget' / 'recursive' in LUCIFER_JIT_DEBUG), 688 k compiled calls per 9 k loads. Tried: one loop over an explicit stack of open containers (no recursion, same errors, differential-tested against the C scanner): the loop unit met values of every type at one site, left on every entry and retired 8 levels; entries rose 55 k -> 1.39 M. Needs the JIT side: budget by what a path executes rather than code size, or polymorphic value sites that do not leave; then revisit.


## Notes

**2026-11-21T06:04:11Z**

Landed as 613f164b, replacing the held half-budget rule (dd33780c). Attribution of that rule's losses (AWFY Json 1x100, pprint, instructions on a core lease): Json +2.34G, of it +2.40G in the compiler (each_ref, lu_ev_virt, rewrite, lu_ssa_optimize, lu_fs_*) and -0.04G in compiled code; pprint +2.66G = +3.27G compiler, -0.69G compiled code. Cause: refusing a recursive copy inside a cycle of several functions made the refused member a root that rebuilt the cycle: _Parser._read_value 5 builds/1.0 MB -> 10/6.3 MB, PrettyPrinter._format 1.9 -> 6.2 MB. New rule: only a function's copy of itself is refused, when the budget cannot hold again what the active frame has spent (recorded per inlined frame); cycles keep the plain budget. Master vs 613f164b, same checkout path, median of 3: CD -16.67%, Permute -1.17%, Json +0.00%, Havlak +0.03%, Towers +0.00%, pprint +0.03% (bimodal in both arms), regex_compile -0.78%, async_generators +0.25%, async_tree_cpu_io_mixed +0.50%. A refusal census over AWFY and all pyperformance rows (1 loop each) found refused copies only in CD, async_generators, the four async_tree *cpu_io_mixed* rows, docutils, generators, go, meteor_contest, regex_compile, regex_v8, sphinx, sqlglot_v2, sqlglot_v2_optimize, sympy; not measured for want of core time: docutils, go, sphinx, sympy, sqlglot, meteor, generators, regex_v8, the _tg/eager async_tree rows. json_loads (+0.63%) and deepcopy (~+1%) have no refusals: the same shift comes from the struct ctx field alone (luc-uk70). The +10.8% json_loads gain of the half rule is not kept: it came from refusing copies inside the scanner's cycle, the same refusals that cost AWFY Json. Gates: kernelcheck jit_recursive_budget, jit_large_callees, jit_calls, jit_recursion_depth, jit_inlined_frames, jit_inline_paths, deep_recursion, deep_container_recursion in plain/jit/jit-deopt 8/8 each; jitcheck all lanes pass (recursive 3319 -> 5819 at 20k/40k; calls GAP is the same on master); check-harness OK.
