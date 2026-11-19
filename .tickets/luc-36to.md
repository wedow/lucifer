---
id: luc-36to
status: closed
deps: []
links: []
created: 2026-11-18T18:48:08Z
type: bug
priority: 3
assignee: Greg Wedow
---
# json_dumps +1.3% instructions, +3.5% cycles across the cq passes A-G

Accepted loss from luc-fhda..luc-um1n (f3879c4..720b666), measured on a core lease, 3 reps, master before vs after. Not attributed to a pass. Bisect over the seven commits by instructions, find which mechanism costs on json_dumps' shape (str building, dict iteration), fix in the mechanism.


## Notes

**2026-11-19T21:49:06Z**

Measured all seven steps from f3879c4 through 720b666 using the four inputs from pyperformance 1.14.0's json_dumps. Three fresh processes per revision, pinned to one core with rotating order and a fixed hash seed. Each process warmed up for 96 workload loops, then counted instructions and cycles for 36 loops.

| Revision | Median M instructions per loop | Change from previous |
| --- | ---: | ---: |
| f3879c4 | 449.109 | |
| 0c7fd970 | 448.470 | -0.14% |
| 45134d87 | 448.397 | -0.02% |
| 1a4164d5 | 448.252 | -0.03% |
| b768cf8e | 447.889 | -0.08% |
| 84b30d16 | 448.070 | +0.04% |
| 0689f73f | 447.893 | -0.04% |
| 720b666 | 443.398 | -1.00% |

The endpoint used 1.27% fewer instructions. The only upward step, 84b30d16, changed test tooling and produced a byte-for-byte identical executable to b768cf8e. With 24 warm-up loops, those identical executables showed up to 1.46% instruction spread within a three-run group and a 1.15% difference between their medians. With 60 warm-up loops and fixed process addresses, the medians differed by 0.03%. The largest repeatable move was in 720b666's release checks, which moved the frame and liveness checks to the decref slow path in runtime/jit/x86_64/emit.c and reduced instructions.

In sampled runs of 720b666 with process address randomization disabled, increasing warm-up from 24 to 96 loops reduced the estimated instructions in `type_lookup` from 866 M to 572 M across the 36-loop window. `lu_getattr_native` and C method binding also fell. Generic attribute lookup and method binding explain part of the short-window overhead.

The standard pyperformance worker confirmed the direction at the endpoints. Three fixed-address repetitions of the difference between 9 and 3 values after 10 warm-up values and 6 loops per value gave 448.150 to 443.938 M instructions per loop (-0.94%). Cycle medians moved in opposite directions in the two methods, with wide ranges, so the cycle loss remains unverified.

The reported steady-state instruction loss did not reproduce anywhere in the named range. The ticket remains open for a reproducible case. The starting revision for this investigation, ce303a3c, used 509.418 M instructions per loop in the same comparison, 14.89% above 720b666. That change falls after the ticket's bisect range and has not been attributed.
