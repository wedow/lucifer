---
id: luc-1kz0
status: open
deps: []
links: []
created: 2026-11-24T14:42:51Z
type: task
priority: 1
parent: luc-orn7
tags: [perf, jit]
---
# jit: compile only what pays back

On mostly cold programs the tier spends 15-44% of its instructions compiling (pyflakes 22%, black 35%, pygments 44%, stdlib imports 19%). Make the tier compile only what pays back, by a general cost model calibrated on the machine it runs on: predicted saving against predicted compile cost, for first builds, rebuilds and budget growth alike. No thresholds tuned to programs, no flags, no hard caps; a hot loop compiles no later than now by more than a stated bound.


## Notes

**2026-11-24 step 1: what each compile cost and saved (master f0136a06 + measurement counters)**

Method. Each program once, pinned, `perf record -e instructions:u` with frame-pointer chains, and LUCIFER_JIT_DEBUG=units extended (wip commit, not for landing) with: per unit its build phases, built and live IR, machine bytes split into blocks / slow paths / exits and by the code each IR instruction came from, inline depth, entries, and bytecodes its blocks ran (a counter per block weighted by the bytecodes the evaluator began in it); per code its first call and when it gave up an eighth of its threshold; the interpreter's bytecode total. Samples under compile() are charged to the unit whose build window holds them; samples whose chain reaches a unit before an interpreter frame are the unit's run (it and its helpers). Interpreted equivalent of a unit = its bytecodes x the program's JIT-off instructions per bytecode (base binary, whole run, 82-132). Payback = (equivalent - run) / compile. Check: unit bytecodes + interpreted bytecodes with the JIT on = 1.02-1.09x the JIT-off total (black 1.21, deltablue 1.31: blocks entered and left early count whole, so their savings read high). Programs: pyflakes, black, pygments (cli corpus), the stdlib-import line, and richards, deltablue, json_loads as pyperf workers (-l 4 -n 10, 1.2-2 s each; their harness and imports are most of their units).

| program | run G instr | compile G (% of run) | units | p25 / p50 / p75 / p90 payback | compile in units with payback < 1 | rebuilds' share of compile |
|---|---|---|---|---|---|---|
| pyflakes | 15.8 | 3.54 (22%) | 520 | 0.21 / 1.1 / 4.8 / 19 | 65% | 65% |
| black | 13.4 | 4.83 (35%) | 959 | 0.10 / 0.70 / 3.8 / 17 | 70% | 66% |
| pygments | 18.4 | 8.15 (44%) | 558 | 0.36 / 1.8 / 11 / 49 | 63% | 87% |
| imports | 1.71 | 0.34 (19%) | 172 | 0.46 / 1.8 / 5.9 / 14 | 70% | 30% |
| richards | 9.46 | 1.15 (12%) | 398 | 0.35 / 1.3 / 5.1 / 14 | 58% | 53% |
| deltablue | 6.57 | 1.82 (28%) | 547 | 0.41 / 1.3 / 5.8 / 23 | 51% | 64% |
| json_loads | 5.59 | 1.40 (25%) | 386 | 0.32 / 1.0 / 4.0 / 15 | 55% | 56% |

Per code (all builds of a root together): codes whose units saved less than they cost hold 51-56% of the compile on the cli programs and imports, 20-39% on the controls. Codes built exactly once: 85-96% of their compile never pays (10-14% of all compile). First builds by themselves rarely pay (pyflakes 93% of first-build compile): they are killed and rebuilt, and the replacement earns the saving.

Compile cost per IR instruction is not constant; it grows with unit size (instructions under compile() per built IR instruction): <100 ins 8.0k, 250-500 9.4k, 500-1000 13.7k, 1000-2000 16.0k, 2000-4000 35.6k, >=4000 77.2k. About 5M instructions per compile-ms. The policy's constants assume 2,500 per instruction (STALE_PER_INS, INS_PER_FAIL, HOT_CALLS_PER_INS), 3-30x too little, so its rebuild triggers fire on a fraction of what a rebuild costs. Profile of the compiler on pyflakes: build (evaluation and inlining) 26%, optimize 20%, regalloc 12%, emit 40% (deopt records `make_deopt`/`put` 8%); frame-state walks (lu_fs_each_frame, lu_fs_each_once, each_ref, lu_fs_slot) about 18% across all phases: every exit carries the frames of every inlined call it sits in (52-75 exits per 100 live IR instructions), so cost grows with size times inline depth.

Bytes: 110-140 bytes per live IR instruction; blocks 52-56%, their slow paths 23-33%, exits and stubs 11-23%. pyflakes' 35.7 MB is 71% kernel code inlined into callers: the generic attribute path (lookup_attr, generic_getattr, _type_getattr, getattr_hook, is_data, _module_getattr) is copied into 120-165 units, 18.7 MB, and is 50% of pyflakes' compile instructions (black 6%, pygments 2%, imports 19%, controls 5-12%).

Build causes (share of compile): budget growth (grown) is 53% of pygments' compile, one code (`_match_entry`) grown 4 times from 2.0k to 14.7k IR instructions, the last build 2.9G instructions for 0.08M bytecodes run; learned-since-build 17-33%, guard marks 10-31%. Codes rebuilt 10 or more times (pyflakes Checker.handleNodeLoad and addBinding, 16 builds each of ~1.6k instructions, for 1.4M and 0.7M bytecodes) lose; on the controls the steady codes rebuilt 10+ times still pay.

Heat at the decision: codes first compiled 1 s or more after their first call lose on 5 of 7 programs (75-91% of that compile wasted), those within 100 ms mostly pay; but the time taken by the last 7/8 of the threshold does not separate (pygments: fastest-heating bucket 96% wasted, slowest 14%): the waste is in what is built and rebuilt, not when. Kernel roots pay (pyflakes +18G net for 17% of compile); user roots carry 77-94% of compile. Codes warmed without waits (state made by an exit or an invalidation in an inlined copy) are under 1% of compile.


**2026-11-24 stage A design (coordinator-approved)**

Rebuild policy as a break-even ledger: a rebuild (giving up a guard, taking in what the root learned, taking a hot call in line, doubling the budget) is bought when the losses its absence has already caused (exits at the guard, passes through places built without the learned type, calls made out of line) have cost what the new build will. A unit that runs on then pays at most twice what the best policy in hindsight pays (the ski-rental bound: losses paid equal the build bought, and the build removes them), and a unit that stops soon pays little. No separate "earned" counter: the loss evidence accrues only while the unit runs, so it is the work proxy, and a counter of loop passes would write on every iteration of compiled loops and contend across threads.

Costs are machine instructions of this binary estimated from counts, so every decision is the same run to run: a build of n IR instructions costs 8,600 n + 5 n^2 (fitted per log2 size bucket on 3,016 builds of the seven programs: predicted/measured by bucket 2^2 0.52, 2^3 0.47, 2^4 1.00, 2^5 1.17, 2^6 1.16, 2^7 1.13, 2^8 1.10, 2^9 0.96, 2^10 0.97, 2^11 0.63, 2^12 1.14; whole-program total 0.86); an exit 4,700 (deopt path measured at about 1,200 on pyflakes: 39.3M instructions for 33.1k exits; the rest is the entry back and the interpreted stretch, which timing put at 1.7 us); a call out of line or a stale pass 300. Exits needed to rebuild a 400-instruction unit: about 860 (was max(50, 200)); stale passes 13.4k (was 3,200); calls at a hot site 13.4k (was 3,200); growth when all out-of-line calls together cover a build twice the size (was: 4,096 entries, at most 4 times).

A feedback site keeps the operand kinds its last build relied on when the interpreter has met new kinds there since, unless the site's guard has failed as often as a build without it is worth (lu_jit_site_kinds): a rare kind keeps its exit instead of widening the guard; a kind that turns common pays for the rebuild that takes it.

**2026-11-24 stage A, continued (wip, not landable)**

- Exit value: an exit now costs DEOPT_INS (deopt path and the entry back) plus BYTECODE_INS for each bytecode of the exit's frames' own code that the build evaluated after the exit's place (struct lu_deopt rest, counted per frame by the evaluator: ctx evaluated, lu_fs seq). Calls the interpreter makes after the exit enter their own units, so inlined callees' bytecodes are not counted. A guard at a loop unit's entry loses the loop's whole pass; one deep in the pass, little. Guard losses accumulate in instructions (fails arrays and lu_prof fails are uint64 ledgers) against build_cost. A variant that counted inlined callees' bytecodes too overvalued exits ahead of an inlined call (rare: run@65, about 500 bytecodes of rest for an exit whose callee then runs compiled) and was dropped.
- Constants measured with tools/jitcost (new; perf instructions:u on generated programs; fails when the model is off by more than 2x): interpreted 152 and compiled 3.4 instructions per bytecode of a plain arithmetic loop (BYTECODE_INS 100: whole programs interpret at 82-132); an exit with half a 19-bytecode pass to run 4,600-5,250 (DEOPT_INS 3,800: the earlier 1,200 left out the entry back); builds of generated functions with inlined helpers, attribute loads and branches: 53 IR 6.4k/ins, 142 5.0k, 473 4.7k, 1,538 9.4k, 5,378 25.8k. BUILD_INS 8,600 put the model 2.3x over the 473 bucket; 6,500 keeps it within 1.06-1.87 there and 0.57-1.1 on the step-1 programs' 3,016 builds (2^11 bucket 0.57). A third term does not fix 2^11: exits*depth fits at a negative weight (2^11 still 0.63), built*depth worse (0.49); the bucket is 44 units, mostly pygments' generator units at 50-110k per instruction.
- Polymorphic attribute caches: a unit built for fewer shapes than its cache now holds left at one generic load in 64 (helpers.c h_pic_same), outside the ledger; churn's hot loop was rebuilt seven times for it in its second half. Its generic loads are now stale passes of the root (lu_jit_stale_pass), like a learned type's: the unit is built again when they have cost the build. A stale pass is now counted only at a place that learned something since the unit's build (lu_prof at), not at every place of a root that learned anything.
- lu_jit_site_kinds (a site keeps the kinds its last build relied on until its guard's fails pay) is removed: a site behind an earlier failing guard never fails itself, so phases kept speculating at sites the interpreter had seen floats at for thousands of calls. phases, common and rare-at-the-time passed without it.

Lanes (2 sizes each, deterministic): PASS phases, grown, common, marked, shadowed, large, recursive, turns, attrorder, inlinedpoly, rebind, seqkinds, raresub, kept, hoisted. FAIL:
- churn 8,049 -> 13,167 IR (master 23,851 -> 28,511). The second half's builds: main@78 three times (an old unit at a site another root marked, then marks at handle_load@187 and @197: guards failing on about a quarter of handle_load's calls whole-run, which pay their build after about 3,400 exits each, past iteration 20,000), handle_load's own first unit, Binding.redefines once for a mark. Each is bought by losses already paid; with the 1,000 slack the lane asks a steady failure to be given up within N, which the break-even rule does not promise. Not yet decided: change the slack (with this reason) or reshape the lane.
- rare 506k -> 947k helpers. The noborrow guard at run@45's entry (fails on every entry) now marks after 543 fails, before the tuple's first call (599); the rebuild profiles total@4 and total@5, learns list and list_iterator, and its rebuild iterates total's loop with a helper per item (about 21 per iteration). Not yet known why the third build is generic there; total@5 is also marked nospec later (358 entries, the tuple's OSR entries at about 8.3k each).
- megaiter 3.50M -> 6.56M compiled calls. hot@36 calls str.join, set() and join(filter) out of line for want of budget, once per iteration each; their callees' loops make a compiled call per item. Master grows the budget (fixed trigger) and takes them in line; the ledger values a call out of line at CALL_COST (300), so growth needs 137k calls and hot sites 49.5k at one site: neither comes within 40,000 iterations. The loss of a call left out of line is mostly what the callee then does generically per item, which no count the ledger keeps shows. This is stage B's question (inline what shrinks); stage A has no general valuation for it yet.

