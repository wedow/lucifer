---
id: luc-eud7
status: open
deps: []
links: []
created: 2026-11-24T01:47:34Z
type: task
priority: 2
parent: luc-orn7
tags: [jit, alloc]
---
# jit: instance release and keyword construction in line

Instance allocation and release are the largest remaining pieces on the object rows (daily idiom table: alloc_escape 115 cycles against PyPy's 26, alloc_kwinit 430 against 5, alloc_local 19 against 5). (1) A per-class plain-release bit and the release's bound counted from the object words present (luc-uqyj design note): the instance release stub was 168 instructions; target under 40 for a plain instance with a few attributes. (2) A class called with keywords binds to __init__'s parameters as keyword calls of functions do, with no dict. (3) Re-trace alloc_escape, alloc_kwinit, alloc_local and oo by owner.


## 2026-11-24: design, the plain-release bit and the release bound

The bit (LU_TPFLAGS_PLAIN_RELEASE, tp_flags bit 40, Lucifer's own): a heap class with inline values and a managed dict, lu_subtype_dealloc its tp_dealloc, no tp_finalize or tp_del, object its base, no __slots__ (Py_SIZE 0). Weak references stay the object's own to tell: making one sets the object's shared-word maybe-weakref bit, and owner_dies already requires the whole shared word zero. lu_type_refresh_plain(tp) computes it and is called wherever an input changes: lu_inline_values_init (the inline-values flag, at creation), every lu_set_slot write to the type's own slots (tp_finalize, tp_del, tp_dealloc; the kernel's _remro_slots walks every subclass with its own set_slot calls when a dunder changes on a class or a base), and __bases__ assignment (tp_base). __slots__ and Py_SIZE never change after creation; __class__ assignment changes no class (the instance's values move to a dict, which the release reads from the object). The other writers of tp_finalize are inheritance at creation (LU_COPY, before the first refresh) and the generator types (not heap classes). The stub keeps its tp_dealloc compare at dispatch (four instructions), so a C metaclass that writes tp_dealloc directly after creation is still covered.

The release then tests the bit, the dict pointer and the in-dict flag of the object, instead of walking the class (tp_finalize, tp_base, ob_size, the weak-list offset and pointer, the managed-dict and inline-values flags: about 25 instructions).

The bound: the words read and the references counted are the class's names (shared names' count), not the instance's room. No instance holds a word at or past the count (an instance's room is at least every later count, attrs.c), and one that came by __class__ assignment has its values in a dict, which fails the release. An instance the interpreter made has room up to 30, so the old bound (room + 1) asked every value for a local count above 31 and the scan read every word of the room; now a class with two names asks for 3 and reads two words. A value can be dropped at most once per name, plus the type, so the D1 argument (nothing it holds reaches zero) holds unchanged.

Test: jit_release_plain (a class gaining and losing __del__ mid-loop, its base gaining one, __bases__ reassigned to a class with a finalizer, subclasses of a plain class).

## 2026-11-24: numbers after (1) and (2)

Exact instructions per inner iteration, origin/master 8c183450 -> now: alloc_escape 415 -> 362 (the release stub 192 -> 139: the class walk and the room bound gone, instances dispatched first); alloc_local 48 -> 48 (no release: the instance stays virtual); alloc_kwinit 1087 -> 35 (keywords bound to __init__: the instance stays virtual, no type.__call__, object.__new__, type_lookup or allocation helper); oo 2120 -> 2120.

What the 139 of alloc_escape's release are, by part (one traced release; small ints in both words, so each value check is one compare): dispatch 3; owner_dies 10 (local count, owner, shared word, allocation hook); the tp_dealloc compare 4; dict pointer and in-dict flag 4; the collector's count 4; the stack frame and thread 4; the bound (names, values, end) 8; the type's count check 6; the scan, 2 words and the end, 17; the drop, 2 words and the end, 19; the type's drop 4; the collector's count, GC bits and the dead count 5; mi_free in line 22; the return 6; alignment nops the rest. The target of 40 needs the scan and the drop made one pass (no drop to do when every word is unboxed or immortal), the type's count check and drop folded (a class is referenced by every instance: a deferred or immortal count), and the free's page lookup shortened.

## State (keep current)

Branch luc-eud7 from origin/master 8c183450. Commits: d822703c (1) the plain-release bit and the class-count bound; 64c2a599 (2) class calls with keywords bound to __init__; tickets. Gated (both on one build): 46 focused programs (the release, finalizer, weakref and collector programs, the kinds programs, jit_release_plain, jit_class_kwcalls, jit_kw_calls) in plain, jit, jit-deopt; jitcheck 0 failing; the release programs, jit_threads, gc_thread_queues, weakref_threads and handoff_finalize in handoff and handoff:2.

Next step: report; then the release toward 40 (above) if wanted.

Scratch tools (untracked, .scratch/): keep/ (k.py, kk.py with alloc_kwinit and oo rows; tr.sh, trace_gdb.py, trsum.py: exact traces under the host lock, test class; dis.sh, ann.py: disassembly; ab.sh, ctr.sh: timing and counters under the core class); gate.sh: the focused gate; s1.sh, s2.sh: build, gate and traces; kw.sh: the alloc_kwinit firing check; bin/lucifer-base (8c183450), -1, -2: the arms.

Not obvious from the code: rm of tr.* in keep/ deletes tr.sh itself.
