---
id: luc-ueic
status: open
deps: []
links: []
created: 2026-11-08T19:44:46Z
type: task
priority: 3
tags: [jit, perf, regex]
---
# sre: generated matchers stay interpreted under the size-scaled threshold

From luc-ci4f. In cli.pygments and cli.pycodestyle the generated <sre> _entry functions are 25-30% of interpreted instructions (69M and 58M per process), the generic _match another 20%: a generated matcher of a few thousand code units compiles only after 1000 + 4 per unit calls or back-edges per site. A flat threshold for them cut pygments' interpreted instructions 69M -> 42M but added 40 ms of compile and gained no wall time (pygments 2.47 -> 2.55 s, pycodestyle 4.35 -> 4.34). The lever is smaller generated code (fewer units per opcode, shared helpers for repeats and charsets), which lowers both the threshold and the compile.


## Notes

**2026-11-11T18:16:31Z**

Branch luc-regex: a continuation or alternative over 60 lines is now one shared function instead of copies in line after every choice point; the largest generated function in the corpus patterns went from 1,933 lines (black's tokenizer) to 259, total size unchanged. black's regex instructions 5.3G -> 4.1G, total 7.4x -> 6.5x CPython; pygments 7.9x -> 7.5x. Generation itself costs 22-28 us per code word per entry (generator, % formatting, compile()) and is the next cost for one-shot programs.
