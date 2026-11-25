---
id: luc-k7ns
status: open
deps: []
links: [luc-3s7n]
created: 2026-11-24T04:00:00Z
type: bug
priority: 2
---
# jit: a unit keeps its namespace alive while its code lives (units as ephemerons of their globals)

A unit holds strong references to what it folded (also what it learned: a call site's callee, a function of another namespace): its globals and builtins, values read from them (functions, classes), the functions it inlined. The collector sees them through the code object (code_traverse visits its units' constants), so a namespace that only its own units keep alive is freed when the code is garbage too (a code compiled per request, tests/kernel/jit_shared_code_collect.py). But when the code stays alive (one compiled code exec'd into a namespace per request, a module reloaded, FunctionType(code, globals)), every namespace whose function of that code was compiled stays alive as long as the code: the code reaches its units, which reach the namespace. CPython frees those namespaces. Under -X lucifer_jit=always every such namespace compiles on its first call, so each one leaks; with the default tier only those whose function got hot do.

Shape of the fix: a unit built for globals G is reachable only if both its code and G are reachable by other paths (an ephemeron keyed by G). In the collector: update_refs subtracts the units' references as now; the marking passes (mark_alive_from_roots, mark_heap_visitor's propagation, handle_resurrected_objects) skip a keyed unit's constants when its G is not yet marked, record it, and after propagation mark the recorded units whose G became reachable, to a fixpoint; units whose G stays unreachable die (killed with the world stopped, freed in delete_garbage before the garbage's tp_clear, so their references go like a clear's). No activation can be in such a unit (a running frame holds its function, which holds G). Size: about 120 lines in runtime/interp/collector.c and runtime/jit/jit.c (a traverse entry that visits a unit's constants only when told G is reachable, and the kill list), plus the gc:1, gc:100000 and handoff lanes of jit_shared_code_collect.py with the shared-code cases enabled under every lane.

Also, with no unit involved: a code's profiles (jc->profs, its met types and value records) hold strong references to types and values from a namespace, so they keep it alive as long as the code lives; code_traverse does not visit them, so they only keep things alive, never free them early.

Conditions for the ephemeron design (from the review of luc-3s7n): (1) the key must cover every namespace a unit is built for, the inlined callees' globals as well as the root's, so a unit reachable through one live namespace does not keep another dead one; (2) profiles need weak treatment or pruning, or they keep the namespace alive anyway; (3) in this trial-deletion collector it needs a fixpoint mark (a key that becomes reachable late marks its units' constants and what they reach, repeated until nothing changes), and a unit whose key dies must be killed before its constants are released.

Weigh first the simpler alternative: hold globals and learned callees weakly (identities, not references) and kill the unit when one of them dies (a dict or function dealloc watcher event), so a unit never keeps its namespace alive and the collector needs no change; what else the unit folded from the namespace (values it read, functions it inlined) must then be guarded or held through the same weak path.

Acceptance: tests/kernel/jit_shared_code_collect.py passes with both of its cases unskipped in every lane (its skips under -X lucifer_jit=always and the deopt stress mode are this ticket's). Observed holders today: under -X lucifer_jit=always, the shared code's own units keep each namespace they were built for; under the deopt stress mode, serve's unit at its inner loop (live module-level code) inlined the first request's handler as a learned callee (guarded by identity) and keeps that handler and its namespace: 19 of 20 namespaces freed, the referrers of the kept one are serve's code (through its unit's constants) and the handler's own garbage code.

## 2026-11-25: the fresh-code case now leaks under -X lucifer_jit=always too

The daily of 2026-11-25 failed jit_shared_code_collect in the jit lane (19 of 20 namespaces freed). Bisected by building: first bad b20b81a9 (the ledger: "rebuild and grow when the losses paid cover the build"); 8827f901 and f626ea38 pass. Same leak as this ticket, a different unit holding it:

- serve has two units: serve@73 (the inner `for _ in range(calls): total += h(50)`) and serve@14 (the outer request loop, built in request 0). serve@14 inlines the inner loop with a GUARD_IS on request 0's handler at serve@79; that constant holds the handler, its globals and the namespace's witness.
- Before b20b81a9, request 1's handler failed serve@73's guard at serve@79 and the exit marked the site (mark-nospec), which killed every unit of the root: serve@14 and serve@73 both died and were reclaimed.
- Since b20b81a9 the exit is at a place that learned since the build, so it goes to serve@73's own stale ledger and kills only serve@73 (learned-since-build), rebuilt with a generic call. serve@14 survives every request: each entry leaves at the inlined compile frame (compile@414) before the guard, so it never pays a loss there and is never rebuilt; it keeps namespace 0 until it dies (gc referrers: the handler is held only by serve's code, through its units' constants).

The fresh-code case is now skipped wherever every function compiles at its first call (the jit and jit-deopt lanes); the acceptance is unchanged: both cases pass unskipped in every lane.
