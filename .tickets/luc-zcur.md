---
id: luc-zcur
status: closed
deps: []
links: []
created: 2026-11-07T13:11:11Z
type: task
priority: 2
tags: [jit, perf]
---
# JIT: class calls allocate the instance in line and inline __init__

Obj() costs about 8,500 instructions from compiled code (CPython 3.14t: 955): CALL of a class goes through h_call, type.__call__, object.__new__ and an __init__ frame. CD makes 3.1M instances and Havlak 1.4M per AWFY iteration. Lower a call of a heap type whose __new__ is object's and whose __init__ is a Python function: guard the type's version, pop the instance from the carrier's GC_PRE heap as emit_build pops tuples (preheader, inline values from the shared keys, type incref), and inline __init__ through inline_call. Escape analysis (roadmap item 6) can then make short-lived instances virtual. Found by luc-vzka's census.


## Notes

**2026-11-07T22:36:55Z**

Landed on branch luc-zcur (commit "perf: class calls allocate the instance in line and inline __init__"). A CALL of a class whose metaclass is type, __new__ object's and __init__ a Python function (or object's, no arguments) guards the class version, allocates in line from the GC_PRE heap (emit_new_object), and inlines __init__ under a made frame of type.__call__ at its boundary after the init call, so an exit inside resumes type.__call__ (which checks the result and returns obj). An __init__ that does not inline is called from that frame (init_call). Stores and loads on made instances use the class's own shape (lu_specialize_*_fresh) whatever the site cached; a store of a new name takes the word in line when later words are empty. h_attr_store stores into a dict-less instance as the interpreter does. -X lucifer_jit_stats now prints allocations per site and kind.

Census, compiled-code allocations per AWFY iteration (canonical inner; class_call counts each class call, whose instance and six *args/**kwargs containers type.__call__ makes): CD 25.7M -> 11.9M, Havlak 22.0M -> 13.2M, Json 3.44M -> 1.17M, DeltaBlue 2.18M -> 1.13M, Richards 0.237M -> 0.237M, NBody unchanged; Flask index 4,549 -> 4,434 per request (251 class calls left: CALL_KW, metaclasses; luc-rwdf).

Instructions per loop iteration (one core): P(i, i) stored to a list: master 14,032, branch 579 (CPython 1,685); Bare(): 8,451 -> 346 (1,136).

AWFY best of 3 (ms, exclusive host): CD 5626 -> 2149, DeltaBlue 1211 -> 935, Havlak 5693 -> 3556, Json 2091 -> 1433, Bounce 867 -> 663; geomean vs master 0.846, vs CPython 0.520 (master ~0.61). Instructions (reduced inner): CD 10.9G -> 4.79G, Havlak 44.6G -> 27.3G, Json 7.80G -> 6.05G, DeltaBlue 4.60G -> 3.87G; cycles agree. Flask in process 1-4% faster.
