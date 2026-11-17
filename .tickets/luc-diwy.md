---
id: luc-diwy
status: open
deps: []
links: []
created: 2026-11-17T14:36:00Z
type: bug
priority: 2
---
# csv reader/writer race under the JIT: concurrent next()/writerow see a field list of None

test.test_free_threading.test_csv TestCSV.test_concurrent_reader_next and test_concurrent_writer_writerow fail with AttributeError: 'NoneType' object has no attribute 'append' under -X lucifer_jit=always and deopt (alone, unloaded; also in the 11-16 jit floor). PASS in the off floor. CPython's C csv holds the object's critical section across a row; lib's csv shares mutable per-row state between threads.