**2026-11-24 stage A: rare fixed, out-of-line calls valued by their callee (wip)**

- rare: after run@45's entry guard (noborrow) was given up, its rebuild iterated total's loop through a helper per item. GET_ITER takes the iterable's kind from the interpreter's FOR_ITER cache only; total's loop had run only in compiled code (the cache cold), and later met a tuple once (the cache mixed), so every rebuild went generic. GET_ITER now falls back to what the root learned of the iterable (lu_ev_learn_type): a guard on list whose rare tuple exits pay through the ledger. rare passes (87,822 -> 89,802 helpers), as do common, seqkinds, raresub, phases, hoisted, unpack.
- Out-of-line calls (coordinator design): a site's calls are valued at CALL_COST plus the callee unit's losses per entry (its exits' exit_cost, accrued on the exit path while its entries are counted, and its own out-of-line calls at CALL_COST), read at the decision only and never added to the caller's ledger, so a loss is not counted twice up a call chain; past ENTRIES_SEEN the last value stands. The build records each refused site's callee code (a constant of the unit). large, recursive, turns, grown, rare still pass. It does not fix megaiter (3.50M -> 6.59M compiled calls): str.join's and set()'s units lose per item through generic iteration helpers (next_item, then a compiled call of the deque iterator's __next__), which are neither exits nor calls left out for budget, so the callee's ledger shows nothing. Stopped there, as asked: a general fix needs the callee's generic work counted (helper calls of generic paths are not in any ledger).
- churn (not done): to be reshaped, per the coordinator, to assert the ledger's invariant from the event log instead of "no builds after N": each rebuild's predicted cost is at most the losses its code accumulated since its previous build, and IR built at 2N is at most first builds plus total losses / cost per IR instruction times a small stated factor. Needs the kill events to carry the losses paid and the predicted build cost (jit.c kill/event), and a jitcheck lane that parses LUCIFER_JIT_DEBUG=units output. Then the ticket must list churn's remaining builds and why each is right (see the previous note for the list).

**2026-11-24 stage A: generic paths in the ledger, churn's invariant, gate and whole-run table (not landable: richards steady state)**

