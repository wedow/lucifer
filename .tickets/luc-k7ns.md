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

## 2026-11-25: inventory and design (before code)

### (a) What holds a learned object

Strong references (counted):

- A unit's constants, `u->consts` (runtime/jit/jit.c:1253, taken by lu_ir_const at runtime/jit/ssa.c:392): every object the build folded or guards by identity. The root's own code constants and globals/builtins, and the learned ones: a call site's callee (function, or code + globals + builtins of a code-keyed one), the inlined callees' functions, codes, globals and the values read from them, classes and types guarded on (LOAD_ATTR, isinstance folds, LU_PROF_CLASS values), descriptors, bound selves the build folded. Visited by code_traverse (runtime/interp/code.c:117 -> jit.c:2495), so the collector counts them, and marks through them: a live code's unit marks everything it folded reachable. This is the leak: serve's outer unit's GUARD_IS constant on request 0's handler keeps that handler and its namespace.
- A root's profiles, `p->met[]` (jit.c:660): types an operand met, LU_PROF_CLASS classes (and LU_PROF_IMMORTAL values, never collected). Not visited at all, so they count as references from outside: a profile keeps a class of a dropped namespace (and through its methods the namespace) alive while the root code lives.
- Compile-time temporaries (eval_attr.c:134/229, eval_ops.c:65, jit.c:506/517/551, eval_call.c:1455 kwnames, a tuple of str the collector untracks): released or moved into consts by the end of the build.

- Objects a build makes itself and holds only as constants: function_of_code's stand-in function for a code-keyed callee (runtime/jit/eval_call.c:76), which no other path reaches.

Borrowed pointers, valid only while the unit's constants hold their objects: deopt frame records' code/func (runtime/jit/target.h:104), `u->outs[].callee` (target.h:207), the deps' `want.code` (jit.h struct lu_fdep). Identities only, never dereferenced as objects: deps' `want.obj` (runtime/jit/deps.c), callee tables' `globals` (jit.h struct lu_callee), except that speculate_callee (eval_call.c:124) does read a code-keyed entry's globals to make its stand-in function: a dangling pointer once that namespace dies while the code lives (a use after free the leak used to hide). Weak already: callee table entries name only deferred or immortal functions/codes and pic entries deferred descriptors, forgotten by the collector with the world stopped (lu_jit_pic_forget, jit.c:2458).

Semantically needed while the unit lives: nothing of the namespace a running activation does not reach otherwise, except during an activation: a running unit may hold a callee or value only as its constant (a borrowed load guarded by identity, an inlined frame's function rebuilt from the constant at deopt). With no activation in the unit, every constant is droppable: a unit built for a namespace can only be entered by a frame of that namespace's function, which holds it.

### (b) Mechanism: unit constants and profiles are weak to the collector

No new safepoint and nothing on the hot path: the collection's own stop-the-world does it.

1. Counting: update_refs subtracts every unit's constants and every profile's types as references between tracked objects (a registry of codes with JIT state, walked after the heap pass; image and immortal codes included).
2. Marking never goes through them: code_traverse stops visiting units (as CPython's has nothing to visit), so neither the roots pass nor mark_reachable reaches an object through a unit or profile.
3. Between marking and the garbage scan (lu_jit_collect, world stopped), for every registered code: a unit some task's activation is in, or any unit or profile of a code that is compiling, keeps its constants (each still unreachable one is marked reachable, with what it reaches); any other unit with an unreachable constant is killed ("collected") and reclaimed in the same stop, as dead units are today; a profile entry naming an unreachable object is dropped, its reference released with the reclaimed units.
4. What a build made itself (lu_ir_mine: function_of_code's stand-in function) is no reason for its unit to die: the decision reads the unit's other constants, and a unit that lives keeps those objects too.
5. A code-keyed callee table entry holds its globals by a reference counted the same way (dropped from the table once the globals are garbage), and a build holds the code and globals it reads from an entry while it makes the stand-in function.
6. The reclaimed units' constants are released after the world restarts and before finalizers run, so handle_resurrected_objects sees no reference from a unit or profile to the garbage: none survive (killed, or their constants made reachable).

Safety: a unit's constant is released only by freeing the unit, which happens only through the existing reclaim (with the world stopped, no activation in it, its code not compiling, off its code's list and direct entry: the argument at jit.c "Freeing dead units"). A thread that can load a unit's constant is in an activation of that unit, and that unit's constants are kept as roots for the collection, so no thread is between "loaded the callee pointer" and "took a reference" when the object is freed: nothing a held unit names is garbage. Compiles may park while holding pointers read from their root's profiles, so a compiling code's profiles and units are kept the same way. Profiles are read without a lock only by code that does not poll (lu_jit_prof_note), and written under prof_lock, which no holder parks under.

Cost: none on the fast path (no new store, no new check in compiled code or the interpreter). Per collection: a walk of the units' constants and profiles of codes with JIT state, twice. A unit dies only when something it holds is otherwise garbage, i.e. when the program dropped it.

## 2026-11-25: built (branch luc-k7ns, not landed)

Commits: bf08436d (tests), 739182ff (units and profiles weak to the collector), bdd69784 (code-keyed callee globals held, weak to the collector), 6402e8df (a unit's own stand-in function no reason to die; JIT state freed first in code_dealloc), 411bf6e0 (a collection reads only JIT state that can hold references).

Tests: jit_shared_code_collect with both cases in every lane; jit_namespace_collect (direct, inlined, method, generator, poly site over three namespaces, a closure site that learned while cold, a finalizer calling the hot caller, a reload loop of 40 fresh modules: no marker survives) and jit_namespace_collect_threads (a namespace swapped and dropped on the main thread while another runs it hot). On the base: jit_namespace_collect and _threads fail in plain, jit and jit-deopt (the threads program: 1 of 13 dropped namespaces dead; the others leak to exit), jit_shared_code_collect in jit and jit-deopt. With the first fix alone, jit_namespace_collect crashed under handoff:3 (dict free list corrupted by a stand-in function made of a freed globals dict from a callee table): fixed by bdd69784. The closure case covers that path but does not fail deterministically without the fix (the freed dict's memory is reused as a dict, and the guards hold).

Review (adversarial, read-only) found the stand-in function (a unit-private constant) killing its unit at every collection, the callee-table use after free, and a qualname read in code_dealloc's window: all fixed above.

Gate (final build): new programs plus 30 call/inline/pic/generator/reclaim/release/gc neighbours in jit-off, plain, jit, jit-deopt: all pass but jit_direct_calls, which fails identically on origin/master (the rebound case's traceback names 'plain' frames: not this change). handoff and handoff:3 (new programs, jit_reclaim_threads, gc_thread_queues): pass. gc:1 on the new programs: pass. tools/jitcheck: 0 failing. A fresh worktree built with plain make -j4 passes the new programs in plain, jit, jit-deopt, handoff:3.

Firing: "collected" kills on jit_namespace_collect 29 (on) / 116 (always), 0 on the base; DeltaBlue 0, with kills and compiles identical to the base in both modes. Instructions, base -> new, min of 3 interleaved: Richards +0.003% / +0.001% (on / always); DeltaBlue +0.43% / -0.49% (its base alone varies by 0.4% between runs; per collection after a warm DeltaBlue 4.28M -> 4.25M on, 4.59M -> 4.43M always); -c pass -0.06% / +0.07%; pyflakes -0.04% / +0.02%.
