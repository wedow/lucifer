---
id: luc-keyu
status: in_progress
deps: []
links: []
created: 2026-11-07T14:15:47Z
type: feature
priority: 1
tags: [jit, perf]
---
# jit: polymorphic splitting and callee feedback

Roadmap item 4. Duplicate the call after a load_attr_poly method load into each arm so each arm inlines its constant method (adaptive budget, no cliff). Record callee identity per CALL site (mono/polymorphic table in the CALL feedback) so calls through locals, attributes, closures and bound methods inline behind identity guards, with a polymorphic dispatch on 2-4 callees. Folds in luc-sl4y (order-dependent call feedback).

## Acceptance Criteria

Census of non-inlined calls per iteration by reason on DeltaBlue, Json, Havlak, CD and a Flask route, before and after. Inlined-call counts in -X lucifer_jit_stats. luc-sl4y repro as a jitcheck lane running both orders. Kernel tests: a site turning megamorphic after compile, a callee rebound after compile, bound methods, closures. AWFY DeltaBlue and Json; cycles agree with instructions. Gates green.

