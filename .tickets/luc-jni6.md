---
id: luc-jni6
status: closed
deps: []
links: []
created: 2026-11-19T12:00:49Z
type: bug
priority: 3
---
# struct: native 8-byte codes out of range say argument out of range

struct.pack('@q', 2**63) raises struct.error 'argument out of range'; CPython's native table (np_longlong etc.) says "'q' format requires -9223372036854775808 <= number <= 9223372036854775807"; the standard modes say 'argument out of range'. _struct._int picks the message by size and code only, not by mode. Waits for luc-nvdz (the shift in _int).


## Notes

**2026-11-20T17:41:19Z**

CPython 3.14.10t reports explicit integer bounds for q/Q in native and standard formats and for native n/N. Native P accepts signed negatives as pointer bits and reports pointer conversion errors for overflow. Both struct packing paths now use these rules. The struct_runs oracle comparison failed on the original code and passed after the fix in the plain, jit and jit-deopt lanes. This correction is independent of the shift specialization tracked by luc-nvdz.