- Generic paths are losses (coordinator design). The helpers that stand where a build for the operands' classes would have a guard and an in-line path count a pass of their unit: for_iter, load_attr/load_method/store_attr, binary_op/compare_op when an operand's class was not known to the build (a bit in the operator word), and a call of a callable the build did not know (new helper call_any, which keeps call's direct path; the direct path writes nothing). The helper finds its unit through the root frame's published state (jit_desc). lose() adds to the unit's ledger with relaxed stores and stops writing once it covers the unit's own build (lost_entries records after how many entries, for the per-entry value callers read); exits use it too. Constants measured by tools/jitcost (a loop over one class against eight subclasses in turn; -X lucifer_jit_stats now counts generic passes): iteration 966 per pass (ITER_INS 1000), attribute 550 (550), operator 1,865 (1,900), call 489 (500). With the first guesses (150/100/100/150) megaiter passed under jitcheck but not with stdout to /dev/null (3.50M -> 6.59M compiled calls); with the measured values it passes both ways (null 2.66M -> 2.70M, pipe 1.46M -> 1.50M; master 1.42M -> 1.44M).
- Not done as asked: generic passes are not in the unit's own rebuild ledger. A first version counted them as stale passes whenever the root had learned anything since the build; on megaiter that rebuilt hot@36 for places unrelated to the generic site (each rebuild inlines new code whose places then learn, which re-arms it). A helper does not know its profile place; the existing per-place stale passes (CALL_COST each) remain the own-rebuild path.
- CALL_COST 1500 (was 300), NOT measured by tools/jitcost yet: at 300 the typed lane failed (892k -> 1.01M helpers; master flat): fill@96's store left out of line into the kernel's setitem dispatcher, whose per-type dispatch is in-line type tests that no count shows, so the call was valued at the call alone and never taken in line. 1500 passes typed, grown, megaiter, churn, large, recursive, turns, common, rare, phases. A jitcost measurement of an out-of-line call against its in-line form is still to build.
- churn reshaped (ledger lane, tools/jitcheck check_ledger): kill events bought by the ledger carry `paid P cost C` (learned-since-build: the root's stale ledger; learned-type/kind-fails: the profile's fails; mark-* and old-unit-*: the site's fails; hot-site: the dearest site; grown: all sites, against a build twice the size). The lane runs at 40,000 with the units log and asserts every such kill has C <= P and that all IR built <= first builds + 2 x paid / BUILD_INS: 2,397 first, 10,794 rebuilt, bound 36,636. Builds after N=20,000: Binding.redefines twice (mark-nospec @26 paid 6.30M, @37 6.90M: its isinstance and name tests meet each new class, one every 600 iterations), main@78 twice (mark-nospec handle_load@187 14.83M, @197 14.86M: the guards in the inlined handler fail on about a quarter of its calls as classes turn over). Each was paid by exits at its own guard and makes the site generic, so it is not bought again; the run ends with dep-keys kills at teardown in both halves.
- Gate: focused kernel programs (44 programs + 7 thread programs: the jit_* policy, iteration, attribute, call and operator programs) 63/63 in jit-off, plain, jit, jit-deopt; threads 7/7 in handoff. Full tools/jitcheck three times: 0 failing each; counts identical but `inline` (160 in run 1, 159 in run 2 of the same binary; alone it is 160 each time on the branch, 159 on master) and the perf lane's sample count.
- Controls' ledger (units log, -l 6): richards 100 ledger kills, none unpaid, rebuilt IR 16,179 against a bound of 41,789; deltablue 101, none unpaid, 16,234 against 41,920. Time to the best build: each rebuild is bought when the losses paid equal its predicted cost, so a hot unit reaches its last build after at most the sum of the builds bought plus equal losses, 2x what the builds cost, not a count of entries.
- Whole run, core leases, instructions:u, min of two runs per arm (spread <= 0.2%), master 0a564ad9 against the branch:

| program | master G | branch G | compile ms m/b | units m/b | IR m/b | code MB m/b | steady m/b |
|---|---|---|---|---|---|---|---|
| pyflakes | 15.909 | 15.483 | 634/468 | 520/517 | 299k/244k | 38.4/31.2 | |
| black | 13.023 | 12.939 | 738/720 | 962/972 | 292k/287k | 32.6/32.2 | |
| pygments | 17.430 | 15.858 | 1126/826 | 556/554 | 196k/178k | 21.7/19.3 | |
| imports | 1.644 | 1.647 | 71/56 | 172/175 | 29k/30k | 3.7/3.7 | |
| richards -l 12 | 7.975 | 8.377 | 213/198 | 406/398 | 104k/96k | 11.9/11.2 | 159.8M/180.9M a loop (+13%) |
| deltablue -l 2 | 4.382 | 4.198 | 249/209 | 425/413 | 100k/92k | 10.7/10.3 | |
| json_loads -l 12 | 5.108 | 4.926 | 271/232 | 392/389 | 103k/98k | 11.6/11.1 | 28.48M/28.27M |
| regex_v8 -l 2 | 18.268 | 12.861 | 1532/616 | 664/643 | 250k/184k | 28.4/21.0 | |
| deepcopy -l 2 | 4.120 | 3.959 | 200/178 | 370/366 | 83k/80k | 9.6/9.4 | |
| AWFY Richards 20 | 3.143 | 3.175 | 19/23 | 40/42 | 8.0k/9.3k | 0.9/1.1 | 141.1M/141.9M an iteration (+0.6%) |
| AWFY DeltaBlue 20 | 0.594 | 0.604 | 28/27 | 63/60 | 11.4k/11.8k | 1.3/1.3 | 6.40M/6.05M |
| AWFY Json 20 | 2.868 | 2.225 | 290/140 | 124/118 | 94k/74k | 7.8/6.3 | 90.1M/70.3M |

  Steady state from two sizes (-l 12 and 24; AWFY 20 and 60 iterations); -l 2 against 6 still held rebuilds, so deltablue, regex_v8 and deepcopy have no steady figure yet. Compile share is the tier's compile time (ms), not instructions.
- richards' steady-state loss (+13%), at the unit level: master keeps speculating at schedule@150 and schedule@160 (the dispatch on the current task's class): it rebuilds there for what the root learned (learned-since-build schedule@150) and ends with Task.runTask in schedule's unit (955 IR). The branch gives the guards there up first (mark-nospec schedule@150, then @160, each paid by its exits), so its last schedule@17 is 329 IR, reaching Task.runTask (610 IR, its own unit, 5.2M helper calls) through a generic call on every step. The ledger ranks a guard's exits and the stale passes of the place that is learning the new classes separately; an exit at a guard whose place is learning its types should go to the learned rebuild, not to the mark. Not fixed.

**2026-11-24 stage A, fourth pass: richards, CALL_COST measured, determinism, table (not landable: typed, DeltaBlue)**

- Richards (task 1). The cause was narrower than marks against learning: the polymorphic cache at schedule@150 learned each task class; the unit, built for fewer shapes, took its generic way on those passes (stale passes of the place, valued at a call each), and that way's value is a bound method, so the callee guard at schedule@160 failed on every such pass. Those exits bought mark-nospec @160 long before the stale passes bought the learned rebuild, and @160 went generic. Rule now (jit.c lu_jit_note_exit): a guard's exit in a unit whose stale ledger is non-zero (a pass of the unit went through a place that met a class its build did not know; a place past its polymorphic limit, poly, or a cache that stopped looking, learns nothing more and charges nothing) is paid into that unit's stale ledger and buys the learned rebuild (relearn), not a mark. A unit whose ledger is empty has every class its places met: its failing guards fail on classes it already covers, and their exits buy the mark as before. Bound: a diverted rebuild needs a pass through a place that learned since the unit's build, and a place learns at most LU_OPCLASS_WAYS classes before it goes poly, so rebuilds per place are at most its ways plus one. The stale ledger moved from the root to the unit (lu_unit.unlearned, against the unit's own build cost): a root ledger was emptied by any entry's build while an old entry (deltablue remove_propagate_from@191) kept charging it, and exits of fresh entries then rebuilt them in a loop (21 rebuilds in deltablue -l 64). Lane `dispatch` (tests/kernel/jit_class_dispatch.py: four task classes woken one after another share their step method) fails before (75 compiled calls a loop iteration of 40 steps; master 35) and passes after (35: the step in line, its call of the class's work out of line).
- CALL_COST (task 2). tools/jitcost `call`: the same one-line function called once per entry of its caller's unit, out of line (the caller's budget spent by a straight line before the call) and in line: 402 instructions a call (argument and result boxed for the call). CALL_COST is 420. A stale pass is now valued at CALL_COST per helper call the build made for the place's instruction (lu_ev_price_places, lu_jit_prof_built), the place's report included. Tried and dropped: valuing an out-of-line call by the helper calls the callee's unit makes on values from its arguments. It fixed megaiter at the measured CALL_COST but every method's helpers on `self` count, the value compounds through a recursive descent's calls, and budget growth ran away: AWFY Json steady 85.5M -> 218M an iteration, _Parser._read_array grown five times to 30k IR (paid 8.5G for a 4.8G build), pygments +25%. Also dropped: valuing a callee with no live call-entry unit by its loop units. Without both, megaiter passes at 420 (the per-unit stale ledger changed its rebuilds).
- typed still fails at 420 (429,235 -> 459,023 helpers; master flat). The memoryview-exported 'd' array's fill@33 is built at 1,227 IR (the whole __setitem__ slow path in line, its typecode place not yet run), learns the typecode at array.__setitem__@37, is rebuilt after its stale passes pay 15.5M (1,125 IR), learns it again at the second store's place (a new inline-context key in the new build) and is rebuilt after another 13.6M (233 IR). A stale pass there is a profile report plus a call of the typed_set primitive with the int boxed: 2 calls, 840, which is about what it loses. The ledger buys both rebuilds after 20,000 iterations; master's eager rule (8 stale passes per IR instruction) bought them before. The lane's demand is stricter than break-even for a unit whose rebuild is predicted at its own size (1,227) and comes out at 233. Not resolved; options for the coordinator below.
- Generic helpers (overhead found on AWFY Richards). Finding the unit per generic pass through the thread state cost 0.6% (perf: lu_jit_generic 0.72% of instructions, 20k passes an iteration); the evaluator now passes the unit to the six fixed-arity generic helpers (call_any keeps the running activation: its argument array is the interpreter's), and the pass stops at a load and a test once the unit's ledger has settled (`settled`; with -X lucifer_jit_stats every pass is counted). With counting compiled out AWFY Richards reads +0.01% against master, with it +0.17%. A build restart used to clear lu_func.unit (reset): fixed.
- Determinism (task 3). The `inline`/`repeat` counts vary by one between runs of one binary, on master too: jit_inline_paths.py without its thread, master reads box_int 32, 33 or 34 over ten runs with ASLR off and one OS thread, while compiles, entries and calls are identical in every run. The box_int C path is taken when the carrier's free list and page are both empty, and the free-threaded collector decides whether to collect by the process's RSS (runtime/interp/gc.c should_collect_mem_usage, VmRSS, which transparent huge pages move run to run), so the heap at a given box differs. No compile decision of the tier depends on it; it is outside this ticket. Poll exits no longer feed a unit's losses (the runtime takes them when it asks), which removed the one time-driven input the ledger had.
- Steady state (task 4): rebuilds do not stop in either arm by -l 128 for deltablue, by -l 64 for regex_v8 and deepcopy (master deltablue compiles 579 at -l 32, 589 at -l 64; the late builds are the harness's own code after the timed loops: json output, re). The steady columns below are differences of two loop counts after the hot code's last build. Time to the best build, as entries the benchmark's own functions' units served before their code's last build (units logs, master/branch): richards -l 12 21,658/165,999 (HandlerTask.fn 4,096/76,321: master grows its budget at 4,096 entries, the ledger when the calls left out have paid the larger build), deltablue -l 16 60,944/104,097; builds of their own code 68/75 and 199/161.
- Table, instructions:u, core leases, one measurement a lease, min of two runs per arm (spread < 0.5%); compile ms from one unpinned stats run per arm (load inflates both arms). Master 0a564ad9 against the branch:

| program | sizes | master G | branch G | compile ms m/b | units m/b | IR m/b | code MB m/b | steady m/b (a loop or iteration) |
|---|---|---|---|---|---|---|---|---|
| pyflakes | | 15.495 | 14.451 (-6.7%) | 907/579 | 520/479 | 299k/209k | 38.4/26.4 | |
| black | | 12.780 | 12.671 (-0.9%) | 1101/1018 | 962/941 | 292k/278k | 32.6/31.0 | |
| pygments | | 16.951 | 13.187 (-22.2%) | 1334/675 | 556/515 | 196k/157k | 21.7/17.1 | |
| imports | | 1.629 | 1.620 (-0.6%) | 96/81 | 172/163 | 29k/28k | 3.7/3.6 | |
| richards | 12 24 | 6.990 | 7.272 (+4.0%) | 314/348 | 409/395 | 104k/109k | 11.9/12.5 | 121.48M/121.32M (-0.1%) |
| json_loads | 12 24 | 4.979 | 4.917 (-1.3%) | 404/430 | 395/366 | 104k/100k | 11.7/11.3 | 25.99M/25.68M (-1.2%) |
| deltablue | 64 128 | 9.612 | 10.245 (+6.6%) | 695/634 | 589/568 | 203k/205k | 18.9/19.4 | 35.22M/36.15M (+2.6%) |
| regex_v8 | 8 16 | 30.776 | 25.149 (-18.3%) | 2678/1416 | 842/803 | 310k/250k | 35.0/28.9 | 1107.4M/959.7M (-13.3%) |
| deepcopy | 16 32 | 4.457 | 4.444 (-0.3%) | 310/332 | 424/399 | 101k/102k | 11.5/11.7 | 11.39M/9.87M (-13.4%) |
| AWFY Richards | 20 60 | 2.721 | 2.757 (+1.3%) | 24/27 | 40/39 | 8k/9k | 0.9/1.0 | 120.05M/120.25M (+0.2%) |
| AWFY DeltaBlue | 20 60 | 0.563 | 0.599 (+6.4%) | 31/37 | 61/50 | 10k/11k | 1.1/1.3 | 6.24M/7.28M (+16.7%) |
| AWFY Json | 20 60 | 2.743 | 1.802 (-34.3%) | 433/147 | 123/105 | 95k/54k | 7.9/4.6 | 83.78M/50.95M (-39.2%) |

  (whole-run sizes: the first size; richards' steady state is per loop of -l, as the earlier richards row's 159.8M was a worker loop with the harness's two values; the measurements here run without -X lucifer_jit_stats.) AWFY DeltaBlue's steady loss is convergence, not a worse final build: guard exits 11,777 against 4,309, most in units the ledger has not yet bought a rebuild for (binary_op@1, 1,457 IR, built once, learns list/float/int afterwards; 2,370 exits at its call_special@52 guard, paid into its stale ledger, 12M of its 20M build by iteration 60; master rebuilt it at once and marked the guard at 50 exits). deltablue -l 64..128 likewise (more exits and deopts: eval_frame +0.8%, lu_jit_deopt +0.3% of instructions).

**2026-11-24 stage A, fifth pass: the coordinator's rulings 1 and 2 (not landable: deltablue)**

- Ruling 1: a stale pass is valued at the measured generic loss of its place's kind (FOR_ITER 1,000, LOAD_ATTR/STORE_ATTR 550, BINARY_OP/COMPARE_OP 1,900, CALL/CALL_KW 500; other instructions a CALL_COST per helper call as before: lu_jit_prof_built). Alone it fixes neither: typed 512,777 -> 597,563 helpers (fails), AWFY DeltaBlue 60 still 11,801 guard exits.
- Ruling 2: the learned rebuild is predicted at the stale build less the IR the build emitted after a report of a place that had met nothing, in that place's inlined frame (lu_ev_price_rest, lu_prof rest, lu_unit relearn: the least over the places whose stale passes charge the unit; exits diverted to the stale ledger pay against it too). typed passes (364,789 flat; master 429k flat), dispatch passes, and the lanes churn, rare, common, phases, megaiter, grown, large, recursive, turns, kept, marked, shadowed, attrorder, inlinedpoly, rebind, seqkinds, raresub, hoisted, callees, classes, cmethods, inline, repeat pass (run on the first variant below). AWFY DeltaBlue 60: 5,819 guard exits (master 4,309, before 11,777).
  - A first variant counted the rest in the root frame too: it predicts most of a unit away (predicted/actual cost of the next build, median 0.31 on pyflakes' 190 relearns, 0.38 on deltablue's 180) and cost pyflakes its gain (-0.7%) and AWFY Richards whole run (+6.6%). Inlined frames only: median 0.79 and 0.91 (pyflakes 64 of 157 under half: predicted 370M against 902M built). The stale build's own size predicts at a median of 0.88 (quartiles 0.69-1.01). The rest of a frame bounds what learning folds from above; the IR of the arms that the learned class makes unreachable is not attributable without building them.
- Table (instructions:u, core leases, master as measured in the fourth pass; branch one run each, a second round queued behind a host hold; regex_v8 from the first variant):

| program | sizes | master G | branch G | steady m/b |
|---|---|---|---|---|
| pyflakes | | 15.496 | 14.808 (-4.4%) | |
| pygments | | 16.961 | 13.484 (-20.5%) | |
| richards | 12 24 | 6.990 | 7.101 (+1.6%) | 121.21M/121.04M (-0.1%) |
| deltablue | 64 128 | 9.613 | 10.107 (+5.1%) | 35.12M/36.43M (+3.7%) |
| regex_v8 | 8 16 | 30.760 | 25.345 (-17.6%) | 1107.5M/989.0M (-10.7%) |
| AWFY Richards | 20 60 | 2.721 | 2.755 (+1.3%) | 120.05M/120.25M (+0.2%) |
| AWFY DeltaBlue | 20 60 | 0.488 | 0.503 (+3.0%) | 6.23M/6.19M (-0.7%) |
| AWFY Json | 20 60 | 2.737 | 1.866 (-31.8%) | 83.92M/46.16M (-45.0%) |

- pyperformance deltablue is not slow learning but slow marking. Planner.add_propagate@1 (deltablue -l 16, units log): its guard at @63 fails on nearly every entry; the exits go to the stale ledger (the unit has learned places), buy a learned rebuild after 1,987 entries (7.7M), the rebuild fails at the same guard, and its exits then pay the mark from zero: mark-nospec @63 after 2,911 more entries (11.6M). Master marks it after 266 entries. Guard exits -l 16: Planner.add_propagate@63 3,980, make_plan@64 3,776, Constraint.add_constraint/incremental_add@18 3,343, Constraint.satisfy@19 3,001, add_constraints_consuming_to@39 2,918 (master's largest: 1,115). Rebuilds do not stop by -l 128 in either arm, so the steady column carries this.
- Candidate (not built, for the coordinator): an exit diverted to the stale ledger pays its site's mark ledger as well (either purchase removes the loss, so the loss counts toward both and the first bought resets the other), so a guard that keeps failing after the learned rebuild is marked on what it already lost.

**2026-11-24 stage A, sixth pass: exit attribution by the guard's feeding place (not landed: dispatch fails)**

Coordinator's rules, built and not kept:
- (1) A guard's exit pays the learned rebuild only when the value it tests was made by (or is the generic result of) a profile place that has met a class since the unit's build; every other guard exit pays its site's mark. No loop: a place learns at most LU_OPCLASS_WAYS classes before it goes poly, so the rebuilds its feeding buys are at most its ways plus one; a site is marked once.
- (2) What a guard's exits paid towards a learned rebuild is held per site (code, pc) and carried once into its mark ledger when the guard fails again with its feeding place unchanged since the rebuild, so a loss is not paid twice.
- Built as: each IR instruction records a place of the bytecode instruction it was built for (the first lu_prof on that instruction's IR); a guard's feed is the place of the instruction defining its tested value, else its own instruction's; the deopt record carries it; the unit records its root's learned count at build; exits test feed->at against it; jc->held per code unit.
- Result: typed, churn, rare, common, phases, megaiter, grown pass; dispatch fails (1,438,834 helpers at 20,000 iterations against 674k with the fourth-pass rule). Every guard exit in schedule's units had no feed: the failing guards are attribute-load guards (LOAD_ATTR at schedule@12, @34, Task.step@21/27/51/63) and the callee guard at schedule@58, and the place that learns there is the polymorphic attribute cache (lu_jit_pic), which is not a lu_prof on the IR and whose learning does not move lu_prof.at. The feed has to name the cache too: a guard tests a value made by, or is in, an attribute instruction whose cache holds more entries than the build used. That needs the cache and its count at build on the deopt record (or a lu_prof standing for each cache). Not built; the work is kept outside the branch.

**2026-11-24 stage A, final: what lands, gate, table**

Rules, each with its argument:
- Break-even: a rebuild (giving up a guard, taking in what the root learned, taking a call in line, a larger budget) is bought when the losses its absence already caused cover the predicted cost of the new build. Ski rental: a unit that runs on pays at most about twice what the best policy in hindsight pays; one that stops soon pays little.
- Exit valuation: an exit costs the deopt path plus the bytecodes the interpreter runs until the unit is entered again (exit_cost); the constants are what tools/jitcost measures (bytecode, exit, build by IR size, a call out of line, a generic pass by place kind), each within a factor of two.
- Generic-pass losses: a pass through a place built without what it learned since loses what the generic path of its kind does (FOR_ITER 1,000, LOAD_ATTR/STORE_ATTR 550, BINARY_OP/COMPARE_OP 1,900, CALL/CALL_KW 500; else CALL_COST per helper call), paid into the stale ledger; the learned rebuild is predicted at the build less the IR emitted after a report of a place that had met nothing, in inlined frames.
- Stale ledger per unit: a guard's exit in a unit whose stale ledger is non-zero pays the learned rebuild, not a mark; the ledger is the unit's own (a root's was emptied by any entry's build while an old entry kept charging it). Bound: a place learns at most LU_OPCLASS_WAYS classes, so rebuilds per place are at most its ways plus one.
- Quiet rule: a learned rebuild (learned-since-build, learned-type/kind-fails) is made only when it is paid for and what the unit paid since a place of its root last learned (a place meeting a class, or an attribute cache gaining an entry) covers half its predicted cost. A rebuild made while the places are still learning is wasted with certainty when the next class arrives; waiting costs at most half a build more than not waiting. Places past their polymorphic limit learn nothing more. Marks and growth are unchanged.
- Value sharing: a build keeps, per SSA value, the classes its places learned; a later place on the same value relies on no narrower set (the union; the polymorphic limit applies to it), so an exit at an earlier place on the value cannot teach the places after it one class by chance. No run-time cost. The exact class inside an earlier place's arm is not used: the union at the join only.
- Ledger lanes: the ledger does not buy rebuilds after a dependency changed (dep-keys, dep-value), so their IR is outside the ledger's bound in tools/jitcheck. classtests now checks the ledger and a bound on its helpers (flat between sizes; at most twice the 377,636 this build makes at 20,000 iterations; master 116,504). Its flat-helpers count encoded the old policy (watch long, rebuild once).

Facts found on the way: the builder asks only the place's own profile, keyed by the chain of inlined frames (lu_ev_learn_type, lu_ev_learn_const, lu_ev_kernel_arms); after an exit the interpreter writes no profile place. The class tables are dead code on master: lu_jit_want_classes has no callers, so lu_jit_observe_classes and lu_jit_opclass_note always return at once.

Gate on the measured tree (before the rebase onto 12e0fa43): focused kernel programs (46 + 7 thread programs, 64 checks) pass in jit-off, plain, jit and jit-deopt, threads 7/7 in handoff; full tools/jitcheck 0 failing (classtests 377,636 flat, its ledger 61,589 first, 11,910 rebuilt, 94.6M paid: at most 90,702; churn 2,820 first, 10,727 rebuilt: at most 33,016; intrange 256,251/436,251); tools/jitcost PASS. The same gate on the rebased tree (on 12e0fa43): the same programs and lanes pass, jitcheck 0 failing, jitcost PASS.

Table (branch on master ca02da54, the measured tree; master the same base): instructions:u, core leases, one measurement a lease, two runs per arm alternated, min (spread at most 0.9%, under 0.1% but deepcopy and CD); compile ms, IR and code from one -X lucifer_jit_stats run per arm, pinned. Steady: the difference of the two sizes per loop of -l (pyperformance) or per iteration (AWFY), after the hot code's last build.

| program | sizes | master G | branch G | steady master/branch | compile ms m/b | IR m/b | code MB m/b |
|---|---|---|---|---|---|---|---|
| pyflakes |  | 15.490 | 14.836 (-4.2%) |  | 510/446 | 299k/242k | 38.4/30.7 |
| black |  | 12.781 | 12.959 (+1.4%) |  | 712/751 | 292k/307k | 32.6/34.0 |
| pygments |  | 16.961 | 13.931 (-17.9%) |  | 1026/497 | 196k/177k | 21.7/20.0 |
| imports |  | 1.629 | 1.644 (+0.9%) |  | 68/54 | 29k/30k | 3.7/3.8 |
| richards | 12 24 | 7.004 | 7.119 (+1.6%) | 242.98M/243.93M (+0.4%) | 203/217 | 104k/107k | 11.9/12.5 |
| deltablue | 64 128 | 9.612 | 10.072 (+4.8%) | 70.44M/74.91M (+6.3%) | 354/394 | 203k/206k | 18.9/19.4 |
| json_loads | 12 24 | 4.979 | 4.797 (-3.7%) | 52.00M/57.97M (+11.5%) | 252/226 | 103k/97k | 11.6/10.9 |
| regex_v8 | 8 16 | 30.776 | 25.152 (-18.3%) | 2214.88M/1972.96M (-10.9%) | 1768/915 | 309k/256k | 35.0/29.6 |
| deepcopy | 16 32 | 4.456 | 4.311 (-3.2%) | 22.51M/21.54M (-4.3%) | 231/214 | 101k/98k | 11.5/11.3 |
| awfy-Richards | 20 60 | 2.721 | 2.755 (+1.3%) | 120.05M/120.25M (+0.2%) | 16/19 | 8k/9k | 0.9/1.0 |
| awfy-DeltaBlue | 20 60 | 0.488 | 0.503 (+2.9%) | 6.24M/6.22M (-0.3%) | 22/24 | 10k/11k | 1.1/1.3 |
| awfy-Json | 20 60 | 2.742 | 1.814 (-33.8%) | 83.78M/47.44M (-43.4%) | 315/110 | 95k/58k | 7.9/4.9 |
| awfy-Havlak | 20 60 | 336.987 | 335.954 (-0.3%) | 17353.22M/17276.04M (-0.4%) | 550/404 | 183k/148k | 17.3/14.1 |
| awfy-CD | 20 60 | 0.931 | 0.977 (+4.9%) | (an iteration at inner size 1 is about 0.2M) | 19/25 | 12k/17k | 1.6/2.1 |

The json_loads steady loss (+11.5%; the fourth pass measured -1.2% before the quiet rule and value sharing) and deltablue's (+6.3%) are not diagnosed.


Known losses:
- pyperformance json_loads steady state +11.5% (52.00M against 57.97M a loop), not diagnosed.
- pyperformance deltablue steady state (fifth-pass note): guards unrelated to learned places pay the stale ledger first, then their mark from zero.
- jit_isinstance_growth under -X lucifer_jit=always (instructions:u, two runs a side): master 1.092G at 20,000 iterations and 1.099G at 40,000; the branch 1.399G (+28.1%) and 1.410G (+28.3%); 521 against 347 instructions an iteration once rebuilding stops; compile 148 against 130 ms, IR 83.5k against 75.3k. Cause: exits at isinstance@96 pay learned rebuilds where master bought mark-nospec there after 674 entries (master's final unit then inlines isinstance at run@285; the branch's calls it), and a mark at the loop header run@98 (range_iterator learned after it). run@98 is built 9 times, master 2.

Follow-ups:
- Exits attributed by cause (sixth-pass note), with the attribute caches as feeds as well as lu_prof places.
- The evidence rule (rely on a place's one type only after a second report of it, counted where reports are written): classtests total +15% against master with it, +28% without, run@98 built 5 times. Not landed: on jit_int_range_prims a place confirmed by its second report (mixed_products@73) stays in the profiling form in a final build the ledger never rebuilds (intrange 298,758 -> 518,758 helpers at 20,000/40,000 iterations, 11 an iteration against the lane's 9).
- Remove the dead class tables (lu_jit_want_classes, lu_jit_observe_classes, lu_jit_opclass_note and their interpreter calls): a separate small ticket.

**2026-11-24 diagnosis: the json_loads and deltablue steady-state losses**

Method: the same two binaries as the table (master ca02da54, branch on ca02da54); instructions:u on core leases, one measurement a lease; per-call figures from differences of run lengths; units logs with a marker after each benchmark call (the pyperf worker with its bench function wrapped, and a bare driver calling the function N times); exit tallies differenced between run lengths. A pyperf `-l N` worker makes 2N calls (warmup and value), so the table's `-l 12`/`-l 24` window is calls 24-48.

1. Neither figure is a final build. In both programs both arms are still rebuilding inside the measured window, and the sign of the difference changes from window to window:

| window (calls) | json_loads m/b per call | deltablue m/b per call |
|---|---|---|
| 24-48 (pyperf) | 26.64M/28.44M (+6.8%; table +11.5%) | |
| 48-96 | 24.58M/28.30M (+15.1%) | |
| 96-192 | 27.20M/24.68M (-9.3%) | |
| 128-256 (pyperf; the table's) | | 35.22M/37.45M (+6.3%) |
| 192-384 | 22.07M/22.43M (+1.7%) | |
| 256-512 | | 33.87M/33.83M (-0.1%) |
| 512-1024 | | 34.45M/33.37M (-3.2%) |
| 768-1536 (driver) | 21.41M/21.67M (+1.2%) | |
| 1024-2048 | | 33.19M/33.55M (+1.1%) |
| 128-2048 together | | 33.75M/33.80M (+0.1%) |

- json_loads (driver): the scanner units' last builds come at call 305 on master (loads, learned) and at 474 on the branch (_scan_once grown), then 1,823 (_scan_once grown again); JSONDecoder.decode is still rebuilt at 1,355 and 1,661 on master, 1,029 on the branch. Master grows _scan_once at calls 23 and 28 and Scanner.__call__ at 84 and 156 (the fixed 4,096-entry trigger); the ledger buys the same growth at 474 and 1,823, and at 96 and 381 (paid 44.9M against 42.1M, 140.6M against 115.3M). Converged, the branch is within 1-2% of master (+1.2% at calls 768-1536); the same window under -X perf is -5.8% (master's Scanner.__call__ is not grown in that run), so the residual is inside the spread of which build each arm happens to end in. The guard exits stop before call 200 in both arms (tallies identical at 200, 800 and 3,000 calls); the branch keeps the speculation master gave up at _parse_array@155 and _parse_object@381 (a learned rebuild fixed the guard). Whole run at `-l 192`: branch -0.9%.
- deltablue: rebuilds go on past call 1,000 in both arms (master 62 kills after call 300, branch 56), largely of the 4,000-IR loop units projection_test@58 and chain_test@34 (about 115M each) for learned-since-build at shared kernel places (get_iter@84, truth@7, type.__call__@230) and late marks. Over calls 128-2048 the arms are equal (+0.1%); whole run at `-l 1024` +0.7%.

2-3. What the windows hold (pyperf, exits differenced between `-l 64` and `-l 128`; the cost of an exit, about 17k instructions, is the window's remainder divided by the excess exits, not measured on its own):
- deltablue calls 128-256, +2.23M a call (+6.3%): builds 428M against 372M (+0.44M a call, 1.2 points; projection_test@58 built twice against once); guard exits 15,988 against 2,688 (+13,300, about 5.1 points):
  - (a) Old units that keep exiting at a site already marked: Constraint.satisfy@102 is marked at call 129, but projection_test@58 (built at 125) exits there 3,166 times until its old-unit-nospec kill at call 160 (`paid 64.0M cost 1.1M`); StayConstraint.__init__ and incremental_add add 1,118. About 4,300 exits, 1.6 points. Cause: a guard's exit in a unit whose stale ledger is non-zero is diverted to it and returns before the old-unit check, so a unit that keeps learning (a large root almost always has) is never killed for a mark already bought. This is a defect of the stale rule, not a cost estimate.
  - (e) Marks paid twice: incremental_add@34 exits at Constraint.satisfy@134 and @130 (5,161), ScaleConstraint.__init__ at BinaryConstraint.__init__@28 (2,099). The diverted exits buy a learned rebuild (incremental_add@34 at 165, learned-since-build satisfy@134; ScaleConstraint.__init__ at 67, 72, 101) that does not fix the guard, and the next unit's exits pay the mark from zero: satisfy@130 marked at 190, BinaryConstraint.__init__@28 at 148 (master: 97 and 18). About 7,260 exits, 2.7 points. The fifth pass's add_propagate is the same rule: marks at calls 21, 52, 106 against master's 2, 5, 7.
  - Other sites about 0.8 point; nothing of (b), (c) or (d) found.
- json_loads calls 24-48, +1.80M a call (+6.8%): builds equal (66M each arm), exits fewer on the branch (374 against 776). The branch's _parse_array@1 runs stale from call 14 to 39 (what its stale ledger paid by the learned rebuild at 39, passes and diverted exits: 14.1M, 0.56M a call, 2.1 points) where master marked _parse_array@155 at 22 and rebuilt at 26; the remaining 4.7 points are not attributed (code of the intermediate units in each arm; per-unit profiles under -X perf change which builds are made, so they do not resolve it).

4. Rule changes, in order:
- (A) An exit at a site already marked, from a unit built before the mark, kills the unit at once whatever its stale ledger: the stale diversion is taken only when the site is not marked. It cannot bring churn back: the mark is already bought, each unit dies at most once per mark, and the unit built next knows the mark (master's old-unit rule unchanged). A few lines in lu_jit_note_exit (the site is the blame site or the innermost frame, as below the diversion); not built here: it needs its own failing test and gate.
- (B) A diverted exit pays its site's mark ledger as well as the stale ledger, and the mark is still decided only by exits of a unit that is not stale (built with what its places had learned). A guard that keeps failing after the learned rebuild is then marked on its first exit there, with what it already lost, instead of after paying the mark again from zero. Bound unchanged: a mark is bought once per site, learned rebuilds at most ways plus one per place; the change moves a purchase earlier, it adds none. This gives the invariant the ledger lacks: a unit paying exits at a steady rate at a site is rebuilt with what was learned at most ways plus one times, then marked, and after that pays nothing there (with A, its old units too).
- Generic-pass losses buying the unit's own rebuild: not the cause in either program (no permanent generic site found; json's exits end by call 200 and the final builds converge). The stale ledger already charges passes of places that learned since the build; a place that has not learned cannot be changed by a rebuild, so charging it is the churn the earlier builder saw. Keep it excluded.
- Growth: the ledger buys json's growth 15 to 60 times later than master's fixed trigger (ski rental, by design); it costs early windows, not the final build. No change proposed.
- Both arms: the large loop units of deltablue are rebuilt every 30-300 calls to the end for places in inlined kernel frames that learn late; a separate follow-up.

**2026-11-24 rules A and B (built, gated; not landable yet: windows)**

- A (86707fc3): an exit at a site marked since its unit's build kills the unit at once, whatever its stale ledger holds; the stale diversion applies only to unmarked sites. An old unit's death buys nothing (the mark was bought), so its kill carries no ledger figures.
- B (73fa7e63): a diverted exit pays its site's mark ledger as well as the unit's stale ledger; the mark is still decided only by an exit of a unit that is not stale.
- The invariant: a unit that keeps losing at a site gets at most ways plus one learned rebuilds, then the mark, then pays nothing there; with A its old units stop too. Bound: one mark per site, at most LU_OPCLASS_WAYS + 1 learned rebuilds per place; B moves a purchase earlier and adds none.
- Test: not built. A lane of the shape (a loop whose places learn a class every 2,500 iterations, inlining a method load whose receiver alternates after the build, alone and beside a second root that marks the site first) did not reach the diverted path: its marks and kills were the same with and without A and B (guard exits 2,974 before, 2,949 with A, 2,778 with A and B, master 2,365, the difference outside the lane's units), so it was dropped rather than kept as a test that passes either way. The evidence is deltablue's (diagnosis note).
- Gate (73fa7e63): focused kernel programs 64/64 in jit-off, plain, jit, jit-deopt; threads 7/7 in handoff; tools/jitcheck 0 failing (classtests 203,280 helpers flat, before 377,636, master 116,504; its ledger 61,899 first, 9,624 rebuilt, 61.9M paid: at most 80,948; churn 2,808 first, 9,554 rebuilt; intrange 256,193/436,193); tools/jitcost PASS. With A alone the classtests ledger failed on an old-unit kill printed as a purchase (paid 2.4M against 15.9M); fixed in A by printing none.
- Windows (master 12e0fa43 against the branch with A and B and the branch before them, both on 12e0fa43; instructions per call, pyperf worker, one run per arm on core leases; `-l N` makes 2N calls):

| program | calls | master | A+B | before A+B |
|---|---|---|---|---|
| json_loads | 24-48 | 29.19M | 29.73M (+1.9%) | 28.81M (-1.3%) |
| json_loads | 48-96 | 26.77M | 28.25M (+5.5%) | 27.44M (+2.5%) |
| json_loads | 96-192 | 26.62M | 24.36M (-8.5%) | 24.76M (-7.0%) |
| json_loads | 192-384 | 21.88M | 22.52M (+2.9%) | 22.57M (+3.1%) |
| json_loads whole, -l 12 | | 4.647G | 4.538G (-2.3%) | 4.573G (-1.6%) |
| deltablue | 128-256 | 35.42M | 35.44M (+0.0%) | 35.89M (+1.3%) |
| deltablue | 256-512 | 34.20M | 34.34M (+0.4%) | 33.38M (-2.4%) |
| deltablue | 512-1024 | 34.48M | 33.12M (-3.9%) | 33.31M (-3.4%) |
| deltablue | 1024-2048 | 33.23M | 33.55M (+1.0%) | 33.22M (-0.0%) |
| deltablue whole, -l 64 | | 9.289G | 9.503G (+2.3%) | 9.827G (+5.8%) |
| richards | 24-48 | 122.89M | 119.71M (-2.6%) | 121.98M (-0.7%) |
| richards | 48-96 | 122.21M | 120.33M (-1.5%) | 122.01M (-0.2%) |
| richards whole, -l 12 | | 6.764G | 6.900G (+2.0%) | 6.885G (+1.8%) |
| regex_v8 | 16-32 | 1104.12M | 996.16M (-9.8%) | 983.61M (-10.9%) |
| regex_v8 | 32-64 | 883.36M | 939.09M (+6.3%) | 920.88M (+4.2%) |
| regex_v8 whole, -l 8 | | 30.285G | 24.749G (-18.3%) | 24.707G (-18.4%) |
| classtests total (lucifer_jit=always) | 20,000 it. | 1.217G | 1.217G (-0.1%) | 1.285G (+5.5%) |
| classtests total | 40,000 it. | 1.228G | 1.224G (-0.4%) | 1.296G (+5.5%) |

- Reading: A and B remove the classtests loss (level with master) and halve deltablue's whole-run loss (+5.8% to +2.3%), and deltablue's table window (calls 128-256) is level. Not met: json_loads calls 192-384 is +2.9% (+3.1% before A and B, so not theirs) and its early windows are worse than before (+1.9%, +5.5% against -1.3%, +2.5%), deltablue 256-512 +0.4% against -2.4%. The whole-run table (tab9) was not rerun.
- The "steady" column of the final note is not steady state: its windows (json_loads calls 24-48, deltablue 128-256, richards 24-48, regex_v8 16-32, deepcopy 32-64, AWFY 20-60 iterations) are windows in which both arms still rebuild. Long-run figures (diagnosis note): json_loads +1.2% at calls 768-1536, deltablue +0.1% over 128-2048.

**2026-11-24 final gate and whole-run table (base dbb9b594)**

- Rebased onto origin/master dbb9b594 as luc-1kz0-land; one conflict, mechanical: master dropped the call_special helper, the branch adds call_any beside it (kept call_any, dropped call_special).
- Review fixes (jit: ledger review fixes): out_calls and the per-entry loss clamped (a torn read of a callee's counters under another root's lock could cache a huge per_entry for good), the per-entry loss bounded by the unit's recorded losses; inline growth capped at master's MAX_GROWN (budget at most 16,000), the ledger still buys each doubling; check_ledger counts each purchase once per root and event; the classtests cap is 2 x 164,529 (the count this tree measures at 20,000 iterations; the earlier 203,280 was before the rebase); jit_grow_direct's comment corrected (under the ledger the callee's first build takes big in line, so it no longer exercises growth).
- Gate on the fixed tree: focused kernel programs 64/64 in jit-off, plain, jit, jit-deopt; threads 7/7 in handoff; tools/jitcheck 0 failing (classtests 164,529 at 20,000 and 40,000; its ledger 56,958 first, 9,290 rebuilt, at most 76,070; churn 2,941 first, 9,692 rebuilt, at most 31,127); tools/jitcost PASS.
- Table: master dbb9b594 against the branch on it; instructions:u, one measurement a core lease, two runs per arm alternated, min of the two. Whole run at the small size (pyperf richards -l 12, deltablue -l 64, json_loads -l 12, regex_v8 -l 8, deepcopy -l 16; AWFY 20 iterations); long window from the two larger sizes (pyperf -l 48/96, 512/1024, 384/768, 32/64, 64/128, as calls, a worker making 2N; AWFY 20/60 iterations). AWFY inner size 1, except CD 10 and Mandelbrot 500 (their inner count is the problem size; CD at 1 fails verification, as it did in the earlier tables). Compile ms, IR and code from one -X lucifer_jit_stats run per arm at the whole-run size.

| program | whole run m/b (G) | whole | long window | per call/it. m/b | long | compile ms m/b | IR m/b | code KB m/b |
|---|---|---|---|---|---|---|---|---|
| pyflakes | 11.204/10.307 | -8.0% |  |  |  | 701/550 | 302k/235k | 40220/30887 |
| black | 11.859/12.097 | +2.0% |  |  |  | 815/904 | 292k/315k | 32883/35282 |
| pygments | 14.741/13.669 | -7.3% |  |  |  | 1016/593 | 194k/184k | 21880/21125 |
| imports | 1.434/1.470 | +2.6% |  |  |  | 59/72 | 29k/33k | 3804/4178 |
| richards | 6.844/6.959 | +1.7% | calls 96-192 | 122.57M/122.31M | -0.2% | 235/274 | 113k/120k | 13141/14199 |
| deltablue | 9.328/9.403 | +0.8% | calls 1024-2048 | 32.43M/32.22M | -0.7% | 487/484 | 226k/209k | 21142/19937 |
| json_loads | 4.750/4.607 | -3.0% | calls 768-1536 | 21.91M/22.31M | +1.8% | 292/392 | 110k/103k | 12885/12024 |
| regex_v8 | 28.935/24.205 | -16.3% | calls 64-128 | 794.91M/867.50M | +9.1% | 1896/1030 | 312k/263k | 35870/30895 |
| deepcopy | 4.214/4.130 | -2.0% | calls 128-256 | 5.87M/5.97M | +1.7% | 256/260 | 107k/106k | 12639/12457 |
| AWFY Richards | 2.673/2.695 | +0.8% | it. 20-60 | 119.98M/120.17M | +0.2% | 22/15 | 8k/8k | 910/904 |
| AWFY DeltaBlue | 0.409/0.409 | +0.0% | it. 20-60 | 5.77M/5.85M | +1.5% | 27/18 | 10k/9k | 1046/970 |
| AWFY Json | 2.239/1.637 | -26.9% | it. 20-60 | 65.42M/44.26M | -32.3% | 210/112 | 94k/57k | 7862/4855 |
| AWFY Havlak | 335.950/334.737 | -0.4% | it. 20-60 | 17299.12M/17231.32M | -0.4% | 652/445 | 192k/147k | 17923/13746 |
| AWFY Bounce | 0.237/0.238 | +0.7% | it. 20-60 | 1.33M/1.33M | -0.0% | 4/4 | 2k/2k | 352/336 |
| AWFY Sieve | 0.185/0.184 | -0.6% | it. 20-60 | 1.15M/1.15M | -0.0% | 2/2 | 1k/1k | 177/161 |
| AWFY List | 0.250/0.251 | +0.2% | it. 20-60 | 3.86M/3.89M | +0.6% | 3/3 | 2k/2k | 312/296 |
| AWFY Permute | 0.393/0.396 | +0.6% | it. 20-60 | 9.33M/9.33M | +0.0% | 8/8 | 3k/3k | 424/408 |
| AWFY Queens | 0.295/0.289 | -2.0% | it. 20-60 | 3.99M/4.00M | +0.3% | 9/10 | 4k/3k | 481/423 |
| AWFY Storage | 0.425/0.422 | -0.8% | it. 20-60 | 10.28M/10.28M | +0.0% | 6/8 | 3k/2k | 358/329 |
| AWFY Towers | 0.319/0.318 | -0.3% | it. 20-60 | 7.07M/7.07M | +0.0% | 4/4 | 1k/1k | 259/242 |
| AWFY NBody | 0.229/0.229 | -0.0% | it. 20-60 | 2.54M/2.55M | +0.4% | 4/3 | 2k/1k | 240/228 |
| AWFY CD | 6.562/6.900 | +5.2% | it. 20-60 | 263.87M/233.64M | -11.5% | 147/140 | 71k/60k | 8502/7360 |
| AWFY Mandelbrot | 12.586/12.589 | +0.0% | it. 20-60 | 610.30M/610.30M | -0.0% | 9/8 | 4k/4k | 542/529 |

- Reading: whole runs better on pyflakes -8.0%, pygments -7.3%, regex_v8 -16.3%, AWFY Json -26.9%; worse on CD +5.2% (its long window -11.5%), imports +2.6%, black +2.0%, richards +1.7%. Long windows within 2% except regex_v8 +9.1% (calls 64-128) and the CD and Json gains.

Follow-ups from the adversarial review (low):
- The per-exit "bytecodes still to run" value (d->rest) counts bytecodes in build order past the loop and is not validated by tools/jitcost.
- Places' rest/pass prices are shared across all units of a root and clamped to u->nins: a place priced by a larger sibling predicts a relearn cost of 0; such kills print no paid/cost.
- Rule A's "old" test uses the code-wide mark counter (marked_since), so a later mark of another site kills units built after the first mark.
- relearn() resets an escalated wait.
- The out-call counter `left` is re-armed without atomics and can leave a site that never exits again.
- The "same" rebuild diagnostic is dead (the unit's address is hashed as a constant).
- h_pic_same passes a NULL place on allocation failure.
- The value-class table is scanned linearly (eval_emit.c).

**2026-11-24 diagnosis: regex_v8's long windows and the cold programs' extra compiles (base dbb9b594, branch 44b4b232)**

Method: the binaries of the final table. Event logs (LUCIFER_JIT_DEBUG=units) of both arms: regex_v8 under its pyperf worker at `-l 256`, with a marker after each pass of the benchmark loop (block11 wrapped; calls 1-512), and imports, black, pyflakes, pygments, AWFY Json 60. Builds per window, counted in IR and in each build's own ms. Instructions:u on core leases, one measurement per lease, regex_v8 at `-l 32/64/128/256` under perf record (fp chains) to split compile from run. compile's children share undercounts once inlining makes chains deeper than the kernel's 127-frame limit (master `-l 64` reads less than `-l 32`), so compile is counted as the self samples of the build pipeline's functions (eval_*, opt, ssa, regalloc, emit, facts, codemem) times 1.13. 1.13 is children/self at `-l 32`, where the chains are complete (1.115 master, 1.148 branch). Logs reproduce the table's IR exactly (imports 29,183/32,559, black 292,126/315,157).

A. regex_v8. Neither arm stops rebuilding: about 100 builds per 64 calls in each to the end, new code still gets its first builds late (block3 and block4 at calls 460 and 504), and master's largest builds come last (block0@12 39.7k IR at call 381, block1@12 47.8k IR at 474).

| calls | master per call | branch per call | compile m/b | run m/b |
|---|---|---|---|---|
| 1-64 (whole -l 32) | 1116.1M | 1056.4M (-5.3%) | 212M/147M | 904M/909M (+0.6%) |
| 64-128 | 795.1M | 867.8M (+9.1%) | 15.2M/90.7M | 779.9M/777.1M (-0.4%) |
| 128-256 | 781.5M | 836.7M (+7.1%) | 6.9M/64.1M | 774.6M/772.6M (-0.3%) |
| 256-512 | 785.9M | 790.4M (+0.6%) | 17.0M/27.3M | 768.9M/763.1M (-0.8%) |
| whole -l 256 | 423.54G | 432.60G (+2.1%) | 19.8G/30.4G | |

- The +72.7M a call at calls 64-128 is all compile: +75.5M (+66.8M in self samples alone) against run -2.8M. The branch's units run 0.3-0.8% better than master's from call 64 on. The branch converges to that: about 0.5% better run code, compile still paid to the end. Whole run: -16.3% at `-l 8`, +2.1% at `-l 256`.
- Builds by cause, calls 64-128 (each build's own ms; branch 1,447 ms against master 293; about 4.2M instructions/ms from the counts above):
  - growth: 4 builds, 713 ms, about 47M a call. block0@12 to grown 4 at call 100, Pattern.sub and sub to 3, search to 1.
  - new loop entries of block0 at grown budgets: @104 grown 3 (9.8k IR) and @210 grown 4 (16.6k IR). 473 ms, about 31M a call.
  - a mark at grown 1: 71 ms, about 5M a call.
  - the rest -6M a call, against master's hot-site and learned rebuilds. Sum +76M.
  - These ms are wall times of an unpinned run: the split is good to about 10%, the total is the perf figure.
- Over calls 65-512 the branch's builds take 5,377 ms against master's 2,120. Branch: growth 15 builds 2,644 ms; new entries at grown budgets 1,007 ms; other builds at a grown budget 949 ms. Master: one growth build in the window, 9 ms (its fixed trigger grew Pattern.sub to 4 before call 3, then stopped).
- What buys it: hot_exit's growth rule. Every doubling is a ledger purchase (`paid` at or above `cost`), and each code climbs to the ceiling:
  - block0@12 at calls 9, 24, 48, 100, then a 33k-IR hot-site build at 278;
  - sub at 2, 37, 79, 178;
  - Pattern.sub at 9, 35, 85, 222;
  - Pattern.search at 11, 62, 129, 374;
  - block2 at 210, 322, 467;
  - Pattern.split at 167, 381; search at 83, 438.
  It is a repeated purchase for one reason, bounded only by MAX_GROWN. The purchases are valued by out_cost: CALL_COST plus the callee unit's losses per entry. Inlining does not remove most of the callee's losses (its exits and generic passes stay in the inlined copy), so the saving is not realized. block0's growth at call 100 was paid 2.21G over 58 entries, about 38M a call claimed. The run per call after it is within 0.5% of master's, which never grew block0. unit_losses also freezes a callee's value once it passes ENTRIES_SEEN (unverified whether that matters here).
- New entries: jc->grown is per code, so block0's loop entries (@58, @104, @164, @210, @297, @343) are built at the budget block0@12's ledger bought: 5.7-16.9k IR for units entered 2-18 times, 88k IR and about 1.3 s in all. They exist because the interpreter's loop-header waits in block0 run out (5,800 waits; master reaches 5,341 at @58 by call 512 and builds none). Master would build them at grown 0.

B. Cold programs. First builds are not the cause: imports 95/19,376 against 95/19,553 IR, black 434/120,398 against 426/119,523. The extra IR is learned-since-build rebuilds:
- imports: 46/6.2k IR on master, 54/10.6k on the branch (+4.4k against +3.4k net). Marks -0.4k.
- black: 242/61.7k on master, 297/100.9k on the branch (+39.1k), offset by mark-nospec -12.5k and retired -7.5k. old-unit-nospec 13/7.9k against 16/14.3k: +6.4k, rule A's old units rebuilt. Net +23k.
- The pattern is not hundreds of small units: a few large units, each rebuilt once and bought far below cost. The learned rebuild is predicted at build_cost(nins - rest), where rest is the IR after a place that had met nothing in an inlined frame (lu_ev_price_rest, lu_jit_prof_rest, lu_jit_stale_pass). When that frame is most of the unit, the prediction falls to a few percent, but the rebuild is as large or larger:

| unit (imports) | IR before -> after | predicted | build_cost(after) |
|---|---|---|---|
| hasattr | 1,137 -> 1,085 | 0.43M | 12.9M |
| _wraps | 1,087 -> 1,057 | 0.44M | 12.5M |
| getattr | 1,028 -> 1,017 | 0.49M | 11.8M |
| lookup_attr | 551 -> 1,027 | | |
| len | 272 -> 424 | | |

  In black: dict.__init__ predicted 0.17M against 12.2M, update_common 0.39M against 16.6M, _match_entry 1.11M against 34.4M.
- Learned rebuilds predicted below a third of build_cost(the unit's own size):

| program | rebuilds | IR | predicted | build_cost |
|---|---|---|---|---|
| imports | 12 | 5.4k | 5M | 57M |
| black | 81 | 38.6k | 72M | 462M |
| regex_v8 | 75 | 63k | 108M | 2.18G |

  In imports and black that IR matches the excess learned IR. The quiet rule and value sharing make no build larger that I could find (first builds equal in size).

C. Rule changes, in order:
1. Predict a learned rebuild at the unit's own build: relearn = enough = build_cost(nins). Delete the rest discount (lu_ev_price_rest, lu_jit_prof_rest, prof->rest, the minimum in lu_jit_stale_pass).
   - Argument: the ski-rental bound holds only if the predicted cost is at least the build bought. Measured rebuilds are 0.95-1.9 times the unit they replace, never the 3-5% predicted. Learning one class folds that place's generic expansion, not the rest of its frame.
   - Removes imports' +4.4k and black's +39k learned IR, and regex's learned rebuilds at grown sizes.
   - Risk to the gains: the same purchases occur in pyflakes (50, 41k IR), pygments (45, 29k) and Json (12, 7.5k), and would be delayed to about master's timing. Their gains come mostly from growth and marks the branch no longer makes: pyflakes grown 63.3k -> 10.5k IR and learned-type-fails 23.5k -> 9.7k; Json grown 73.3k -> 24.3k and learned 75.8k -> 45.4k. Measure them.
   - A deletion, but a change of the model, not a plain defect: not built.
2. Growth belongs to the entry that paid it: a unit's budget is INLINE_BUDGET shifted by its own entry's doublings, not the code's jc->grown.
   - Argument: the ledger that bought a doubling was one unit's calls. Inheriting it multiplies one purchase by every later entry, which never lost anything for want of it.
   - Removes regex's grown loop entries (about 31M a call at calls 64-128, 1.0-1.3 s over the run).
   - Risk low: it touches only codes with several entries after a growth. A few lines, but a mechanism change: not built.
3. Value a doubling by what inlining removes, and buy the next one only after the last one delivered.
   - Argument: out_cost adds the callee's exits and generic passes, which stay in the inlined copy unless the caller's facts reach the callee's places. The megaiter case, where the caller knows the iterable, is the exception that motivated it.
   - Rule: after a growth, the unit's losses per entry (own, plus out-of-line calls valued as now) must have fallen by at least what the doubling was bought for, else the code grows no further. Ski rental again: a purchase that removes nothing is not repeated.
   - Removes regex's growth chain (about 47M a call at calls 64-128, 2.6 s over the run).
   - Risk: Json and megaiter rely on growth. It needs its own design and lane: not built.
- Order: 1, then 2, then 3. 1 is a deletion that addresses both the cold programs and part of regex. 2 is small and local. 3 is a design.
- Not examined here: (3) json_loads, deepcopy and DeltaBlue's long windows.

**2026-11-24 change 1 built (4b477b47); stopped: the typed lane needs a decision**

- Built: a learned rebuild is predicted at the unit's own build (enough = build_cost(nins)); the rest discount is deleted (lu_ev_price_rest, lu_jit_prof_rest, lu_prof rest, lu_unit relearn and the minimum in lu_jit_stale_pass). Lane learnprice (tests/kernel/jit_learned_price.py, a ledger lane): every learned rebuild must be predicted at least at the build of the unit it replaces. The ledger lanes now check that too. Before the change, learnprice, churn and classtests failed on it (setitem@1: 85 IR predicted 0.32M; main@78: 1,184 IR predicted 13.5M). After it, they pass: churn 2,988 first, 14,431 rebuilt; classtests 56,842 first, 8,371 rebuilt; learnprice 56,906 first, 3,247 rebuilt.
- typed fails: 511,732 helpers at 20,000 iterations, 594,094 at 40,000 (with the discount: 368,853 flat). fill@33 (1,212 IR) is rebuilt for array.__setitem__@37 after paying 15.2M, which is build_cost(1,212), between 20,000 and 40,000 iterations. The rebuild comes out at 1,214 IR, so no sound price lies below the full one. Rules A and B do not carry this lane: its demand (flat after 20,000) is stricter than break-even for this unit. Its stale pass loses about 840 instructions, so a 1,212-IR rebuild pays back after about 18,000 passes. Decision needed: keep the lane's demand (which no sound price meets) or bound typed by the ledger as churn and classtests are.
- Other lanes run: dispatch passes (674,882 -> 1,374,882, as before). classtests growth passes at 246,789 flat (before 164,529; AT_MOST 329,058).
- AWFY DeltaBlue, instructions per iteration (sizes 20 and 60, one run a lease): master 5.747M; change 1 5.328M (-7.3%); branch before it 5.857M (+1.9%). Whole run at 20: 409.5M, 407.6M, 408.7M. Guard exits at 60: 4,390.
- Not done: changes 2 (growth per entry) and 3 (growth must deliver), the full gate and the table.

**2026-11-24 typed reshaped, change 2 built (a624c785, 1e23eebd)**

- typed (coordinator's ruling): "flat after 20,000 iterations" encoded master's fixed stale wait, not a property.
  - Under break-even, fill@33's learned rebuild (1,212 IR, 15.2M) is bought after about 18,000 stale passes of about 840 instructions each, and the same again for its second learned rebuild (1,214 IR, a new inline-context key), which lands between 40,000 and 80,000 iterations.
  - The lane now compares 4N and 8N (80,000 and 160,000; START in tools/jitcheck): 636,330 helpers flat. It also joins the ledger lanes at 2N.
  - The ledger part fails: fill@33 rebuilt for learned-type-fails fill@58 is predicted at 1.35M, below its own build of 226 IR (1.73M). learned-type/kind-fails prices its purchase at build_cost(jc->built_ins), the root's last build of any entry, while the purchase kills every unit of the root. Not fixed: the sound price is the builds it kills (the sum of their `enough`, or at least each one's own), a pricing change for the coordinator.
- Change 2 (1e23eebd): the doublings of the inlining budget are kept per entry (jc->grown per code unit, indexed by the unit's entry pc). An entry's doublings are bought by its own unit's calls out of line; a new loop entry of a grown code starts at the base budget.
  - The growth cost is still build_cost(2 x jc->built_ins), the root's last build, unchanged.
  - Lane entrygrowth (tests/kernel/jit_entry_growth.py): entry's start grows on its calls out of line, and the loops of an arm run only in the second half are entered from the interpreter after that.
  - Before the change it fails (entry@255 and entry@307 built at grown 1). After it, it passes.
  - The lane's checker code went into a624c785 with the typed reshape (one file); the program and the runtime change are in 1e23eebd.
- Lanes on 1e23eebd: PASS entrygrowth, learnprice, churn, classtests (ledger and helpers, 246,789 flat), dispatch, grown (2,013 flat), megaiter (504,236/525,239), large, recursive, turns, typed helpers (636,330 flat). FAIL: the typed ledger (above). The rest of the jitcheck lanes, the kernel gate and jitcost were not run.

**2026-11-24 pricing by kills, change 3 (growth must deliver), gate and table (dd6d2f69, fcf5f177)**

- Pricing (coordinator's rule): a purchase is priced at the cost of everything it kills. learned-type-fails, learned-kind-fails and every mark kill all live units of the root: they are now priced at the sum of those units' own builds (`enough`), never below any one of them; with none live, at the root's last build. A hot site and a doubling kill one unit: priced at that unit's build (twice it for a doubling), not at whatever entry of the root built last. Rule A's old-unit kills still buy nothing.
- The typed ledger failure was not this price: fill@33's kill was of an older code object's unit (182 IR, priced exactly at its own build) while the checker looked up "its own build" by name and found a newer code's 226-IR unit (typed execs its functions anew per type code). The kill event now prints the killed unit's IR (`ins N`), and every ledger lane checks every purchase, not only learned ones, against it. typed passes (73,583 first, 17,633 rebuilt).
- Change 3: when a doubling is bought for an entry, the entry's bar is the unit's losses per entry then (own exits and generic paths, plus calls out of line valued with their callees' losses) less half of the out-of-line part (the reason). The next doubling is bought only if the grown unit's losses per entry are at or below the bar; else the entry stops growing. Argument: a doubling costs twice the last; one that removes less than half of what it was bought to remove starts a chain to MAX_GROWN that costs more than it can return. Inlining a callee whose losses are its own exits and generic paths moves them into the caller's copy instead of removing them (regex_v8's block0 doubled four times on about 38M a call of claimed losses; its run afterwards stayed within 0.5% of master's).
- Lane delivers (tests/kernel/jit_grown_generic.py): a start calling a large callee at 24 sites whose losses are its own megamorphic attribute reads. Before: grown twice (1,241 -> 2,263 -> 3,868 IR). After: once. grown, megaiter, entrygrowth pass.
- Gate on fcf5f177: focused kernel programs 69/69 in jit-off, plain, jit, jit-deopt (the earlier set plus the ledger and growth lanes' programs); threads 7/7 in handoff; full tools/jitcheck 0 failing; tools/jitcost PASS.
- Table: master dbb9b594 against fcf5f177, instructions:u, one core lease a measurement, two runs per arm alternated, min. Sizes as in the earlier table; compile ms, IR and code from one stats run per arm.

| program | whole m/b (G) | whole | long window | per call/it. m/b | long | compile ms m/b | IR m/b |
|---|---|---|---|---|---|---|---|
| regex_v8 (-l 8) | 28.936/23.936 | -17.3% | calls 64-128 | 794.9M/812.6M | +2.2% | | |
| regex_v8 | | | calls 128-256 | 781.5M/792.4M | +1.4% | | |
| pyflakes | 11.214/10.069 | -10.2% | | | | 712/411 | 302k/197k |
| pygments | 14.741/13.304 | -9.7% | | | | 839/444 | 194k/160k |
| imports | 1.434/1.431 | -0.2% | | | | 55/58 | 29k/28k |
| black | 11.860/11.248 | -5.2% | | | | 697/619 | 292k/261k |
| AWFY Json | 2.243/1.603 | -28.6% | it. 20-60 | 65.29M/39.38M | -39.7% | | |
| AWFY CD (10) | 6.562/7.077 | +7.8% | it. 20-60 | 263.87M/279.05M | +5.8% | | |
| AWFY DeltaBlue | 0.409/0.408 | -0.2% | it. 20-60 | 5.76M/5.36M | -7.1% | | |
| AWFY Richards | 2.673/2.696 | +0.9% | it. 20-60 | 119.97M/120.17M | +0.2% | | |
| richards (-l 12) | 6.845/7.019 | +2.5% | calls 96-192 | 122.56M/122.21M | -0.3% | | |
| deltablue (-l 64) | 9.330/9.448 | +1.3% | calls 1024-2048 | 32.42M/32.08M | -1.1% | | |
| json_loads (-l 12) | 4.744/4.556 | -4.0% | calls 768-1536 | 21.91M/23.84M | +8.8% | | |
| deepcopy (-l 16) | 4.215/4.134 | -1.9% | calls 128-256 | 5.88M/6.10M | +3.8% | | |

- regex_v8 split (perf record, pipeline self samples x 1.13): calls 64-128 compile 14.1M/25.7M a call, run 780.8M/786.9M (+0.8%); calls 128-256 compile 7.2M/18.5M, run 774.4M/773.9M (-0.1%). Was +9.1% and +7.1%, nearly all compile; about 11M a call of extra compile remains.
- Not accepted: CD whole and window, richards and deltablue whole, json_loads and deepcopy windows, regex windows; imports compile ms 58 against 55 on one wall-time run (IR lower). Causes, from event logs of master, the earlier landing binary (44b4b232), the stage copies after change 1 and change 2, and fcf5f177, at each window's two sizes:
  - CD: change 3. _CollisionDetector.handle_new_frame@6, _reduce_collision_set@3 and _recurse@1 each grow once and are refused their second doubling, which change 2's binary bought (handle_new_frame: 180k a call out of line before the first, 141k before the second, a fall of 39k against the 90k the rule asks) and which delivers there (CD -11.5% a iteration in the earlier table). The window's builds fall (17.2k IR master, 10.5k change 2, 4.5k now): the loss is run code. The rule's losses per entry miss what these doublings save.
  - json_loads window: run code; builds in the window are equal across the branch arms (one growth of bench_json_loads, 2k IR). The hot parser units buy fewer marks: master marks _parse_object@103, _parse_array@1/@70, _scan_once@44, _new_match@169/@202; the earlier landing binary _parse_object@103 and _new_match@169/@202; change 2 only _new_match; now none: the _new_match marks (paid 1.32M against the root's last build, 1.05M) are now priced at the sum of _new_match's two live units and not paid in 1,536 calls.
  - deepcopy window: no builds in the window in any arm; run code. The pricing doubles _deepcopy_list's learned-type-fails (deepcopy@7: 24.6M for its two units, bought after 1,585 entries; was 12.4M after 787), and mark-catch _deepcopy_tuple@35 (paid 1.58M against 0.75M after change 1) is not bought: _deepcopy_tuple@7 keeps leaving on every entry. The earlier landing binary also bought learned-type-fails deepcopy@309 on _deepcopy_dict, which change 1 does not.
  - richards and deltablue whole: the branch builds less IR than master (richards 44.5k first, 21.5k learned against master's 25.2k; marks 64 against 79); changes 2 to 3 do not change richards' builds (identical logs). The extra is fewer and later learned rebuilds and marks under the ledger, which change 1 priced up (whole +1.7% and +0.8% in the earlier table).
  - regex windows: the remaining compile is new code's first builds and learned rebuilds; growth chains are gone.

## State

Branch luc-1kz0-land, on origin/master dbb9b594, head after the note commit. Measured binary fcf5f177.

Built:
1. (4b477b47) a learned rebuild is predicted at the unit's own build.
2. (1e23eebd) growth is per entry.
3. (dd6d2f69) a purchase is priced at the builds it kills (sum of the killed units' own builds; one unit's for hot site and growth); the kill event prints the unit's IR; the ledger lanes check every purchase against it.
4. (fcf5f177) a doubling must deliver (per-entry bar); lane delivers.

Gate on fcf5f177: kernel 69/69 in four lanes, handoff 7/7, jitcheck 0 failing, jitcost PASS. Table in the last note.

Not landable on the acceptance: CD (+7.8% whole, +5.8% a iteration: change 3 refuses second doublings that pay), json_loads window +8.8% and deepcopy window +3.8% (marks and learned-type-fails priced at the sum of several units are bought later or not at all), richards +2.5% and deltablue +1.3% whole, regex windows +2.2%/+1.4% (compile), imports compile ms 58/55 (one run). Gains kept: pyflakes -10.2%, pygments -9.7%, regex_v8 whole -17.3%, Json -28.6% whole and -39.7% a iteration, black -5.2%.

Open for the coordinator: (a) the delivery measure: CD's second doublings deliver in run code while counted losses per entry fall by less than half their reason; (b) the sum price for marks: _new_match's and _deepcopy_tuple's marks kill several small units whose sum delays the mark past the window, where one unit's own build bought it before.

Lanes and sizes (tools/jitcheck):
- Helpers and count lanes at N and 2N (N = 20,000), except typed at 4N and 8N (START).
- Ledger lanes (churn, classtests, learnprice, typed) at 2N: every purchase paid at least its predicted cost and predicted at least at the killed unit's own build; total IR within first builds plus LEDGER_SLACK x paid / BUILD_INS.
- entrygrowth at 2N: the start grew; loop entries built after that are at grown 0.
- delivers at 2N: jit_grown_generic's start grows exactly once.

Compile/run split for regex_v8:
- Record the pyperf worker at -l 32, 64, 128, one run per core lease, pinned: `perf record -e instructions:u -c 1000003 --call-graph fp`.
- Count compile as the self samples (`perf report --no-children --sort sym`) of the build pipeline's functions: every function defined in eval_*.c, opt.c, regalloc.c, ssa.c, x86_64/emit.c, facts.c and codemem.c. Multiply by 1.13 (children/self of compile() where chains are complete).
- A window is the difference of two run lengths; `-l N` makes 2N passes. A pyperf worker refuses an existing -o file: remove it before each run.

Debug aids: LUCIFER_JIT_DEBUG=units logs builds (with the entry's grown level), kills (`ins N` of the killed unit, `paid P cost C` for ledger kills) and learns; -X lucifer_jit_stats prints generic passes; LUCIFER_JIT_DUMP=1 prints each unit's IR.
