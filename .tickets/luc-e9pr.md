---
id: luc-e9pr
status: closed
deps: []
links: []
created: 2026-11-17T21:29:31Z
type: task
priority: 1
---
# status: PyPy columns in the daily AWFY and suites tables

The README promises speed claims against CPython, PyPy and free-threaded CPython, but tools/status records no PyPy. Time PyPy with the CPython reference (--refresh-reference, PYPY set) and show PyPy and Lucifer/PyPy in the AWFY and suites tables; suites PyPy cannot run say so.


## Notes

**2026-11-17T23:20:39Z**

Landed: PyPy is timed with --refresh-reference when PYPY is set (AWFY at the daily's 3 iterations, the whole corpus at CPython's loops); the AWFY table gains PyPy and on/PyPy columns, the suites table PyPy failing, PyPy geomean and Lucifer/PyPy. PyPy cannot run cadence (numpy for 3.14t) and fails 4 pyperformance programs (fastapi, sqlalchemy_declarative fail; regex_compile, xml_etree output differs). Added cost: about 19 minutes on refresh days only (10 s AWFY, 1128 s corpus on one pinned core).
