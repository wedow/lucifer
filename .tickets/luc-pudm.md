---
id: luc-pudm
status: open
deps: []
links: []
created: 2026-11-08T01:02:06Z
type: chore
priority: 3
parent: luc-0ivh
tags: [jit]
---
# JIT: direct generator resumes refuse the perf trampoline

From luc-0ivh. h_for_iter, h_send, h_gen_send and lu_jit_resume decline when interp->eval_frame is set, which -X perf sets (its trampoline); lu_jit_enter accepts the trampoline (lu_perf_is_trampoline). Under -X perf every generator resume goes through kernel next_item and the eval loop, so perf profiles of generator code are distorted. Accept the trampoline there too.

