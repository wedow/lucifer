---
id: luc-oke3
status: closed
deps: []
links: []
created: 2026-11-10T05:41:04Z
type: bug
priority: 3
parent: luc-zelv
---
# bytearray.extend twin slower than master

k_bytearray_extend 181 ns/op on master, 226 with the fixed-arity twin (_extend) of luc-zelv stage 3 (CPython 57). Find what the twin's inline does worse than the generic call.


## Notes

**2026-11-10T13:03:29Z**

Fixed on luc-custom2 (35f31e6): when a kernel twin (or call_special's known function) did not inline, the opaque call still called the original *args/**kwargs function, so the twin only added work. The call now calls what it became. 400k bytearray.extend(b'ab'): 470M instructions before customization, 763M on master, 160M now.
