# A dict kept alive by its watcher's DEALLOCATED event stays the dict it was:
# its items stay, no new dict takes its place, and when it is dropped at last
# it is freed once, touching no other dict.
import gc
import dict_watch_resurrect as w

for i in range(5):
    d = {'a': i, 'b': [i]}
    w.watch(d)
    del d
    r = w.take()
    fresh = [{'x': 'fresh'} for _ in range(3)]
    print(type(r).__name__, any(r is f for f in fresh), sorted(r.items()) if type(r) is dict else r)

for i in range(200):
    d = {'a': i}
    w.watch(d)
    del d
    r = w.take()
    fresh = {'x': i}
    del fresh
    other = [{'y': j} for j in range(3)]
    del r
    for o in other:
        if list(o) != ['y']:
            raise SystemExit('a live dict changed: %r' % (o,))
    del other
gc.collect()
print('ok')
