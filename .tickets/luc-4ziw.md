---
id: luc-4ziw
status: closed
deps: []
links: []
created: 2026-11-19T11:59:48Z
type: task
priority: 2
---
# a struct of one number packs to its bytes directly

pickle and base64 rows: pack('<I', n), pack('<B', n), packI in a85decode made a bytearray (type.__call__, not inlined), stored each byte under the bytearray's lock and copied it to bytes (bytes.__new__). A format whose one item is a number filling the struct returns int.to_bytes / the float's bytes directly.


## Notes

**2026-11-19T13:06:26Z**

Landed. pickle_pure_python 0.799, base64 0.808 of the stage below it. Whole lane against master ba3426d6: base64 0.300 (23.2 G -> 7.0 G), pickle_pure_python 0.472 (572 M -> 270 M), unpickle_pure_python 0.663 (491 M -> 325 M), logging 0.874; json_loads 0.994, json_dumps 0.999 (5 reps), pickle/unpickle/pickle_list/unpickle_list/pickle_dict 0.95-1.00, AWFY Json and Richards 1.000.
