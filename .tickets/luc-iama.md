---
id: luc-iama
status: open
deps: []
links: []
created: 2026-11-25T17:35:00Z
type: task
priority: 1
parent: luc-orn7
---
# inline budget is a cliff: a unit slightly over budget loses every call site after the cut

Found while measuring luc-c997 stage 3 (tomli_loads, 11-25 daily +10.6%, bisected in luc-1kz0 to c684ad72). Belongs with the growth-valuation question recorded in luc-1kz0's findings (valuing a call left out of line by what inlining it removes).

## Evidence

tomli_loads (`run_benchmark.py --worker -l 1 -w 1 -n 3`), JIT on, `LUCIFER_JIT_DEBUG=1` build events and `LUCIFER_JIT_DUMP=1` IR, one build per commit. Under this run shape ad1eb461's grown-4 build is 13,826 IR; the 10,389 quoted in luc-1kz0 is its learned rebuild after that.

key_value_rule@3 builds from grown 3 on (built IR):

| commit | grown 3 | grown 4 and after | final unit (live IR) |
|---|---|---|---|
| ad1eb461 | 7,867 | 13,826, then learned 10,389 | 7,186 |
| c684ad72 | 7,867 | 15,606, learned 11,387, learned 15,738 | 11,193 |
| 72ee02e2, c15d6f64, ece1a949 | 7,838-7,867 | about 15.6k, 11.3k, 15.7k | about 11.1k |
| b20b81a9 (ledger) | 8,534 | 15,738, then grown 5 to 27,152 | |
| 01a16669 ... 2908a5db (master) | 8,611 | 15,821 only | 11,191 |
| 922ff849 (luc-c997 stage 3) | 8,611 | 15,078 only | 10,578 |
| 43239be3 (stage 4) | 8,617 | 15,086 only | 10,578 |

Live IR per inlined callee in the first grown-4 build, ad1eb461 -> c684ad72 (the unit gained 1,457 live IR; about 1,300 of it is the kernel attribute path, reached through isinstance's fallback `_instancecheck` -> `lookup_attr`, where c684ad72's type and module branches copy `_type_getattr` and `_module_getattr` again for an unlearned type):

| callee | ad1eb461 | c684ad72 |
|---|---|---|
| `_module_getattr` | 136 | 370 |
| `_type_getattr` | 248 | 482 |
| `getattr_hook` | 100 | 258 |
| `generic_getattr` | 600 | 769 |
| `lookup_attr` | 336 | 437 |
| `is_data` | 368 | 506 |
| `truth` | 0 | 141 |
| `_instancecheck` | 273 | 342 |
| `bool.__new__` | 0 | 45 |
| `_initializing` | 0 | 27 |

The sequence:
1. The extra ~1.3k IR makes the grown-4 build drop `Flags.set` (key_value_rule@370) for budget.
2. A learned rebuild (11,387) fits everything again.
3. Learning `Flags.set` triggers another rebuild (15,738), which runs out of the 16,000 budget; the budget is spent in bytecode order, so every call after the cut goes out of line.
4. From 01a16669 on (the ledger) there is no rebuild after grown 4: the truncated build is the final unit.

Call sites out of line in the final unit: ad1eb461 only key_value_rule@115 and @178; from c684ad72 on also key_value_rule@136, @224 (`Flags.is_`), @273 (`NestedDict.get_or_create_nest`), @370 (`Flags.set`), @63, @68, parse_value@319, @376, @400, @410 and parse_array@66. That matches luc-1kz0's profile of the loss (value 3, master minus f0136a06: lu_frame_push +131M, call_function +125M, lu_frame_call +106M).

luc-c997 stage 3 shrank the hooks: 743 built IR fewer (15,821 -> 15,078), out-of-line calls in the final unit 69 -> 53, but the same eleven sites stay out of line, and tomli_loads instructions did not move (JIT on 103.47G -> 103.51G).

## Two mechanisms

1. The budget is spent in bytecode order, so a unit a little over budget loses the tail of the function whatever its heat, not a proportional share. A build that knew it would not fit could choose which callees to leave out by cost and count (largest cold callees first).
2. Since the ledger there is no rebuild after grown 4 (MAX_GROWN), so a truncated build is final; before it, a learned rebuild could restore the tail.

## Acceptance Criteria

- tomli_loads back to or better than ad1eb461's instructions (both JIT modes as the ledger table measures them).
- No row of luc-1kz0's ledger acceptance table worse than 1%.
- A count check (tools/jitcheck lane) on a synthetic unit a few percent over the inline budget: it loses a proportional share of its inlining, not its tail.
- No fixed-number escape: the budget is not raised to make one program fit.

