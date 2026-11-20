---
id: luc-hf1p
status: open
deps: []
links: []
created: 2026-11-20T12:09:06Z
type: task
priority: 4
tags: [threads, perf]
---
# specialize_seq shares a cache line with four runtime locks: no measurable scaling cost

_PyRuntime.specialize_seq (the inline-cache seqlock, runtime/lucifer.h) sits on one line with getargs_lock, specialize_lock, shared_names_lock and pic_lock. Measured 2026-11-20 on master 766c71d7 against a build that pads specialize_seq onto its own line (64 bytes either side): a program whose threads run attribute loads, stores and method calls at sites that meet eight classes, fixed work per thread, 300k iterations, 1/4/8 threads, five alternating rounds, medians in seconds (base / padded): default flags T=1 0.162/0.149, T=4 0.402/0.502, T=8 0.778/0.929; -X lucifer_jit=off T=1 0.390/0.365, T=4 1.175/1.031, T=8 2.119/1.959. The two modes move in opposite directions and T=1 moves as much as T=8, so the difference is the loaded host (12 CPUs, load ~14 on a test hold), not the line: no cost shown, nothing landed. Remeasure on a quiet host if a scaling profile ever names the line. (Scaling itself is weak here, about 5x the 1-thread time at 8 threads, in both builds.)

