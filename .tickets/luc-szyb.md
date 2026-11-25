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

### Check 1: site table (2026-11-25)

Count-mode runs of the wip binary (`LUCIFER_SITES=2`, check 3): -w 1 -n 3 at the reference loops, regex_v8 at -l 1. Calls per value are the last value's window, all units of the root, counters at in-line and out-of-line sites. The cli programs are one window, the whole run. Each table describes the root's last build in that run.

How to read the columns:
- IR in context: IR made while the callee's frame was built, nested frames included; the callee's own part in brackets.
- Callee own unit: the IR of the callee's own last build, - when it has none.
- Signature, per argument: c constant, t type known, v virtual, u unknown.
- Caveats on the counts. The in-line counters are guards with frame states, so they can shift a build slightly. A site's name is today's site key (code and pc, and the frame that called it), so copies of one call under one parent share a count.

Units:

| unit | builds | predicted compile M (n(6500+5n), all builds) | last IR | sites in / out for budget / out other | in-line IR at sites with no call in the window | out-of-line calls a value |
|---|---|---|---|---|---|---|
| tomli key_value_rule@3 | 8 | 1,919 | 15,733 (budget 16,000) | 667 / 18 / 1 | 9,500 of 15,424 (62%) | 2,358,914 |
| django VariableNode.render | 4 | 110 | 2,878 (2,000) | 94 / 4 / 0 | 2,363 of 2,854 (83%) | 61,434 |
| django render_value_in_context | 2 | 38 | 1,711 (1,000) | 51 / 2 / 0 | 1,480 of 1,663 (89%) | 0 |
| django Node.render_annotated | 6 | 72 | 57 | 0 / 0 / 0 (nospec at @3) | - | 0 |
| django getattr (kernel) | 3 | 34 | 880 | 23 / 0 / 0 | 713 of 829 (86%) | 0 |
| richards DeviceTask.fn | 6 | 30 | 527 | 6 / 0 / 0 | 30 of 451 | 0 |
| richards HandlerTask.fn | 2 | 27 | 1,124 | 13 / 1 / 0 | 0 of 968 | 16,974 |
| richards IdleTask.fn | 2 | 13 | 161 | 4 / 0 / 0 | 0 of 72 | 0 |
| richards WorkTask.fn (@1; @106) | 3; 3 | 25; 12 | 633; 516 | 8 / 0 / 0; 4 / 0 / 0 | 30 of 495; 427 of 427 | 0 |
| genshi XMLSerializer.__call__@97 | 1 | 24 | 1,621 | 46 / 16 / 0 | 263 of 636 | 28,002 |
| genshi XMLSerializer.__call__@116 | 4 | 442 | 6,848 | 124 / 79 / 0 | 2,527 of 4,183 (60%) | 4,384 |
| regex_v8 block0@12 | 2 | 98 | 2,997 | 33 / 85 / 0 | 1,491 of 1,959 (76%) | 14,155 |
| regex_v8 Pattern.sub | 3 | 120 | 3,547 (2,000) | 40 / 28 / 0 | 1,635 of 3,525 (46%) | 29,831 |

