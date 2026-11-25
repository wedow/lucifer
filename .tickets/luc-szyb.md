---
id: luc-szyb
status: open
deps: []
links: []
created: 2026-11-25T18:00:05Z
type: feature
priority: 1
assignee: Greg Wedow
parent: luc-orn7
---
# one site ledger: what a unit takes in line, how deep, and what happens when it does not fit

One mechanism for what a unit takes in line, how deep, and what happens when it does not fit, replacing the fixed inline budget spent in bytecode order (luc-iama: the cliff) and the growth-level doubling policy (luc-1kz0 Findings: three formulas for valuing growth were wrong). A call site is in line when its value (calls at the site times the saving per call: call protocol plus a fold measured from IR at build time) covers its price (the IR it adds times the marginal build cost at the unit's size). No budget, no levels, no final build. Design below, by reading only: nothing was built or run. Stage 0 is measurements only and can falsify each later stage; every later stage is tabled alone on the ledger table before the next is stacked on it. Open rulings for the owner are in section 9 (2: the acceptance metric if the pre-ledger lead on django_template, genshi and richards is growth that does not pay back inside the measured process; 6: unit size no longer bounded by a ceiling; 8: evidence at a first build under lucifer_jit=always).


## Design (2026-11-25)

Read from origin/master at 38732cd3 with `git show` and `git grep`. I did not run `git fetch` (this session is read-only); `git ls-remote origin master` returned the same commit, so the local ref is current. No worktree was made, and nothing was built or run. Every statement about today's behaviour cites that tree. Every prediction is derived from the tickets' logs and tables and is marked where a number is missing.

## 0. Summary

- **One rule.** A call site is in line when its value covers its price.
  - Value is calls at the site times the saving per call.
  - Price is the IR the callee adds there times the marginal build cost at the unit's size.
- **Calls.** Counted by the unit where the site is out of line (`COUNT_OUT`, exists). For a site no unit has counted yet, the calls are the entries its frame has shown times a block weight from the interpreter's branch words.
- **Saving.** The call protocol (jitcost's `call` row) plus the fold: the expected instructions the callee's own unit runs per call, less what its in-line copy runs. The fold is measured from IR at build time, per callee and argument signature, and kept.
- **No budget.** Build cost is convex (`n(6500 + 5n)`, `runtime/jit/jit.c:224-239`), so the marginal price rises with the unit. Sites are taken in order of value per IR until the next one's density is below the marginal price. That is the greedy knapsack with a capacity that comes out of the cost model.
- **No levels, no finality.** A site left out keeps its counter armed for its break-even. A rebuild is bought when the admitted sites together have paid the whole new build.
- **Replaced:** `INLINE_BUDGET` and bytecode order, growth levels and `MAX_GROWN`, hot sites with `LOOP_CALLS` and the quarter rule, `lu_ev_room_begin/_end`, the callee-loss term of `out_cost` (`unit_losses`, `per_entry`, `lost_entries`), the recursive-copy test, and the peel-room test.
- **Kept:** the first-compile threshold, marks, learned rebuilds, the quiet rule.
- **Finding to rule on before building (section 7).** On django_template, genshi and richards, the pre-ledger arm's lead on the best of three values looks like growth that does not pay back inside the measured process. If stage 0 confirms that, no payback rule recovers those rows on that metric, whatever it values.

## 1. What the tier knows about a call site today

| Fact | Build time | Run time | Evidence |
|---|---|---|---|
| Calls at a site, interpreted | One bit: the site ran | Written once | `runtime/jit/jit.h:164-172`, `runtime/interp/ceval.c:1809,1821` |
| Calls at a site, compiled, out of line for budget | Not read by the build | `lu_out.left/armed/before`, a plain `inc qword` per call | `runtime/jit/target.h:205-211`, `runtime/jit/x86_64/emit.c:5190-5200`, `runtime/jit/eval_call.c:207-220` |
| Calls at a site, compiled, in line | None | None | No counter is emitted |
| Calls at a site refused for another reason (recursive, depth 24, noinline, unsupported, recursive copy) | None | None | `eval_call.c:251-272` return before `lu_ev_room`; 284-289 after it returned 1 |
| Unit entries | - | Counted to 65,536, then frozen | `emit.c:5522-5529`, `jit.c:314-321`, `target.h:318` |
| Callee identity | Constant callable, virtual bound method, function the unit made, or the site's table when it met exactly one | Table of up to 4, then mega; no counts per callee | `eval_call.c:1080-1100`, `eval_call.c:113-116`, `jit.h:316-342` |
| Polymorphic site (2 to 4 callees) | Never in line: `H_call_any` | A generic pass, `ANYCALL_INS` | `eval_call.c:114`, `eval_call.c:1236`, `jit.c:2211-2217` |
| Callee size, bytecode | `Py_SIZE(co)`; used only for the threshold and waits | - | `jit.h:46-55`, `jit.c:1481` |
| Callee size, IR | `built_ins` (root's last build), `u->nins`; the inliner reads neither | - | `jit.c:122,1356-1358`; `lu_ev_room` takes `callee` only to record it, `eval_call.c:188,214-216` |
| Callee size in this context | Not recorded, though it is at hand: IR indices are contiguous per inlined frame | - | `eval_call.c:574,597`; `runtime/jit/eval_emit.c:128-131` scans the same way |
| Arguments constant, typed, virtual | Known per value, not recorded per site | - | `lu_ev_const`, `lu_ev_type`, `LU_IS_VIRT`: `runtime/jit/eval.h:332-333`, `runtime/jit/ssa.h:397`, `eval_call.c:110` |
| Loop depth | None; a block is a loop header or not | Inferred: 64 calls per entry, or half the entries of a generator's resume unit | `eval.h:91-95`, `jit.c:95-96,2153-2159,2176` |
| Reached in the profile | Root frame's sites only, for the put-off; ignored at the call | - | `runtime/jit/eval_arith.c:7-18`, `eval_call.c:1360,1378`, `jit.c:1335` |
| Branch proportions | Two 8-bit counts per conditional jump; a direction under 1 in 64 is not built | Interpreter only | `jit.h:191-218`, `runtime/jit/eval_ops.c:267-276` |
| Callee's losses | - | Exits and generic passes of its unit, per entry, frozen at 65,536 entries | `jit.c:248-267,2094-2131` |

How the budget is spent today:
- The budget is a threshold test at the call, not a reservation: `spent = nins + 2 * ninlined <= budget` (`eval_call.c:181-193`). A callee admitted just under it adds whatever it adds.
- Blocks are built lowest pc first (`runtime/jit/eval_flow.c:2099-2105`) and a callee is built whole at its call (`eval_call.c:597`). So the cut falls in bytecode order.
- A hot site is let past the budget once per build and its callee gets a fresh base budget (`eval_call.c:197-206,224-243,594-598`).
- The budget is `INLINE_BUDGET << grown[entry]`, at most four doublings (`jit.c:93-94,1316-1317`).
- A doubling is bought when all sites together cover `build_cost(2 * nins)`. A hot site is bought when one site covers the unit's build, costs a quarter of it, and makes 64 calls per entry. Otherwise each site is re-armed for as many calls again as it has made (`jit.c:2146-2209`).
- After the fourth doubling only the hot-site clause can fire, so a site called a few times per entry is never revisited. That is luc-iama's second mechanism.

### One more count, and what it costs

| Count | Cost | Use |
|---|---|---|
| Block weights at build time | No run-time cost; a float per IR block, set at forks from the branch word (`eval_flow.c:1941-1948`) and summed at merges (`eval_flow.c:1147`) | **Proposed.** |
| Loop trips in FOR_ITER's cache word | About 4 instructions per interpreted step, none compiled. The word holds four kind bits (`jit.h:174-189`); 12 are free for two 6-bit counts kept as the branch words are | **Proposed**, the only new run-time count. |
| Back-edge counter over the code's calls | Free (`jit.h:86-120`) | Not used: both stop when the code is warm and never count under `lucifer_jit=always`. |
| Per-site counter in the interpreter (CALL's first cache word as a count) | One store per interpreted CALL | Not needed once weights exist. |
| Per-site counter at in-line sites | Two or three instructions and a store to a shared line per call in hot loops | Not proposed: the ledger refused a loop-pass counter for this (`.tickets/luc-1kz0.md:46`). |

### Can the evaluator measure the specialization it would get?

Yes, but not by trying and undoing.
- The evaluator has no undo; only the whole build restarts (`eval_flow.c:21-30,2245-2274`, `eval_call.c:5-7`).
- **Every in-line build is a free measurement.** The frame's IR is the index range made around `lu_ev_build(k, 0)`, with nested frames as sub-ranges.
- **A trial is a scratch root build of the callee.** No calls in line (`f.budget = -1` already does this for the put-off, `jit.c:1328-1336`), parameters seeded with the site's knowledge (`lu_ev_def_type`, constants), then freed.
  - It costs one evaluator pass over the callee's own frame.
  - The evaluator is 26% of compile on pyflakes (`.tickets/luc-1kz0.md:35`). At 8.0k to 16k instructions per IR instruction for units under 2,000 IR, that is about 2k to 4k per IR instruction. Stage 0 measures it as `TRIAL_INS`.
- **Results are kept** per callee code and argument signature. The signature is, per parameter: unknown, a type, a constant of a type, or a virtual object of a type. Every later in-line build of that pair refreshes the record.
- **What is compared.** IR counts do not work: `getattr`'s unit is 1,087 IR with a least way of 64 (`.tickets/luc-1kz0.md:678`), so a raw count overstates by the 9 to 30 times in the Findings. I compare expected priced IR: each block's weight times the measured price of its instructions, own frame only.
- **Why this may hold where the work estimates did not.** It is used only as a difference between two builds of the same bytecode under the same branch words. Weight errors cancel to first order, and what remains is exactly the arms and helpers that fold.
- **What it misses.** Folds in the caller from what the callee returns, guards hoisted once in line, and anything behind a nested site (valued at that site). It is a lower bound on specialization.

## 2. Selection: the rule, and whether a budget is needed

Definitions:
- Build cost `B(n) = n(B1 + B2 n)`, with `B1 = 6500` and `B2 = 5`. Marginal price `λ(n) = B1 + 2 B2 n`.
- For site `i`: calls `N_i`, saving `s_i = P + F_i`, IR added `m_i`, density `d_i = N_i s_i / m_i`.
- `P` is the call's protocol cost. `F_i = max(0, E_own(callee) - e(callee, signature))` from the kept records, and 0 while there is none.
- `m_i` comes from the record. Without one it is the callee's own-unit size, and without that its bytecode size times a measured IR-per-code-unit ratio.

Rule at a build:
1. The root-only pass meets every root-level site. It exists today for an entry's first compile (`jit.c:1328-1350`), and at `budget = -1` `lu_ev_room` already lists each site and callee (`eval_call.c:207-217`).
2. Sort the sites by density and admit while `d_i >= B1 + B2(2n + m_i)`, adding `m_i` to `n`. The price the last one faced is the unit's `λ*`.
3. The real build takes a site in line when it is bought (below) or its density is at least `λ*`. This applies at every depth.
4. A nested site's calls are its frame's calls times its weight relative to the frame's entry.
5. For a rebuild, `λ*` is the marginal price at the killed unit's size.

What "does not fit" means: a site whose density is below the marginal price. Nothing is cut. A unit a few percent larger faces a price a few percent higher, and loses the sites of lowest density first, wherever they are in the code.

A budget is not needed. Because `B` is convex, charging every admitted site the marginal price at the size reached collects at least the true increment: the sum of `m_i λ*` is at least `B(n_final) - B(n_0)`. Greedy order makes this independent of bytecode order.

Bounds:
- **Cold program, first build.** IR taken in line costs at most the sum of `N_i s_i`, where `N_i` is the threshold the interpreter served (1,000 plus 4 per code unit past 256, `jit.h:42-55`) times the weight.
  - At 1,000 entries, one call per entry, `s = 420` and a 300-IR root: `m(9500 + 5m) <= 420k`, so `m <= 43` IR.
  - At 100 passes per entry: `m <= 2,100` IR.
  - Today the same site gets up to 1,000 IR on no evidence.
- **Cold program, later.** A site reaches the unit only when counted calls have paid: a 300-IR callee in a 1,000-IR unit costs `300 * 18,000 = 5.4M`, which is 12.9k calls at 420. The rebuild that takes it needs all admitted sites to cover `B(1,300) = 16.9M`. Rebuilds for sites therefore cost at most the losses counted, the ledger's bound.
- **Trials.** Run only when `N_i P` covers `TRIAL_INS * M`, once per callee and signature, so their total is under the protocol losses shown.
- **Hot program.** A unit stops growing where the best remaining density meets `λ(n)`, that is at `n* = (d - B1) / (2 B2)`. For very hot sites that is far above any size built today.
  - This is by design: each purchase is paid.
  - It is also a risk: build latency and memory are not in the ledger (section 9, item 6).
- **Steady state.** Compiling is 2.6 to 4.4% of steady cycles today (`.tickets/luc-orn7.md`, note of 08:06). Call protocol in compiled code is 8.6% and calls through C 8.0% (`.tickets/luc-ne54.md`, step 2). So the protocol half of the value is bounded near a tenth of steady cycles, and the rest has to be the fold.

## 3. Growth without levels

A site left out of line is armed for the calls that complete its own price (`ceil((m λ* - N s) / s)`), and for at least as many calls as one look costs (`exit_cost / s`). Today it is armed for the unit's IR count and then doubled (`jit.c:1284-1298,2185-2192`), which lands purchases 1.22 to 1.55 times past break-even (`.tickets/luc-1kz0.md:557`).

At a look (`hot_exit`):
1. Value each out site from its counter. Run the greedy admission against the unit's current size.
2. If the admitted set covers `B(n + sum of m)`, the whole new build, record the set as bought for this entry. Kill the unit and build at once.
3. Otherwise re-arm the looking site for its share of the shortfall at the proportions seen.

How many rebuilds:
- At most one per site in theory, each paid in full.
- In practice one or two per hot unit, because of three batching effects:
  - **Whole-build price.** Small sites wait for company, since the admitted set must pay the whole rebuild.
  - **Evidence passes down.** A bought frame's nested sites get the parent's counted calls times their weight, so a chain is taken in one rebuild when each link pays.
  - **One rebuild ledger per unit (stage 4).** Stale passes and admitted sites' calls pay the same rebuild, since one build removes both. The quiet rule (`jit.c:594-624`) holds it while places still learn.
- The bought set is per entry, as growth is since 1e23eebd. A loop entry built later must not inherit it: that was regex_v8's 88k IR of grown loop entries (`.tickets/luc-1kz0.md:342`). Today's `jc->hot` is per root (`jit.c:127,1269-1282`).

## 4. Finality and churn

- Every site out for price has a counter, so the event that revisits it is its own break-even. There is no last build.
- Sites out for structure (recursion past one copy, depth 24, an unsupported instruction, a noinline mark) have no counter and stay out, as today.
- It cannot churn:
  - The bought set only grows, and a site is bought once per entry.
  - Each purchase is paid at the whole build, so IR built is at most first builds plus paid over `BUILD_INS` times the lanes' slack (`tools/jitcheck:586-633`).
  - Sites admitted by estimate never buy a rebuild. They are re-decided only when a rebuild is bought for another reason.
  - A site that flips out at such a rebuild is counted from then on and stays once bought.

## 5. Interactions

- **Marks.** Unchanged. A noinline mark (`jit.c:2359-2363`) overrides a bought site, once. A nospec'd call site has no known callee (`eval_call.c:110`) and is not a candidate. Django's `render_annotated@3` stays generic until rule change 3 of luc-1kz0 is settled separately.
- **Polymorphic sites.** Still out of line. Where the receiver's learned types make arms (`eval_call.c:1277-1346`), each arm calls a different function at one `(code, pc)`, and today's key merges them under the first callee (`jit.c:1261-1267`, `eval_call.c:209-216`). The key gains the callee's code.
- **Generators.** `no_room` (`runtime/jit/eval_gen.c:223-243`) takes the same test. A generator made and stepped out of line costs 3.5 times a plain call (`.tickets/luc-1kz0.md:567`), outside jitcost's factor of two (`tools/jitcost:162-164`), so it gets its own constant. Each resume entry buys for itself.
- **Recursion.** The one-extra-copy limit stays (`eval_call.c:264-272`). The recursive-copy budget test (`eval_call.c:276-286`) becomes the ordinary price test. The fold is own-frame only, so value cannot compound down a descent, which is what ran AWFY Json to 30k IR (`.tickets/luc-1kz0.md:101`).
- **Kernel functions.** A kernel call out of line measured 210 (`.tickets/luc-1kz0.md:567`), so it gets its own constant by the same factor-of-two argument.
  - Operators already go in line only when operand types are known (`eval_call.c:1262-1275`). The record then shows a small `m` and they pass at first build.
  - A cold fallback such as `_instancecheck` to `lookup_attr` for an unlearned type (`.tickets/luc-iama.md`) shows 437 IR and little fold, and stays out.
- **Free threading.** No new shared write on a hot path.
  - `COUNT_OUT` stays a plain increment; a lost update delays a look.
  - The bought set is written under `prof_lock`, as `hot` is.
  - Records are published whole. A missing or stale record only changes a prediction.
  - Two threads may trial one pair; the later store wins.

## 6. Cost model

Existing constants, unchanged: `BUILD_INS`, `BUILD_INS2`, `CALL_COST`, `ITER_INS`, `ATTR_INS`, `OP_INS`, `ANYCALL_INS` (`jit.c:224-232`). Removed: `INLINE_BUDGET`, `MAX_GROWN`, `LOOP_CALLS`.

New jitcost rows, each failing outside 0.5 to 2 like the rest:

| Row | Measures | Constant |
|---|---|---|
| `call kernel`, `call gen` | The stage B shapes that differ from plain by 2 times or more (210, 1,480) | `KERNEL_CALL`, `GEN_CALL` |
| `pass` | Instructions per expected-path IR instruction of the measured function's own unit: 5.75 straight line, 8.40 two arms (`.tickets/luc-1kz0.md:676`) | `PLAIN_INS` |
| `helper` | One published non-generic helper call, its in-line sequence and a small primitive | `HELPER_INS` |
| `trial` | An evaluator-only pass per IR instruction, at three sizes | `TRIAL_INS` |
| `build` at 8k, 16k, 32k IR | The n² term where units will now be priced; today's rows stop at 256 statements (`tools/jitcost:122`) | Checks `BUILD_INS2` |
| `fold` | End to end: a callee that dispatches on an argument, called with a constant and with a variable. Predicted `F` against the measured difference per call | Checks the estimate |
| `weight` | Predicted calls per entry at sites behind branches and loops of known frequency, against `call_refused` counts | Checks weights |

## 7. Predicted behaviour (derived, not run)

**tomli_loads, `key_value_rule@3`.**
- There is no grown-4 build of 15,821 IR. The cold kernel attribute path (about 1,300 IR) is not admitted.
- The eleven sites lose 2.39G a value (24,934M against 22,542M). At least 0.7G of that is call machinery (`.tickets/luc-1kz0.md:551`).
- A rebuild of a unit near 11.5k IR prices at `B = 736M`, so the sites are bought within about one value on protocol alone. Values 2 and 3 then run the repaired unit.
- Predicted row: about 22.5G, with about 0.7 to 1.4G of compile in values 0 to 1 (master compiles 1.21G in value 1).
- Missing: calls per site per value.

**django_template.**
- Master bought `VariableNode.render`'s growth at value 12, paid 362M against 267M. Exact arming alone moves that to about value 9.
- Pre's shape prices at about 1.26G by the model (`render_annotated` to 8,792 IR is 444M; `render_value_in_context` 96M and 112M; `getattr` 37M and 65M; intermediate sizes are my assumption). Its saving is 87M a value.
- So pre's growth pays back after 7 to 14 values, and the benchmark runs 4. Pre wins the best value and probably loses the whole process (unverified).
- To buy inside value 3 on true savings, the bought IR must price under about 260M across the three units. That holds only if value-selected copies are far smaller than pre's blind doublings.
- Missing: in-context IR and calls per site in pre's final units.

**richards, `HandlerTask.fn`.**
- Master paid 59.4M at 65,536 entries, 906 an entry, against a price of 38.8M. The measured gain is 6.6M a value, about 857 an entry, so counted value is already about right.
- Exact arming buys at about 43k entries, value 5 to 6. Earlier needs fewer IR: inside value 3 needs the added IR under about 335.
- Missing: IR of the sites pre's grown units took in.

**genshi.**
- Master never buys pre's four hot sites or two growths in steady state: the hot-site clauses block them, and generator calls are valued 3.5 times low.
- The design buys them when paid. For `XMLSerializer.__call__@97` with a 1,500-IR generator added, `B(6,555) = 257M` against about 215M a value for the whole xml gap: values 3 to 5.
- Long windows recover; best of three recovers in part.

**regex_v8.**
- The growth chain was bought on callee losses that stay in the in-line copy (2.21G claimed over 58 entries, run within 0.5%).
- Stage 2 credits only protocol plus measured fold. Predicted compile in calls 64-128 and 128-256 falls from 4.44G and 10.2G toward pre's 1.78G and 3.0G, with run within 1%.

**pyflakes, black, pygments.**
- Stage 1: first builds unchanged and growth is already about 5% of IR, so compile ms stays within about 2% of 1,313.
- Stage 3: first builds take only what the threshold has shown. The 120 to 165 copies of the generic attribute path (`.tickets/luc-1kz0.md:37`, an older tree) mostly stay out, and compile ms falls. I cannot size it without the replay.

## 8. Stages

Each stage runs `tools/floor quick`, full `tools/jitcheck` and `tools/jitcost`. Its lanes are written and shown failing first. Then it is tabled alone against master and against the stage before it. The table uses the luc-1kz0 method of 2026-11-25: instructions on core leases, two runs per arm, best of three values, each row split into compile and run so that a purchase crossing a value boundary is not read as a worse unit.

Rows that must not regress by more than 1%: django_template, genshi, richards, tomli_loads, deepcopy, pyflakes, black, pygments, regex_v8 calls 64-128 and 128-256, AWFY Richards, DeltaBlue and CD. Cli compile ms must not rise above master's.

| Stage | Change | Files | Lines (+/−) | Lanes, failing first | Expected to move |
|---|---|---|---|---|---|
| 0 | Measurements only, on a branch that does not land | wip counters in `jit.c`, `eval_call.c`; scripts | about +250 wip | The checks below | Nothing |
| 1 | Sites, not levels. `hot_exit` buys the greedy set against the whole build. Bought set per entry. Key includes the callee. Arming for the shortfall. Delete `grown`, `MAX_GROWN`, `LOOP_CALLS`, the quarter and turns clauses. First builds unchanged | `runtime/jit/jit.c`, `eval_call.c`, `target.h`, `tools/jitcheck`, two kernel programs, the doc's budget paragraph | runtime +80/−80; tools +60/−45; tests +140 | `sitefinal`: a hot call once per entry behind more cold calls than four doublings hold; on master it stays out for good. `sitechain` and `siteentry` replace `growchain` and `entrygrowth`. The ledger lanes read `site` kills | tomli_loads to about 22.5G; AWFY CD whole; purchases 1.2 to 1.55 times earlier |
| 2 | The fold. Block weights, FOR_ITER trips, the expected-priced-IR pass, records per callee and signature, the trial, `s = P + F` and `m` from records at looks. Delete `unit_losses`, `per_entry`, `lost_entries` and `out_cost`'s callee term | `eval_flow.c`, `eval_ops.c`, `eval_iter.c`, `eval_call.c`, `eval_emit.c`, `jit.c`, `jit.h`, `ceval.c` (FOR_ITER word), `tools/jitcost`, `tools/jitcheck`, three kernel programs | runtime +330/−70; tools +170; tests +200 | `foldbuy`: a callee dispatching on a constant argument is bought within 1.25 times `price/(P+F)`. `nofold`: `jit_grown_generic.py`'s megamorphic callee is not bought before `price/P`. `megaiter` must still pass | regex_v8 windows' compile; django, genshi, richards timing; megaiter |
| 3 | Price at build time. Evidence from shown entries times weight, greedy `λ*` from the root-only pass, the same test for generators, recursion and peeling. Delete `INLINE_BUDGET`, `lu_ev_room_begin/_end`, `hots`, `peel_room`'s budget | `eval_call.c`, `eval_flow.c`, `eval_gen.c`, `eval.h`, `ssa.h`, `jit.c`, `tools/jitcheck`, three kernel programs | runtime +150/−130; tools +70; tests +200 | `cliff` (luc-iama's criterion): K callees of falling call frequency; adding one callee moves out-of-line calls per iteration by at most one site's share. `coldcopies`: cold kernel fallbacks are not built. Lanes whose sizes encoded first-build inlining move `START`, with the reason, never the property | cli compile ms and IR down; code MB; tomli's unit size |
| 4 | One rebuild ledger per unit: admitted sites' calls and stale passes pay the same build, under the quiet rule | `jit.c`, `tools/jitcheck`, one kernel program | +30/−15; tests +80 | `sharedrebuild`: a unit with a learning place and a hot out site is rebuilt once | deltablue and json_loads windows |

Stage 3 comes after stage 2 on purpose. Without the fold, a first build priced on protocol alone would leave out mid-size callees whose value is specialization, and the stage could not pass its table alone.

Stage 0 checks, each able to falsify a stage:
1. **Site table.** Per site, in and out of line: callee, signature, in-context IR, the callee's own-unit IR, calls per value. For `key_value_rule@3`; `VariableNode.render`, `render_value_in_context`, `Node.render_annotated`, `getattr`; the four richards task functions; `XMLSerializer.__call__@97/@116`; `block0@12`, `Pattern.sub`; and the 20 units with most compile in pyflakes, black, pygments.
2. **Fold against the machine.** For each hot site, the unit built with the site forced in and forced out (a wip list): instructions per call saved, against `P + F`.
3. **Greedy with the budget unchanged.** The same IR budget spent in density order using check 1's counts: the share of tomli's, django's, genshi's and richards' gaps that selection alone recovers.
4. **Replay.** The rule simulated on check 1's logs with `P` alone and with `P + F`: purchases by value index and predicted compile, for every table row.
5. **Whole-process instructions.** Pre against master for django_template, genshi and richards at `-w 1 -n 3` and at `-n 20`.
6. **Weights.** Predicted calls per entry against counted, per site.
7. **Evaluator share** of compile by unit size, to 16k IR.
8. **Build model** at 8k to 32k IR.
9. **Loops peeled and refused** per program, and how many began past 1,000 IR. If any table program has such a loop, stage 1 must carry the peel test early.

## 9. Risks and open questions

1. **Does the fold predict the saving?** Check 2. This is a formula, and three formulas were wrong before.
2. **Are pre's three rows paid inside the benchmark?** Checks 5 and 4. If not, the acceptance metric needs a ruling, not a valuation.
3. **Are weights from branch words and FOR_ITER trips within 2 times of counted calls?** Check 6. Kernel branch words are shared by every caller. Loops without a FOR_ITER take their trips from the exiting branch's word (not designed in detail).
4. **Does a seeded root build predict the in-line copy?** Compare each record's trial values with the first real in-line build's, from the event log.
5. **Can `build_root` run without a live frame?** It reads code, function, globals, builtins, entry, stack depth and locals (`eval_flow.c:2150-2243`). Are there other reads of `g->live` and `f->unit`, and what does a scratch pass leave behind (profile places, `lu_jit_prof_built`, a cold callee's state via `state_for`)? Answerable from code.
6. **Unbounded unit size.** Build pauses are synchronous (`jit.c:5-6`) and memory is 110 to 140 bytes per IR instruction. `jit.c:1316` says the ceiling waits for the owner's decision. This design asks for it.
7. **Is `n(6500 + 5n)` right above 8k IR?** Check 8. It read 0.57 to 0.63 at the 2^11 bucket (`.tickets/luc-1kz0.md:48,55`), and generator units 50 to 110k per instruction.
8. **`-X lucifer_jit=always`.** The threshold is 1 (`jit.h:49-51`), so a first build has shown nothing and would take nothing in line. The gate's first-call lanes would then stop exercising inlined frames. Proposed: evidence at a first build is the `on` threshold in both modes. Needs a ruling.
9. **Process start.** Each kernel function and signature pays one trial after its evidence gate. Read from the replay on `-c pass` and the imports row.
10. **Peeling.** Today it is tied to the budget (`eval_flow.c:1776-1779,1886-1887,1901`). Proposed: the copy's IR at `λ*` against the loop's shown passes times `ITER_INS`. That saving is not measured for a number that changes kind. Check 9 says whether it matters.
11. **More sites count.** Every price-refused site increments a shared word. Does a unit run by many threads lose to that line? One measurement: the thread kernel programs in handoff, and the "call over 3 receiver classes" idiom.
12. **Per-entry bought sets** make a generator's two entries each pay for the same call. This is the observation at `.tickets/luc-1kz0.md:589`, unchanged.
13. **Site keys are hashes** (`jit.c:1261-1267`). A collision merges two decisions, as today.
14. **`CALL_COST` will fall** as luc-ne54 lands (a direct call is about 280 instructions today). The value then shifts to the fold, and jitcost must fail when it does.

What would make me abandon this:
- **Stage 2.** The fold's predicted saving is outside 0.5 to 2 of the measured one on more than a third of the hot sites, or trial records miss the real copies by that much. Then no deterministic count I can find values specialization, and (A) goes back to the owner as a choice between unpaid growth and accepted losses.
- **Stage 3.** Weights fail check 6, or the replay puts cli compile above master. Then stage 1 stays, and the cliff is treated by spending the fixed allowance in density order (check 3's arm), which keeps a constant.
- **The whole design.** Stage 1, tabled alone, loses more than 1% on a ledger row because a doubling took in something no site's count asked for and it paid, as CD showed for the delivery rule. That would mean the site is the wrong grain.
- **The premise.** Check 5 shows pre's lead is paid inside the process while the replay with true savings still buys later. Then break-even itself loses these rows, and no valuation fixes it.

### Files
runtime/jit/jit.c, runtime/jit/eval_call.c, runtime/jit/eval_flow.c, tools/jitcost, tools/jitcheck.

## Stage 0 results

Branch luc-site0 from origin/master 270f8f0b (not to land: wip counters and scripts). Arms: pre = f626ea38 (the ledger's parent), master = 270f8f0b. Method of luc-1kz0 (2026-11-25): one pyperf worker per core lease, pinned, `perf record -e instructions:u --call-graph fp -k CLOCK_MONOTONIC` with each value's window stamped, compile = samples with compile() in the chain; reference loops (django_template and richards -l 2, genshi -l 1); two runs per arm alternated, each figure the min of the two (spread between the runs 0.0-0.3%, genshi best value 1.6%).

### Check 5: whole-process instructions, pre against master (2026-11-25)

M instructions (compile in brackets). "All values" is the warmup and the timed values together: the benchmark's own work. "Whole process" adds start-up, imports and the harness.

| program | -n | figure | pre | master | master vs pre |
|---|---|---|---|---|---|
| django_template | 3 | best value | 855.8 (24.0) | 909.2 (13.0) | +6.2% |
| | 3 | mean value | 918.7 (83.1) | 952.9 (61.5) | +3.7% |
| | 3 | all values | 4,944.8 (1,316.0) | 4,651.0 (651.4) | -5.9% |
| | 3 | whole process | 10,357.2 (2,845.2) | 9,707.3 (1,941.0) | -6.3% |
| | 20 | best value | 850.4 (13.0) | 851.4 (0.0) | +0.1% |
| | 20 | mean value | 902.7 (27.1) | 886.7 (20.4) | -1.8% |
| | 20 | all values | 20,242.2 (1,609.4) | 19,526.6 (875.6) | -3.5% |
| | 20 | whole process | 25,709.0 (3,142.0) | 24,588.6 (2,166.0) | -4.4% |
| genshi (text + xml) | 3 | best value | 1,791.8 (120.6) | 1,918.4 (44.6) | +7.1% |
| | 3 | mean value | 2,099.4 (342.1) | 2,157.5 (263.9) | +2.8% |
| | 3 | all values | 9,042.4 (1,651.8) | 9,225.0 (1,301.2) | +2.0% |
| | 3 | whole process | 12,910.4 (2,774.0) | 12,851.8 (2,301.8) | -0.5% |
| | 20 | best value | 1,601.2 (0.0) | 1,815.4 (0.2) | +13.4% |
| | 20 | mean value | 1,708.3 (71.2) | 1,965.1 (102.9) | +15.0% |
| | 20 | all values | 36,915.6 (2,051.0) | 41,979.2 (2,522.4) | +13.7% |
| | 20 | whole process | 40,807.6 (3,187.0) | 45,656.9 (3,520.5) | +11.9% |
| richards | 3 | best value | 242.6 (0.0) | 246.6 (0.0) | +1.6% |
| | 3 | mean value | 244.3 (1.6) | 251.1 (3.3) | +2.8% |
| | 3 | all values | 1,285.0 (229.0) | 1,458.0 (304.6) | +13.5% |
| | 3 | whole process | 4,755.7 (1,232.8) | 4,698.5 (1,174.0) | -1.2% |
| | 20 | best value | 242.6 (0.0) | 240.4 (0.0) | -0.9% |
| | 20 | mean value | 246.1 (1.3) | 244.1 (1.0) | -0.8% |
| | 20 | all values | 5,474.2 (250.0) | 5,585.6 (314.2) | +2.0% |
| | 20 | whole process | 8,929.7 (1,258.2) | 8,840.9 (1,189.6) | -1.0% |

Outside the values (whole less all values, -n 3) master spends less in all three: django 356M (239M of it compile), genshi 241M (121M), richards 231M (135M). That is the ledger's start-up saving on import and harness code, not the benchmark's growth, so the whole-process column mixes the two; "all values" isolates the benchmark.

Master moved since luc-1kz0's table (django best value 949.4 -> 909.2, genshi 1,927.6 -> 1,918.4, richards 249.2 -> 246.6).

Reading. Pre wins the best of three on all three rows and loses the whole process on all three, but the cause differs, so the design's finding holds for one row of three:
- **django_template: confirmed.** Pre's growth does not pay inside the measured process. On the benchmark's own values pre spends 294M more. Its extra compile is 665M (1,316M against 651M) against a run lead of 56M a value (mean run 835.6M against 891.4M), so it needs about 12 values to pay back, and by then master has bought its own growth. At -n 20 the best values tie (850 against 851) and master leads every other figure. Pre's best-value lead is unpaid growth bought early.
- **richards: refuted.** Pre's lead is paid inside the benchmark. Its values cost 173M less at -n 3 (+13.5% for master: master compiles 305M against 229M and runs the slow units longer before its single purchase at 65,536 entries). Pre loses the whole process only through the 231M outside the values. By -n 20 master's best value is 0.9% better, so the steady units are equal or better on master; the gap is time to the best build.
- **genshi: refuted.** Pre's lead is not growth awaiting payback. It is a steady-state gap: +13.4% best value and +11.9% whole process at -n 20, and +2.0% over all values at -n 3. Master's -0.5% whole-process edge at -n 3 is again only the outside-the-values saving (241M against a 183M deficit in the values).

Abandon condition ("the premise": pre's lead is paid inside the process while the replay with true savings still buys later). For richards and genshi the first half is met: their leads are paid inside the benchmark's values. Whether the premise fails therefore rests on check 4 (the replay), which has not been run. For django the condition is not met. Ruling 2 (the acceptance metric) is needed for django_template alone. Best of three rewards growth that 20 values do not pay back, and no payback rule can recover that row on that metric.

### Check 3: greedy with the budget unchanged (2026-11-25)

What was built (wip commit on luc-site0, about 250 lines with check 1's log; not for landing):
- **Counts.** A count run per program on the wip binary with `LUCIFER_SITES=2`. Every site taken in line gets a counter that never leaves: an extra `lu_out` entry after the unit's real outs, incremented by the existing `COUNT_OUT` with no look. Sites out of line keep today's counters. Each unit's counts are written at its kill and at each value's end (a hook in the value marks), and at process exit for the cli programs.
- **Site log.** At every build the evaluator logs each site's decision with its stable name `code:line@pc<parent:line@next` (today's site key, with names in place of addresses), its callee, and the IR made while the callee's frame was built.
- **Plan (offline, from the count run).** Per root and budget level (1000 << grown), today's allowance is spent in order of calls per IR instead of bytecode order:
  - Allowance: the IR the count run's last build at that level actually took in line. Today's builds overshoot the nominal budget, because the test is at the call and an admitted callee adds whatever it adds.
  - IR per site: its own in-line IR in that build (its frame's IR less its in-line children's), else its callee's own unit.
  - A name counts as one decision, as the run-time key is. A name is admitted only once one of its parent names is. A parent ranks by the densest site below it.
  - Hot sites (let past the budget) are left to today's rule.
- **Measured run.** `LUCIFER_SITEPLAN` makes `lu_ev_room` admit a listed site whatever the budget, refuse a listed one (counted as today, so growth and the hot-site rule still see it), and apply today's test to anything unlisted. Lookups are memoized per site key, root and budget. Growth, hot sites, learned rebuilds and marks are unchanged.
- **Caveat: an oracle, not the in-process counters the design names.** The counts come from a separate run of the same workload, so this bounds what selection alone can give, in-sample. The out-of-line counters alone cannot rank in-line sites against each other, because in-line sites have no count today.

What the plans change, at each root's last count-run build (sites; calls in the count run):
- tomli: 281 sites go out (3.4M calls) and 62 come in (9.7M calls). The eleven root-level sites of luc-iama all come in: `key_value_rule@59, 63, 67, 68, 74, 224, 273` at 316k calls a value each, plus `@370 Flags.set`.
- django: 366 out (0 calls), 75 in (168k).
- genshi: 344 out (14k), 122 in (973k).
- richards: 129 out (20), 29 in (4.4k).
- cli: pyflakes 431 out (6k) and 113 in (167k); black 541 out (39k) and 115 in (75k); pygments 291 out (6k) and 90 in (899k).

Table. Arms are master, wip without a plan (the control: same binary as wip + plan) and wip + plan. Two runs per arm alternated, min of the two (spread 0.0-0.4%). Best value of -w 1 -n 3, M instructions (compile in brackets). Gap references: ad1eb461 measured here for tomli; pre from check 5 for the others.

| row | master | wip, no plan | wip + plan | plan vs no plan | reference | gap | share of gap recovered |
|---|---|---|---|---|---|---|---|
| tomli_loads best value | 24,935.1 (1.0) | 24,934.1 (0.0) | 24,217.1 (392.0) | -2.9% | ad1eb461 22,239.1 | 2,695.0 | 27% |
| tomli_loads whole process | 106,749 | 106,877 | 101,985 | -4.6% | | | |
| django_template best value | 908.8 (13.0) | 908.8 (13.2) | 898.6 (26.6) | -1.1% | pre 855.8 | 53.0 | 19% |
| django_template whole process | 9,708 | 9,799 | 9,732 | -0.7% | | | |
| genshi best value | 1,918.4 (45.2) | 1,918.4 (45.0) | 1,866.4 (51.4) | -2.7% | pre 1,791.8 | 126.6 | 41% |
| genshi whole process | 12,848 | 12,975 | 12,566 | -3.2% | | | |
| richards best value | 246.6 (0.0) | 246.8 (0.0) | 242.4 (0.0) | -1.8% | pre 242.6 | 4.2 | 105% |
| richards whole process | 4,690 | 4,817 | 4,800 | -0.4% | | | |
| pyflakes whole (G; compile ms; IR) | 8.097; 347; 176,361 | 8.094; 347; 176,362 | 8.101; 355; 179,249 | +0.1% | | | |
| black whole | 12.713; 641; 262,225 | 12.714; 658; 262,226 | 12.589; 599; 253,802 | -1.0% | | | |
| pygments whole | 13.581; 430; 157,503 | 13.582; 437; 157,503 | 13.188; 448; 159,864 | -2.9% | | | |

The wip binary without a plan matches master on every value. Its whole process is 0.1-2.7% higher: start-up compile is about 100M more, not investigated, and the same in both wip arms.

Reading:
- Selection alone, with the allowance unchanged and oracle counts, recovers about a quarter to two fifths of the tomli, django and genshi gaps, and all of richards' small one (HandlerTask's nested `Task.addPacket -> Packet.append_to` goes in line).
- It gains 0.4-4.6% on every whole-process row except pyflakes (+0.1%). Cli IR built moves -3.1% to +1.6%. Compile ms rose on two of the three cli rows (+8 ms pyflakes, +11 ms pygments; black -59 ms).
- **luc-iama's first mechanism is confirmed directly.** In key_value_rule's last build 9,500 of the 15,424 in-line IR sits at sites with no call in the last value (check 1). Spending the same allowance by density takes all eleven cut sites back in line. That recovers 27% of tomli's gap to ad1eb461, not all of it: the rest is per-pass cost of the in-line code itself (c684ad72's attribute path, luc-1kz0), which no selection removes.
- The remaining django gap (81%) is pre's growth, which this arm does not touch (check 5: unpaid by -n 3). Genshi's remaining 59% is its steady-state units.
- **Abandon conditions.** Stage 3's fallback ("spend the fixed allowance in density order") is shown to work as a mechanism: it gains on every row but pyflakes, with counts that the run-time key can only approximate. It is not yet a landable rule, because its counts here are an oracle and in-line sites have no counter today. Nothing here falsifies stage 1. On tomli, selection accounts for about a quarter of the cliff's cost. The rest is not the budget's size either: it is per-pass cost of the in-line code.
