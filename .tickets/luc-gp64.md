---
id: luc-gp64
status: open
deps: []
links: []
created: 2026-11-15T10:38:25Z
type: bug
priority: 3
---
# gc.get_referrers lists a dead (p,) tuple under -X lucifer_jit=always

Under -X lucifer_jit=always, gc.get_referrers(probe) after 'holder = (probe,); refs = gc.get_referrers(probe); del holder, refs' still lists a (probe,) tuple, kept alive by an untracked holder that CPython does not have. Repro:

    import gc, sys
    probe = []
    for i in range(3000):
        holder = (probe,)
        refs = gc.get_referrers(probe)
        assert any(r is holder for r in refs)
        del holder, refs
        found = [r for r in gc.get_referrers(probe) if type(r) is tuple]
        if found:
            print('found', len(found), 'at', i, 'refcount', sys.getrefcount(found[0])); break
        if i % 50 == 0:
            gc.collect()
    else:
        print('ok')

Found by the race review of the free lists.