Sites with at least 1,000 calls a value at root level or out for budget, for the units that have any (richards: every root-level site of the four task functions is in line, at 4.6k-18.6k calls a value; the one site out is HandlerTask's nested `Task.addPacket@82 -> Packet.append_to`, 17.0k calls, own unit 39 IR):

| unit | site | decision | callee | sig | IR in context (own) | callee own unit | calls/value |
|---|---|---|---|---|---|---|---|
| key_value_rule@3 | @10 | in | parse_key_value_pair | ttu | 15,424 (46) | 950 | 316,419 |
| | @59 | out | len | u | - | 278 | 316,413 |
| | @63 | out | type.__call__ | ccu | - | 155 | 316,412 |
| | @67, @74 | out | get_iter | t | - | 129 | 316,411 |
| | @68 | out | key_value_rule.<locals>.<genexpr> | u | - | 54 | 316,410 |
| | @224 | out | Flags.is_ | utc | - | 146 | 316,408 |
| | @273 | out | NestedDict.get_or_create_nest | ut | - | 153 | 316,407 |
| | @370 | out | Flags.set | utc | - | 331 | 4,489 |
| | parse_value@319, @376 (depth 2) | out | Pattern.match | ctv | - | 1,723 | 27,912 |
| | parse_value@400, @410 (depth 2) | out | Match.end; match_to_number | t; tu | - | 64; 472 | 27,910 |
| VariableNode.render | @24 | in | FilterExpression.resolve | uu | 2,854 (177) | - | 15,363 |
| | @35 | out | render_value_in_context | uu | - | 1,711 | 15,354 |
| | FilterExpression.resolve@166, @206, @258 (depth 1) | out | getattr | ucc | - | 880 | 15,360 each |
| render_value_in_context | @18; @40 | in | template_localtime; localize | t | 18; 16 | 944; - | 20,000 |
| | @111 | in | conditional_escape | t | 1,629 (22) | 1,032 | 20,000 |
| getattr | @7; @39 | in | _attribute_name; lookup_attr | t; ut | 8; 141 (6) | 16; 482 | 40,000 |
| XMLSerializer.__call__@97 | @104 | out | _get | uv | - | 137 | 11,996 |
| | NamespaceFlattener.__call__@209 (depth 1) | out | _get | uv | - | 137 | 12,002 |
| block0@12 | @38 | in | Pattern.search | uc | 1,959 (32) | 2,008 | 6,510 |
| | Pattern.search@48 (depth 1) | out | _new_match | uvu | - | 271 | 6,510 |
| | @58 | out | next_item | t | - | 144 | 1,845 |
| Pattern.sub | @14; @19 | in | _ssize; _subx | t; utttc | 75 (26); 3,450 (263) | 119; 1,852 | 6,288 |
| | _subx@262..@489 (depth 1, 7 sites) | out | _getslice, _append, str.join | v.. | - | 18-31 | 2,101-6,285 |

The 20 units with most predicted compile in the cli programs (one window: the whole run):

| program | unit | predicted compile M | last IR | in / out budget / out other | in-line IR at sites never called | out-of-line calls |
|---|---|---|---|---|---|---|
| pyflakes | Checker.NAME:1855@1 | 295 | 2,254 | 56 / 18 / 2 | 809 of 2,144 | 2,538 |
| pyflakes | Checker.handleChildren:1212@1 | 149 | 3,973 | 131 / 6 / 7 | 2,337 of 3,908 | 212,101 |
| pyflakes | Checker.handleNodeLoad:1041@48 | 97 | 1,558 | 29 / 15 / 0 | 661 of 829 | 5,602 |
| pyflakes | Checker.addBinding:966@1 | 96 | 1,615 | 23 / 26 / 0 | 921 of 1,050 | 2,121 |
| pyflakes | Checker.addBinding:966@23 | 94 | 1,599 | 26 / 24 / 0 | 995 of 1,064 | 1,894 |
| pyflakes | Checker.handleNodeLoad:1041@1 | 81 | 1,503 | 41 / 16 / 0 | 523 of 781 | 6,919 |
| pyflakes | Checker.handleNode:1244@1 | 70 | 447 | 7 / 0 / 1 | 0 of 284 | 0 |
| pyflakes | Checker.getCommonAncestor:915@1 | 70 | 768 | 24 / 3 / 0 | 68 of 519 | 341 |
| pyflakes | Checker.__init__:742@192 | 68 | 1,070 | 37 / 7 / 0 | 569 of 962 | 4,484 |
| pyflakes | _is_typing_helper:620@3 | 47 | 1,199 | 33 / 6 / 0 | 768 of 1,020 | 27,339 |
| pyflakes | Checker.LAMBDA.<locals>.runFunction:1969@2 | 40 | 2,252 | 53 / 8 / 1 | 1,529 of 2,185 | 382 |
| pyflakes | Checker.ARGUMENTS:1978@1 | 40 | 2,239 | 55 / 7 / 0 | 1,564 of 2,221 | 9,452 |
| pyflakes | Checker._run_deferred:786@25 | 36 | 2,112 | 48 / 9 / 1 | 1,335 of 2,028 | 10,502 |
| pyflakes | Checker.IF:1805@1 | 35 | 2,091 | 50 / 9 / 0 | 1,536 of 2,050 | 15,001 |
| pyflakes | Checker.getParent:908@1 | 31 | 194 | 10 / 0 / 0 | 0 of 121 | 0 |
| pyflakes | Checker.differentForks:955@1 | 30 | 1,167 | 26 / 8 / 0 | 266 of 1,059 | 70 |
| pyflakes | Checker.futuresAllowed:800@1 | 29 | 1,829 | 42 / 0 / 0 | 1,539 of 1,751 | 0 |
| pyflakes | Checker.ARG:1981@1 | 28 | 1,244 | 37 / 11 / 0 | 861 of 1,217 | 0 |
| pyflakes | compile:3567@280 | 23 | 1,606 | 26 / 18 / 0 | 1,236 of 1,412 | 0 |
| pyflakes | Checker.CLASSDEF:1984@276 | 23 | 1,595 | 35 / 8 / 1 | 1,482 of 1,482 | 6 |
| black | Parser._addtoken:278@1 | 147 | 1,127 | 36 / 5 / 0 | 0 of 813 | 11,801 |
| black | _match:536@157 | 125 | 1,793 | 17 / 10 / 0 | 119 of 637 | 34,301 |
| black | _t223:2647@17 | 124 | 1,735 | 5 / 5 / 0 | 0 of 392 | 2,376 |
| black | Parser._addtoken:278@38 | 114 | 1,189 | 38 / 5 / 0 | 320 of 918 | 1,892 |
| black | Pattern.match:3077@1 | 84 | 3,119 | 56 / 17 / 0 | 357 of 3,055 | 6,652 |
| black | _match_entry:2785@1 | 68 | 2,115 | 15 / 17 / 0 | 361 of 2,057 | 15 |
| black | LineGenerator.visit_default:134@4 | 67 | 2,474 | 52 / 28 / 4 | 459 of 1,903 | 6,074 |
| black | Parser.addtoken:230@1 | 65 | 1,058 | 32 / 2 / 0 | 168 of 1,005 | 4,890 |
| black | BracketTracker.mark:70@1 | 63 | 1,074 | 20 / 3 / 0 | 95 of 757 | 1,032 |
| black | Driver.parse_tokens:114@81 | 61 | 1,166 | 47 / 8 / 1 | 0 of 727 | 8,142 |
| black | Line.__str__:479@92 | 56 | 635 | 41 / 0 / 0 | 17 of 546 | 0 |
| black | Line.append:52@1 | 55 | 1,088 | 38 / 8 / 0 | 417 of 855 | 11,220 |
| black | Parser.pop:386@1 | 47 | 633 | 30 / 0 / 0 | 75 of 547 | 0 |
| black | Visitor.visit:152@3 | 44 | 410 | 15 / 0 / 1 | 0 of 230 | 0 |
| black | _entry:2906@327 | 36 | 1,545 | 8 / 0 / 0 | 660 of 1,405 | 0 |
| black | _entry:2906@211 | 34 | 1,300 | 7 / 4 / 0 | 889 of 889 | 2,087 |
| black | _entry:2906@1 | 28 | 888 | 3 / 0 / 0 | 690 of 690 | 0 |
| black | normalize_trailing_prefix:127@1 | 28 | 538 | 20 / 0 / 1 | 27 of 495 | 0 |
| black | convert:475@1 | 26 | 695 | 22 / 0 / 0 | 181 of 593 | 0 |
| black | _entry:2906@290 | 26 | 1,230 | 6 / 0 / 0 | 0 of 1,070 | 0 |
| pygments | HtmlFormatter._format_lines:829@45 | 298 | 4,502 | 46 / 53 / 0 | 859 of 2,754 | 1,017,511 |
| pygments | _match_entry:2785@1 | 127 | 1,835 | 21 / 7 / 0 | 26 of 1,764 | 5,341 |
| pygments | Lexer.get_tokens.<locals>.streamer:269@31 | 118 | 3,385 | 53 / 41 / 0 | 1,646 of 2,043 | 303,696 |
| pygments | Pattern.match:3077@1 | 96 | 2,250 | 28 / 14 / 0 | 154 of 2,209 | 249,261 |
| pygments | RegexLexer.get_tokens_unprocessed:700@46 | 94 | 1,543 | 25 / 7 / 0 | 34 of 1,343 | 57,846 |
| pygments | _entry:139@143 | 66 | 1,324 | 20 / 3 / 0 | 17 of 149 | 13,820 |
| pygments | _entry:1@1 | 61 | 259 | 0 / 0 / 0 | 0 of 0 | 0 |
| pygments | RegexLexer.get_tokens_unprocessed:700@43 | 50 | 2,585 | 25 / 24 / 0 | 1,992 of 2,386 | 376 |
| pygments | _entry:270@182 | 30 | 1,899 | 16 / 6 / 0 | 126 of 126 | 0 |
| pygments | str.__mod__:478@1 | 23 | 680 | 48 / 0 / 0 | 0 of 664 | 0 |
| pygments | HtmlFormatter._translate_parts:824@1 | 23 | 953 | 24 / 0 / 0 | 513 of 934 | 0 |
| pygments | compile:3567@280 | 22 | 1,540 | 25 / 16 / 0 | 1,169 of 1,345 | 0 |
| pygments | binary_op:94@1 | 21 | 747 | 9 / 0 / 0 | 74 of 467 | 0 |
| pygments | _lru_cache_wrapper.<locals>.wrapper:641@2 | 21 | 927 | 21 / 0 / 1 | 362 of 560 | 0 |
| pygments | regex_opt_inner:26@408 | 20 | 1,443 | 19 / 16 / 0 | 918 of 1,206 | 720 |
| pygments | str.translate:697@103 | 18 | 428 | 9 / 0 / 0 | 203 of 263 | 0 |
| pygments | list.extend:254@1 | 18 | 574 | 26 / 0 / 0 | 29 of 526 | 0 |
| pygments | _entry:1@143 | 17 | 207 | 0 / 0 / 0 | 0 of 0 | 0 |
| pygments | _Gen.excludes:1847@31 | 17 | 1,296 | 25 / 5 / 0 | 678 of 1,161 | 0 |
| pygments | _Gen.exclusive:2033@35 | 16 | 1,264 | 24 / 5 / 0 | 676 of 1,225 | 263 |

Totals over each program's 20: in-line IR at sites never called is pyflakes 18,999 of 28,087 (68%), black 4,835 of 19,584 (25%), pygments 9,476 of 21,125 (45%).

All logged sites per program (every build):
- Signatures. At least one constant argument: 42-57%. At least one virtual (with no constant): 6-18%. At least one type (none of those): 9-17%. All unknown: 11-39%. Most constants are kernel dispatch (call_special with its name).
- Shared keys. 4% (richards) to 43% (tomli) of sites share their key with another site of the same build: one call inlined at several places under the same parent frame, mostly the kernel's call_special, getitem, compare and contains. A per-site counter keyed as today cannot tell those copies apart, so their calls merge. The design's risk 13 treats only hash collisions; this merging is far larger.

Reading:
- In-line IR mostly goes to code that never runs. In the named units 46-89% of in-line IR sits at sites with no call in the steady value (tomli 62%, django's three units 83-89%, regex 46-76%, genshi @116 60%). Over the cli programs' 20 largest units the share is 25-68%. The design's premise, that today's first builds take IR on no evidence, holds.
- The hot sites out of line are small callees. Most own units are 54-480 IR (tomli's seven at 316k calls a value, genshi's `_get` 137, regex's `_new_match` 271). Under the design's price they are bought at first build, where today the budget is gone by bytecode order. The large exceptions are callees whose own units are big kernel regex paths (Pattern.match 1,723) and django's render_value_in_context (1,711). There, in-context IR is the open question that check 2 and stage 2's records must answer.
- Calls per site at nested depth are not identifiable under today's key wherever kernel helpers are inlined repeatedly. The design's calls `N_i` for nested sites (stage 2's weights, stage 3's admission) must either key by the full inline path or accept merged counts.
- Abandon conditions: none. Check 1 is input to checks 2 and 4 and has no condition of its own. The shared-key finding is a design gap to fix in stage 1 (the key gains the callee) and stage 2 (records per path).

### Check 2: the fold against the machine (2026-11-25)

Method:
- The ten hottest root-level sites from check 1, spread over five programs.
- Each unit is built with the site forced in line and forced out of line, through the check 3 plan mechanism. Every other site of the root's last count-run build is pinned to its decision there, at every budget level. A forced-in callee's own call sites are new names, so they take today's test, and at a full unit that usually sends them out of line.
- One run per arm (check 5's runs repeat within 0.0-0.3%). Best value, run part only (value less its compile), M instructions. Saved per call = (out - in) / calls a value.
- P is jitcost's call-shape row (luc-1kz0 stage B, window harness): plain 435, method 503, kernel 216.
- The design's fold estimate was **not computed**. It needs block weights and an expected-priced-IR pass, well over 150 wip lines. Measured saving is compared with P alone.

| # | unit | site, callee | today | calls/value | run, out | run, in | saved per call | P (shape) | saved / P |
|---|---|---|---|---|---|---|---|---|---|
| 1 | tomli key_value_rule@3 | @59 len | out | 316,413 | 24,935.1 | 24,817.1 | 373 | 216 (kernel) | 1.7 |
| 2 | | @224 Flags.is_ | out | 316,408 | 24,886.1 | 24,747.1 | 439 | 503 (method) | 0.9 |
| 3 | | @273 NestedDict.get_or_create_nest | out | 316,407 | 24,886.1 | 24,576.1 | 980 | 503 (method) | 1.9 |
| 4 | django getattr | @39 lookup_attr | in | 40,000 | 860.4 | 891.2 | -770 | 216 (kernel) | -3.6 |
| 5 | django render_value_in_context | @111 conditional_escape | in | 20,000 | 933.8 | 887.4 | 2,320 | 435 (plain) | 5.3 |
| 6 | django VariableNode.render | @35 render_value_in_context | out | 15,354 | 895.2 | 953.0 | -3,764 | 435 (plain) | -8.7 |
| 7 | richards HandlerTask.fn | @275 Task.qpkt | in | 18,600 | 250.2 | 242.4 | 419 | 503 (method) | 0.8 |
| 8 | richards DeviceTask.fn | @82 Task.qpkt | in | 18,588 | 256.2 | 246.8 | 506 | 503 (method) | 1.0 |
| 9 | genshi XMLSerializer.__call__@97 | @104 _get | out | 11,996 | 1,869.8 | 1,892.4 | -1,884 | 435 (plain, shape unverified) | -4.3 |
| 10 | regex_v8 block0@12 | @38 Pattern.search | in | 6,510 | 909.8 | 902.0 | 1,198 | 503 (method, shape unverified) | 2.4 |

Reading:
- **Protocol alone is within 0.5-2 of the measured saving on 5 of 10 sites** (1, 2, 3, 7, 8): plain and method calls to small callees.
- **Specialization adds 2.4-5.3 times P on two sites** (5, 10). conditional_escape and Pattern.search take constant or typed arguments in context. This is the fold the design means to value.
- **On three sites taking the call in line loses** (4, 6, 9: -770, -3,764 and -1,884 per call).
  - In two of them (6 and 9, both out of line today) the forced copy enters a unit already at its allowance, so the callee's own calls go out of line. Its own unit, built as a root, has them in line. In-line here means in line with its subtree cut.
  - For 4 (lookup_attr in getattr, in line today) the out-of-line arm calls lookup_attr's own unit (482 IR, built for its own feedback), which runs better than the 141-IR copy.
  - The design values a site as `P + F` with `F >= 0`, so it predicts a positive saving at all three, and no fold estimate can match them.
  - The design's own-frame fold also leaves out exactly what decides 6 and 9: whether the callee's nested sites come with it. "Anything behind a nested site (valued at that site)" does not hold when taking the parent in pushes the children out.
- **Abandon condition for stage 2** (the fold outside 0.5-2 of the measured saving on more than a third of hot sites):
  - Not decidable as written, because the fold was not computed.
  - Any `P + F` with `F >= 0` is outside the band on at least 3 of 10 sites (the three losses). That is one site short of the condition. With P alone, 5 of 10 are outside.
  - I read this as stage 2 not yet falsified but at risk. Before stage 2 is built, its value term must price a site with its subtree's fate (callee's sites in or out at the marginal price), not own-frame only, and check 2 must be repeated with the estimate computed.
- **Stage 1 is unaffected**: it buys sites by counted calls at P, and sites 1-3, 7 and 8 show P is the right order for small callees.

### Check 8, partial: the build model above 8k IR

Only one build of the table programs reaches 8k IR: tomli's key_value_rule@3 at 15,733 IR. The count run's first timed value holds it and five small builds: predicted 1,659M (it alone 1,340M); measured compile in that value 1,190M (wip arm). So `n(6500 + 5n)` overestimates the 15.7k build by between 11% and 28% (0.72-0.89). Whole runs (all builds, predicted against measured compile): tomli 1.04, django 1.18, genshi 1.19, richards 1.20. One build is not a model check; the 8k-32k jitcost rows are still needed.

### Not done in this pass

- Checks 4 (replay), 6 (weights: needs the branch-word predictor, which is stage 2 code), 7 (evaluator share by unit size: needs per-build profiles), 9 (loops peeled and refused: needs evaluator counters).
- Check 8 beyond the single build above.

### Stage 0 state (2026-11-25)

Done: checks 5, 3, 1 and 2; 8 in part. Not done: 4, 6, 7, 9. The wip instrumentation is a separate commit on luc-site0 that does not land. The scripts stay local.
- **Stage 1 (sites, not levels) survives.**
  - Check 2 shows P is the right order for small plain and method callees.
  - Check 3 shows that spending the same allowance by counted density gains on every row but pyflakes (+0.1%).
  - Check 1 shows that 46-89% of in-line IR in the named units runs no calls.
  - The site key must change, beyond gaining the callee: 4-43% of sites share a key within one build.
- **Stage 2 (the fold) is at risk and must be redesigned before it is built.** On three of ten hot sites, taking the call in line loses, and own-frame `P + F` with `F >= 0` cannot predict that. The value of a site must carry its subtree's fate. Repeat check 2 with the estimate computed.
- **Stage 3 cannot be judged yet.** It needs checks 4 and 6. Its fallback (check 3's arm) is shown to work with oracle counts.
- **Ruling 2 (acceptance metric) is needed for django_template only.** Pre's lead there is growth unpaid by -n 3: its extra compile is 665M against a run lead of 56M a value. Richards' and genshi's leads are paid inside the benchmark's values (check 5).

## Revision 1 (2026-11-25, after stage 0)

Read from origin/master at 1e1497e0, which the local ref already held, so I did not fetch. `runtime/jit/jit.c` moved since the design's tree: `hot_exit` is now at 2120-2183, `unit_losses` and `out_cost` at 2068-2105. Nothing was built or run. Predictions are arithmetic on stage 0's tables with `n(6500 + 5n)`.

## R0. What changes

- **Cut:** the fold, trials, records per signature, the expected-priced-IR pass, the price rule that replaced the allowance, and the shared rebuild ledger. These were old stages 2, 3 and 4.
- **Kept:** stage 1 (sites, not levels; exact arming), with two additions from stage 0: a path key, and a bought site built as its callee's own unit is.
- **New stage 2:** do not build what the profile does not show running. It is gated on one offline check (R2).
- **Why:** check 3's gain is hot sites coming in, and out-of-line sites already have exact in-context counters. Check 2 shows protocol alone is the right order for small callees, and that no non-negative fold can predict three sites in ten.
- `INLINE_BUDGET` stays as the first build's allowance. It stops being final, and with cold sites out it should rarely bind. Removing it is deferred behind a measured need (R6).

## R1. Stage 0 against the design

| Design claim | Stage 0 | Consequence |
|---|---|---|
| First builds take IR on no evidence | Confirmed: 46-89% of in-line IR in the named units and 25-68% in the cli top-20 is at sites never called | Stage 2 |
| A site left out for budget should be bought by its count | Supported: the hot sites out are small callees (54-480 IR), and protocol is within 0.5-2 of the saving on 5 of 10 | Stage 1 unchanged in substance |
| Value is protocol plus a non-negative fold | Cannot hold on 3 of 10 sites (losses of 770 to 3,764 a call) | Fold dropped; R4 |
| Pre's lead is unpaid growth on all three rows | Django only | R5, R7 |
| The site key needs only the callee added | No: 4-43% of sites share a key in one build | R3 |

## R2. Not building the cold half, with build-time counts only

What exists at build time:
- **The instruction's own feedback word.** Zero means the interpreter never ran it, in any context. That is the CALL bit (`runtime/jit/jit.h:164-172`), the operator kinds word, and LOAD_ATTR's counter (`runtime/jit/eval_attr.c:126-129`). Today it is read only for the root frame's put-off (`runtime/jit/eval_arith.c:7-18`, `jit.c:1335`), and a never-run nested site is taken in line like any other.
- **Branch words.** A direction under 1 in 64 is already not built (`runtime/jit/eval_ops.c:267-276`). A branch the interpreter never executed has a word of zero, which is not "rare" by `lu_jit_branch_rare` (`jit.h:210-218`), so both arms are built. That is one way never-run code gets in.
- **Entries shown:** the threshold, or the killed unit's entries.

The rule: at every build, a site the profile does not show running is left out through `lu_ev_room`'s existing refusal path, so it gets a counter (`runtime/jit/eval_call.c:207-220`). Two forms, smallest first:
- **(a) Word is zero.** About 15 lines, no new state.
- **(b) Expected calls shown below one:** entries shown times a block weight from the branch words, with a zero word as weight zero. About 80 lines: a float per IR block, set in `lu_ev_fork_with` (`runtime/jit/eval_flow.c:1942`) and summed in `lu_ev_merge` (`eval_flow.c:1147`).

No density sort happens at a first build. A one-pass evaluator does not know its nested sites in advance, and check 3's plan had a whole earlier build to read. Density order is used where counts are exact: at purchases (stage 1). Sites shown running go in under today's allowance in bytecode order, as now.

How close can this get to check 3's oracle? I do not know, and I expect two regimes:
- **User code:** the words are the code's own and few callers share them. I expect most never-called sites to show zero words or zero-weight blocks.
- **Shared kernel code in line** (`call_special`, the attribute path's type and module arms): the words are every caller's. A site cold in this context but common elsewhere reads as running, and no context-free count can see it. Django's `getattr` (713 of 829 IR cold) is probably this kind.

Check 6, made cheap. Add two fields to the existing wip site log on luc-site0: the site's word (zero or not), and for form (b) the weight product (about 40 wip lines, logging only). Join offline with the count runs already taken. Per program, IR-weighted and split by kernel or user frame, report:
- **Recall:** the share of never-called in-line IR that the test marks.
- **Miss cost:** the share of in-line calls at sites the test marks.

Stage 2 is built only if recall is at least half on the cli top-20 and miss cost is under 1%. If form (a) reaches that alone, form (b) is not built.

Predictions per row (R is recall):
- **cli compile:** IR in the largest units falls by at most R times 68% (pyflakes), 25% (black), 45% (pygments). Check 3's cli IR did not fall because the freed allowance was refilled; the same refill by sites shown running will take part of this back.
- **tomli `key_value_rule`:** in-line IR falls from 15,424 by R times 9,500. At R = 0.5 the unit is about 11k IR and a build costs 0.66G against 1.34G by the model, across 8 builds. Run code barely moves.
- **Run rows:** small. Cold-out frees allowance, so sites that were cut fit at first build instead of waiting for a purchase.

What bounds its mistakes:
- A site marked cold that runs has a counter armed at its break-even. It loses protocol per call until bought, once, at most its price plus one rebuild.
- A site marked running that never runs costs what it costs today.
- Under `lucifer_jit=always`, a first build that is not put off leaves its zero-word sites out. Some jitcheck lanes will then see a purchase where they saw a first-build inline, and may need their sizes moved.

## R3. The site key

Today the key is the site's code and pc and one parent frame (`jit.c:1261-1267`, `eval_call.c:195-196`). Every copy of a kernel helper shares its inner sites' keys, whatever called the helper.

Proposed: the path of call sites from the unit's root, mixed over the frame chain as `lu_ev_prof` already does for profile places (`runtime/jit/eval_emit.c:108-116`), plus the callee's code. The bought set is kept per entry.

Copies with the same path (the arms of one instruction over learned types, a peeled loop's two turns, a return duplicated through its caller) share one decision and one counter on purpose. They are one site of the program in one context, and its price is the sum of its copies.

Cost:
- A walk of at most 24 frames per decision at build time.
- The same 64-bit key and the same `outs` dedup (`eval_call.c:208-217`).
- More `lu_out` entries (40 bytes each), since nested sites stop merging.
- No extra instruction in compiled code.

Check: rerun the site log with path names. Sharing between sites with different parent chains must be zero; report what remains.

## R4. Check 2's negative cases

Sites 6 and 9 are the allowance, not the value. The forced copy's own calls met a full unit and went out, while the callee's own unit has them in line. Today's hot-site path already avoids half of this: a bought site's callee gets a fresh base allowance (`eval_call.c:224-243,594-598`). Check 2's forcing did not give it.

Rule for stage 1: a site taken in line is built as its callee's own unit is.
- It gets the same allowance.
- It gets the callee's own bought sites, found by the path relative to the callee (one more lookup per enclosing frame).
- It is priced at the callee's own unit, whole subtree.

Protocol is then a floor on the saving for these two, and the parent's value assumes nothing about its children that the build does not deliver. A top-down density order with no allowance would also do this, but that is the deferred price rule.

Site 4 is not selection. The 141-IR copy of `lookup_attr` in `getattr` loses 770 a call to its own 482-IR unit, with no allowance involved. Cause not established. My hypothesis is that the own unit builds from the code's own feedback while the copy reads only the context's places (`.tickets/luc-1kz0.md:165`), which here know nothing. No ledger sees it: an in-line site has no counter and no loss.

Something more is needed for site 4, and it belongs to which profile an in-line frame reads, not to this ticket. First measure its size: repeat check 2's forcing out on the 20 hottest in-line sites per program, and dump both builds of `lookup_attr`.

It also shows that smaller IR is not faster code, which is a second reason to drop any fold measured in IR.

## R5. genshi and richards

**richards (time to best build).**
- The one site out is nested `Task.addPacket@82` to `Packet.append_to`: own unit 39 IR, 17.0k calls a value.
- Master bought it as a doubling of the whole unit, price 38.8M, after re-arming by doubling (`jit.c:2159-2166`). It paid 59.4M at 65,536 entries, in value 8.
- Stage 1 prices it at the build that takes it: `B(1,163) = 14.3M`, against 7.1M a value at 420. That is about two values, with no overshoot.
- Predicted: best of three matches pre (check 3 measured 242.4 with this site in), and the late values of "all values" close.
- Not addressed: most of master's +173M over all values at -n 3. The steady gap is only 4M a value, and luc-1kz0's diagnosis puts 199M in value 0 (756M against 557M, an older master). That is learned-rebuild and mark timing under break-even, a known ledger loss.

**genshi (steady-state gap).** Three mechanisms in luc-1kz0's log:
1. **Hot sites never bought.** One site must cover the unit's whole build alone, cost a quarter of it, and be called 64 times an entry or on half a resume unit's turns (`jit.c:2127-2157`). `XMLSerializer.__call__@116` is 6,848 IR, so one site must lose 279M; its sites make 4.4k calls a value.
2. **Growth priced as a doubling** of large generator units.
3. **Marks priced at every live unit** of a generator root (`jit.c:724-732`): 62.6M and 64.4M for `_flatten@69/@267`, with 69 marks against pre's 101 and `h_call_any` at +6.9M.

Stage 1 removes 1 and 2. Its ceiling is check 3's 41% of the -n 3 gap: 973k calls a value come in across genshi's units. Mechanism 3 is rule change 4 of luc-1kz0, built on a scratch branch and never tabled alone; it should be tabled alone under its own ticket. The remaining share is undiagnosed.

Falsifier for both: stage 1's event log at -n 20. If out-of-line calls a value are near zero and the gap stands, the rest is not inlining.

## R6. The smallest design worth building now

Fold and trials: dropped, not deferred. Check 2 shows protocol suffices where purchases matter (small hot callees), that the specialization on 2 of 10 sites only delays a purchase, and that the model's sign is wrong on 3 of 10. The one row that needed early purchase of large specialized callees was django, whose lead is unpaid (check 5).

| Stage | Change | Files | Lines (+/−) | Lanes, failing first | Predicted | Falsifier not yet run |
|---|---|---|---|---|---|---|
| 1 | Sites, not levels. `hot_exit` buys the densest sites by counted calls at today's `out_cost`, against the whole build `B(n + own units)`. Path key with callee, per entry. A bought site built as its callee's own unit. A site is priced once its callee has a unit. Arm for the shortfall. Delete `grown`, `MAX_GROWN`, `LOOP_CALLS`, the quarter and turns clauses | `runtime/jit/jit.c`, `runtime/jit/eval_call.c`, `runtime/jit/target.h`, `tools/jitcheck`, two kernel programs | runtime +120/−95; tools +60/−45; tests +160 | `sitefinal`: a hot call once per entry behind more cold calls than four doublings hold. `sitepath`: one helper in line under two parents, hot under one; only that copy's site is bought. `siteshape`: a bought callee's own calls are in line as in its own unit. `sitechain` and `siteentry` replace `growchain` and `entrygrowth` | tomli about −2.9% (check 3's 27%; rebuild 1.5G by the model against 0.99G a value, so bought in value 1-2). richards best value to pre's. genshi up to −2.7% at -n 3. django about −1%. black and pygments toward −1.0% and −2.9% | Check 4 offline, protocol only, from check 1's counts: the value index of each purchase per unit. No new run |
| 2 | Cold sites out, form (a), or (b) if the check asks | `eval_call.c`; for (b) also `eval_flow.c`, `eval_ops.c`, `runtime/jit/ssa.h`; `tools/jitcheck`; one kernel program | (a) +20/−0; (b) +80 more; tools +40; tests +120 | `coldsites`: a callee whose never-run arms hold large calls; IR built must not include them, and one arm turned hot late is bought once | cli compile IR down by at most R times 25-68% in the largest units; tomli's build cost roughly halved at R = 0.5 | Check 6 as in R2. Then full jitcheck on the wip arm for lanes that move |
| 3, only on evidence | Value at protocol alone: delete `unit_losses`, `per_entry`, `lost_entries` and `out_cost`'s callee term | `jit.c`, `target.h` | +5/−55 | `megaiter` and `sitechain` must hold | regex_v8 windows' compile toward pre's (1.78G and 3.0G against 4.44G and 10.2G) | Stage 1's regex log: the share of purchases whose value was mostly callee losses. Build only if those purchases are still made |

Each stage is tabled alone on the rows of section 8, judged as in R7.

Deferred, each with what reopens it:
- **Removing the allowance** (the price rule): reopen when sites shown running but refused for allowance at first builds carry more than 1% of calls after stages 1 and 2.
- **Counters on a first unit's in-line sites:** reopen when check 6 shows low recall in kernel frames. The wip `LUCIFER_SITES=2` mode can give their run cost in one measurement.
- **A generator call constant:** reopen when genshi's generator sites are bought late.

## R7. Acceptance metric

I agree with the coordinator's inclination, with one sharpening from check 5.

Judge each pyperformance row on two figures and never on best of three alone:
- **All values at the reference -n:** warm-up plus timed values, compile included. This is the benchmark's own work on a short horizon.
- **Best value at -n 20:** the steady state.

A change must not lose more than 1% on either. Best of three is reported, not judged.

Use "all values", not the whole process, for these rows. Check 5 shows the whole process mixes in 231M to 356M of start-up saving that has nothing to do with the benchmark's units, and it flipped the sign on richards and genshi. The cli rows stay whole-process.

On this metric:
- **django_template is not a loss:** master leads by 5.9% over all values and ties in steady state. It leaves the target list.
- **richards is a short-horizon loss:** +13.5% over all values, level in steady state.
- **genshi is a loss on both.**

## R8. What would make me abandon what is left

- **Stage 1:** tabled alone, it loses more than 1% on either figure of any row because a doubling's breadth took in something no site's count asked for.
- **Stage 2:** check 6 gives recall under half or miss cost over 1%. Stage 1 then stands alone, and the cold half waits for in-context counts.
- **The reduced design as a route to genshi:** stage 1's log shows no out-of-line calls left and the steady gap unchanged. The gap then belongs to marks and learned rebuilds.


## Stage 1 falsifier: check 4 offline, protocol alone (2026-11-25)

Replay of R6's stage 1 rule on stage 0's count runs, no build. The count runs (wip binary of luc-site0, `LUCIFER_SITES=2`, -w 1 -n 3 at the reference loops) were repeated once with the units event log on the same stream, so every build carries its cause.

Rule replayed, per unit (root and entry):
- First builds and the master's own rebuilds (first, learned, marks, dependencies) happen as logged; each resets the unit's counts. The master's purchases (grown, hot-site) do not happen; the IR they added is taken off later own rebuilds, and the sites they took in stay candidates.
- Candidates: the sites the unit's build left out for budget, at any depth. A site is priced once its callee has a unit: m = the callee's last build. A bought site brings the sites its callee's own unit leaves out (R4), counted under this unit's names.
- Value: counted calls since the unit's build times 420 (CALL_COST alone; the runtime adds the callee unit's losses, so it buys no later than this). Calls are spread evenly over their window (400 steps a value).
- At each step: sites by density, admitted while the density covers the marginal price `6500 + 5(2n + m)`; the set is bought when its value covers `B(n + sum m)`, `B(n) = n(6500 + 5n)`. A purchase resets the counts.
- Caveats: names are today's site key (one parent frame), so copies under one parent share a count; a build is placed at the start of its value; cli programs are one window (the whole run), so their purchase points assume an even rate.

Value index: 0 is the warm-up value (start-up included), 1-3 the timed values; genshi's xml values are 4-7.

| unit | own builds | first IR | sites out at first build | stage 1 purchases: value -> IR after (price / paid, M) [sites] | protocol loss while out, M by value | master's purchases (value, kind, IR) |
|---|---|---|---|---|---|---|
| tomli key_value_rule@3 | 4 | 1,330 | 26 | 0.03 -> 1,822 (28.4 / 29.0) [skip_chars' getitem, contains; @68 genexpr; @67, @74 get_iter; @224 Flags.is_]; 0.14 -> 2,408 (44.6 / 45.6) [@273 get_or_create_nest, @63 type.__call__, @59 len]; 0.73 -> 3,352 (78.0 / 78.0) [parse_key_value_pair@74 parse_value, 944] | 203, 72, 72, 72 | 0 grown 2,110; 0 grown 3,988; 0 grown 7,792; 1 grown 15,733 |
| richards HandlerTask.fn | 2 | 1,094 | 6 | 2.35 -> 1,163 (14.3 / 14.3) [addPacket@82 Packet.append_to, 39] | 5, 7, 2 | none |
| richards DeviceTask, IdleTask, WorkTask .fn | 2-6 | 367-1,057 | 0-2 | none | 0 | none |
| django VariableNode.render | 3 | 1,175 | 13 | 3.83 -> 2,862 (59.6 / 59.6) [resolve@166, @206 getattr, 880 each] | 24, 34, 34, 31 | 2 grown 2,878 |
| django render_value_in_context, Node.render_annotated, getattr | 2-6 | 1,020-1,184 | 3-30 | none | 0 | none |
| genshi XMLSerializer.__call__@116 | 3 | 1,760 | 52 | 5.30 -> 2,557 (49.3 / 49.3) [8 sites: _get x2, contains x2, Attrs.get, type.__call__, truth, int.__lt__] | 44, 30, 65, 65 (values 4-7) | 4 hot-site 4,085 |
| genshi XMLSerializer.__call__@97 | 1 | 1,621 | 15 | 7.29 -> 1,895 (30.3 / 30.3) [_get x2] | 8, 12, 12, 5 | none |
| genshi XMLSerializer.__call__@55 | 1 | 1,962 | 14 | none | 5 | none |
| regex_v8 block0@12 | 2 | 2,035 | 85 | none | 6, 3 | none |
| regex_v8 Pattern.sub | 2 | 1,239 | 13 | 0.76 -> 1,556 (22.2 / 22.2) [8 _subx sites, 18-31 IR] | 33, 14, 9, 9 | 1 grown 3,547 |

Cli top-20 units by predicted compile (one window). Units not listed buy nothing.

| program | unit | own builds | first IR | stage 1 purchase: share of run -> IR (price / paid, M) [site] | protocol loss while out, M | master's purchases (kind, IR) |
|---|---|---|---|---|---|---|
| pyflakes | Checker.NAME | 8 | 1,092 | 0.83 -> 1,269 (16.3 / 16.3) [lookup_attr@47 generic_getattr] | 21.4 | grown 2,590 |
| pyflakes | Checker.handleChildren | 1 | 2,389 | 0.73 -> 3,126 (69.2 / 69.3) [5 sites: isinstance x3, contains, handleNode] | 108.0 | grown 3,973 |
| pygments | HtmlFormatter._format_lines@45 | 3 | 2,582 | 0.60 -> 4,868 (150.1 / 150.2) [get_tokens_unprocessed@56 Pattern.match, 2,250] | 231.6 | grown 3,046 |
| pygments | Pattern.match | 3 | 1,198 | 0.48 -> 2,521 (48.2 / 48.4) [match@48 _new_match] | 52.3 | none |
| black | (none of the 20) | | | none | 0-14.4 each | Pattern.match grown 3,119; Driver.parse_tokens hot-site 1,166 |

Against R6's predictions:
- **tomli: holds, earlier and smaller.** The root's out sites are bought in value 0 (at 0.03 and 0.14) and parse_value at 0.73, three rebuilds priced 151M in all against the master's four doublings to 15,733 IR (1.92G by the model). The unit stays at 3,352 IR. 72M a value is still lost at protocol after that: parse_value's own sites at depth 2 (three calls of Pattern.match, own unit 1,959 IR, match_to_number 472, Match.end 64, compare 340; about 28k calls a value each), whose price at that size is about 190M against about 35M a value. Check 3's plan kept those in line under the 16,000 allowance; stage 1 does not, so its run part may stay above check 3's arm.
- **richards: holds.** Packet.append_to is bought in value 2, after about 28k calls (11.2k in value 0, 17.0k a value after), 14.3M as R5 predicted.
- **django: later than master at protocol alone.** VariableNode.render buys its two getattr sites late in value 3; the master grows it in value 2. The runtime values the calls at today's out_cost (420 plus getattr's unit losses per entry), so it buys earlier than this replay; by how much the logs cannot say.
- **genshi: later than master's hot site.** @116 buys eight sites in xml value 1 (the master's hot-site build is in xml value 0, 4,085 IR), and 65M a value is still lost at protocol after the learned rebuild of xml value 2. @97 buys its two _get sites only in xml value 3. R6's "up to -2.7%" is at risk on genshi's -n 3 figure.
- **regex_v8 Pattern.sub:** bought in value 0 at 1,556 IR, where the master grows it to 3,547 in value 1.
- **cli: no chain.** At most one purchase per unit among the top 20; every purchase is paid by losses counted in its own unit (by construction of the rule; nothing in the logs contradicts it). Stage 1's purchases in the top 20 are priced 85.5M (pyflakes), 198.3M (pygments), 0 (black), against the master's growth and hot-site builds there of about 155M, 145M and 83M by the model. Pygments' purchase is the larger one (Pattern.match's 2,250-IR unit in line in `_format_lines`), so cli compile ms is a risk on pygments.

No pattern contradicts R6 as stated (tomli bought by value 1, richards within about two values, no cli chain priced above its losses). Stage 1 goes ahead. Risks carried to the table: django and genshi buy later than the master at protocol alone; tomli keeps 72M a value out of line at depth 2.

## Stage 1 built and tabled alone (2026-11-25, not landable: three misses)

Branch luc-site1 on origin/master ae250d27: lanes d6dca23c, change 9afb781e.

What was built (runtime/jit/jit.c hot_exit, eval_call.c lu_ev_room):
- A look values each site left out at its calls times today's out_cost and, once its callee has a unit, prices it at that callee's last build (`built_ins`, any entry). Sites by density; admitted while value per IR covers `BUILD_INS + BUILD_INS2 (2n + m)`; bought when the admitted sites' value covers `B(n + sum m)`. Kill cause `sites`, a `buy` event per site (callee, IR, calls).
- Path key: the callee's code mixed with each frame's (code, pc) from the site to the root, as `lu_ev_prof` mixes places. The bought set is per entry (`jc->bought`: entry, key). At each call the evaluator looks the key up at every enclosing frame: the root's entry, and each in-line frame's code at its call entry, so a bought callee brings the sites its own unit bought (R4).
- A bought site goes in line with a fresh allowance (today's hot-room mechanism), whether or not the budget is spent.
- Arming: each site is armed for `due * calls_i / value_total` calls (its share of the shortfall at the proportions seen), at least `DEOPT_INS / out_cost`; `due` is the admitted set's shortfall, or the densest site's alone when none is admitted; when no site is priced, as many calls again as were made (today's rule). The first arm is unchanged (the unit's IR count).
- Deleted: `grown`, `MAX_GROWN`, `LOOP_CALLS`, the hot-site quarter and turns clauses, `lu_jit_site_key`, `lu_jit_hot_site`, `hots`, `refused` (unit and func). First builds unchanged.

Where it departs from Revision 1:
- Copies on one path (arms, a peeled turn) all go in line once bought, not once per build as the hot site did.
- A peel refused for room no longer feeds anything (it fed `grown` through `refused`); such a loop is not peeled later.
- m is the callee code's last build of any entry, not specifically its call entry.

Lanes (tools/jitcheck), master -> stage 1:
- sitefinal (jit_site_final.py, 160k/320k): FAIL 222,893 -> 438,479 compiled calls -> PASS 157,585 -> 218,366.
- siteshape (jit_site_shape.py): FAIL 339,139 -> 665,788 -> PASS 249,911 -> 265,909.
- sitepath (jit_site_path.py, 320k, event log): FAIL no purchase of leaf -> PASS: the build that took leaf in added 348 IR (leaf's own unit 514).
- sitechain (jit_site_chain.py, was jit_grown_generic.py / growchain): FAIL no purchase -> PASS 1 purchase, 18 sites, 146.6M predicted.
- siteentry (jit_site_entry.py, was jit_entry_growth.py / entrygrowth): FAIL -> PASS, 11 builds of entry; no loop entry carries bought sites.
- Held: churn (2,861 first, 13,452 rebuilt, bound 58,418 -> 2,902, 13,441, 58,459), classtests (7,585 -> 7,629 rebuilt), learnprice (4,390 -> 4,390), typed (633,375 -> 629,521 helpers; ledger 17,828 -> 17,859 rebuilt), grown (1,948 -> 1,978 IR), dispatch (equal), megaiter (476,123 -> 496,726 on master; 160,765 -> 161,469: the hot sites the quarter clause blocked are bought). growchain and entrygrowth are replaced (their property was MAX_GROWN and grown levels, which are gone); methodgrow is not on master.

Gate: focused kernel programs (75: the jit_* call, growth, gen, mark, policy, site programs, jit_direct_calls, jit_namespace_collect, entry_tools_midcall) 75/75 in jit-off, plain, jit, jit-deopt; threads 7/7 in handoff; tools/jitcheck 1 failing: toolsoff (cprofile output differs), which fails the same way on master; tools/jitcost PASS. New programs and lanes in a fresh worktree built with plain make -j4: 5/5 in all four lanes, lanes PASS.

Table, R7's metric. Instructions:u on core leases, pinned, two runs per arm alternated, min of the two; master = origin/master ae250d27. pyperformance rows at the reference loops (django_template, richards 2; deepcopy 218; the rest 1), -w 1; "all values" is warm-up plus timed values at -n 3, compile included; best value at -n 20; best of three reported only. M instructions, compile in brackets. Regex windows as luc-1kz0 (-l 32/64/128, compile = build-pipeline self samples x 1.13). Cli rows whole process, compile ms from -X lucifer_jit_stats in the same pinned runs.

| row | figure | master M (compile) | stage 1 M (compile) | stage 1 vs master | runs |
|---|---|---|---|---|---|
| django_template | all values, -n 3 | 4,693.4 (667.2) | 4,622.2 (572.6) | -1.5% | 2/2 |
| django_template | best value, -n 20 | 853.6 (0.0) | 853.6 (0.0) | +0.0% | 2/2 |
| django_template | best of three, -n 3 (reported) | 932.4 (25.4) | 917.4 (11.6) | -1.6% | 2/2 |
| genshi | all values, -n 3 | 8,996.0 (1,088.2) | 8,859.8 (1,247.6) | -1.5% | 2/2 |
| genshi | best value, -n 20 | 1,862.4 (2.0) | 1,623.4 (0.0) | -12.8% | 2/2 |
| genshi | best of three, -n 3 (reported) | 1,876.6 (7.0) | 1,821.2 (79.2) | -3.0% | 2/2 |
| richards | all values, -n 3 | 1,445.0 (283.6) | 1,445.0 (292.6) | +0.0% | 2/2 |
| richards | best value, -n 20 | 240.0 (0.0) | 240.0 (0.0) | +0.0% | 2/2 |
| richards | best of three, -n 3 (reported) | 245.8 (0.0) | 240.0 (0.0) | -2.4% | 2/2 |
| tomli_loads | all values, -n 3 | 103,990.4 (2,748.0) | 93,998.4 (1,809.0) | -9.6% | 2/2 |
| tomli_loads | best value, -n 20 | 22,512.1 (0.0) | 22,501.1 (0.0) | -0.0% | 2/2 |
| tomli_loads | best of three, -n 3 (reported) | 25,034.1 (0.0) | 22,748.1 (55.0) | -9.1% | 2/2 |
| deepcopy | all values, -n 3 | 5,470.8 (549.0) | 5,573.4 (705.6) | +1.9% | 2/2 |
| deepcopy | best value, -n 20 | 947.4 (0.0) | 927.0 (0.0) | -2.2% | 2/2 |
| deepcopy | best of three, -n 3 (reported) | 1,131.0 (37.8) | 1,151.8 (70.6) | +1.8% | 2/2 |
| regex_v8 | all values, -n 3 | 8,099.0 (2,364.0) | 7,840.0 (2,115.0) | -3.2% | 2/2 |
| regex_v8 | best value, -n 20 | 842.0 (20.0) | 833.0 (19.0) | -1.1% | 2/2 |
| regex_v8 | best of three, -n 3 (reported) | 1,132.0 (215.0) | 1,089.0 (165.0) | -3.8% | 2/2 |
| regex_v8 calls 64-128 | total / compile / run G | 54.27 / 4.06 / 50.22 | 51.49 / 1.15 / 50.34 | -5.1% total, -71.6% compile, +0.3% run | |
| regex_v8 calls 128-256 | total / compile / run G | 104.56 / 5.83 / 98.72 | 100.18 / 0.73 / 99.45 | -4.2% total, -87.6% compile, +0.7% run | |
| pyflakes whole | G; compile ms; IR | 8.070; 329; 175,619 | 8.436; 396; 195,228 | +4.5% | 2/2 |
| black whole | G; compile ms; IR | 12.797; 586; 264,079 | 12.797; 588; 264,081 | +0.0% | 2/2 |
| pygments whole | G; compile ms; IR | 13.881; 435; 161,453 | 13.496; 504; 163,320 | -2.8% | 2/2 |
| cli compile ms (sum) | ms | 1350 | 1488 | +10.2% | |
| awfy-Richards per iteration | M | 129.42 | 129.42 | -0.0% | |
| awfy-DeltaBlue per iteration | M | 5.33 | 5.33 | +0.0% | |
| awfy-CD per iteration | M | 249.87 | 248.68 | -0.5% | |

Misses (landing bar: no row worse than 1% on either figure, cli compile ms not above master):
- **deepcopy, all values at -n 3: +1.9%** (compile 705.6M against 549.0M; best value at -n 20 is -2.2%). Event log (-l 218 -n 3): stage 1 makes 7 site purchases where master made 5 doublings, each paid at its price: `_deepcopy_list` buys `_append` (12.5M paid / 12.5M price), `deepcopy` buys `object.__reduce_ex__` (18.7 / 18.7), `_deepcopy_dict` buys `_keep_alive` and `deepcopy` itself (1,336 IR; 52.6 / 52.4, the unit to 3,547 IR), `_deepcopy_tuple` three sites (33.9 / 33.9), `benchmark@57` four (18.8 / 18.8), `_reconstruct@3` three (20.0 / 20.0), `__import__` two (17.6 / 17.6). Master's doublings were bought later (deepcopy at 65,536 entries, 45.9M paid for 34.4M). The extra compile is purchases paid inside the short window that the steady state repays (-2.2%).
- **pyflakes whole: +4.5%** (8.070G -> 8.436G; compile 329 -> 396 ms; IR built 175.6k -> 195.2k). Three purchases, 127.3M by the model, against master's one growth (34.9M): Checker.handleChildren (2,334 IR, 58.8M), Checker.NAME (1,035 IR, 37.0M), _is_typing_helper@3 (1,151 IR, 31.5M), each paid at its price at protocol plus callee losses. The larger units are then rebuilt for what their roots learn at their new size (builds 466 -> 470, IR +14k), which the purchase's price does not include.
- **cli compile ms: +10.2%** (1,350 -> 1,488 ms: pyflakes +67, black +2, pygments +69). Pygments' whole run is -2.8%, but its purchase of Pattern.match into `HtmlFormatter._format_lines` (2,666 IR, 149.2M) makes later learned rebuilds of that unit larger (IR 161.0k -> 165.3k).

Rows that gain: tomli_loads all values -9.6% (compile 2,748 -> 1,809M; best of three -9.1%, steady state equal), genshi best value at -n 20 -12.8% (1,862.4 -> 1,623.4M; pre measured 1,601.2) and all values -1.5%, django all values -1.5%, regex_v8 all values -3.2% and its windows' compile -72% and -88% (run +0.3%, +0.7%), richards best of three -2.4% (Packet.append_to in value 2, as the falsifier predicted), AWFY CD -0.5%.

Reading: the site grain recovers the rows R6 named (tomli, genshi, richards, regex windows), and the losses are not a doubling's breadth (R8's abandon condition is not met). They are purchases that pay their own build but not the rebuilds the larger unit then incurs, on cold cli code and on deepcopy's short window. Check 6 (D) was not run: the bar is not met.

## State (2026-11-25)

- Revision 1 is current. Falsifier (check 4 offline): done, no contradiction. Stage 1: built and gated on luc-site1 (lanes d6dca23c, change 9afb781e), tabled alone against master: misses deepcopy all values +1.9%, pyflakes whole +4.5%, cli compile ms +10.2%; not landable as is. Check 6 not run; stage 2 not built.
- Open question for the coordinator: price a purchase with the rebuilds its larger unit will incur (the cli and deepcopy misses), or accept those losses against the tomli, genshi and regex gains.
