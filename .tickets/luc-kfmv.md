---
id: luc-kfmv
status: open
deps: []
links: []
created: 2026-11-20T02:35:46Z
type: bug
priority: 1
tags: [interpreter, jit, generators]
---
# generator return releases its locals while its frame is still current: finalizer reads released locals (crash)

A generator that returns releases its locals (runtime/interp/gen.c clear_frame, from lu_gen_finish at runtime/interp/ceval.c:2111) while its frame is still ts->current_frame, and Py_XDECREFs each local without clearing its slot first. CPython pops the frame before it clears the locals (GH-99729), so a finalizer the release runs sees the consumer on top. Here a __del__ that calls sys._getframe(1) gets the finished generator's frame, and its f_locals hands out the local being finalized and locals already released: use after free.

Compiled code does the same on purpose: OP_UNLINK leaves a generator's frame current until lu_gen_finish (runtime/jit/x86_64/emit.c:3842). 2148a50d (a returning root's releases take no exit state) rests on "the frame is off the chain, so no walker can see it"; for a generator root that does not hold, and a walker then reads a frame with no published state.

Repro (CPython 3.14t prints `0 0`): kernelcheck --programs gen_return_release_frame on master 702f52bf: plain FAIL (stack smashing detected, SIGABRT), jit-deopt FAIL (SIGSEGV), jit PASS.

```python
import gc, sys
kept = []
class Probe:
    def __del__(self):
        f = sys._getframe(1)
        if f.f_code.co_name == "gen":
            kept.append(dict(f.f_locals))
def gen(n):
    q = [str(i) * 3 for i in range(n % 7 + 1)]
    p = Probe()
    r = {"k": [n]}
    yield n
    return 1
def consume(n):
    for v in gen(n):
        pass
    return v
for i in range(4000):
    consume(i)
    if kept:
        junk = [bytearray(64) for _ in range(50)]
gc.collect()
bad = 0
for d in kept:
    for k, v in d.items():
        try:
            repr(v)
        except Exception:
            bad += 1
print(len(kept), bad)
```

Fix direction: make the caller current (and clear each slot before its release) before the generator's locals go, in both the interpreter and OP_UNLINK.

