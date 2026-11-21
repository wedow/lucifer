---
id: luc-8b3s
status: closed
deps: []
links: []
created: 2026-11-20T21:58:12Z
type: bug
priority: 2
tags: [generators, jit]
---
# compiled generator return: a finalizer of its locals sees gi_running True

Review of 0fb4b5ab. Compiled code releases a returning generator's locals after OP_UNLINK but before lu_gen_finish, so frame_state is still EXECUTING and ts->exc_info still the generator's while the finalizers run. CPython's clear_gen_frame marks the frame cleared and restores exc_info first. The interpreter path (lu_gen_finish) is right.

Repro (CPython 3.14t prints [(False, 'stop')]; Lucifer prints [(True, 'generator already executing')] under -X lucifer_jit=always, and both entries in plain and jit-deopt once the loop gets compiled):

    class Probe:
        def __del__(self):
            g = gens[-1]
            try:
                next(g); r = 'resumed'
            except StopIteration:
                r = 'stop'
            except ValueError as e:
                r = str(e)
            seen.add((g.gi_running, r))
    def gen(n):
        p = Probe()
        yield n
    gens = []; seen = set()
    for i in range(2000):
        g = gen(i); gens.append(g)
        for _ in g: pass
    print(sorted(seen))

Fix direction: mark the generator cleared and restore exc_info at OP_UNLINK for a generator root (or call lu_gen_finish's state part there), so the releases run as in clear_gen_frame. Same family as luc-y0wy (gi_frame).


## Notes

**2026-11-21T01:43:13Z**

Fixed by 0eeebe59: OP_UNLINK of a compiled generator root marks it cleared, restores exc_info and gives back its depth before the releases; lu_gen_finish then only releases. tests/kernel/gen_return_release_state.py (oracle [(False, True, 'stop')]) before: plain [(False,True,'stop'),(True,False,'generator already executing')], jit [(True,False,'generator already executing')], jit-deopt both; after: oracle's output in all three lanes. 23/23 generator and recursion neighbours pass in plain, jit and jit-deopt.
