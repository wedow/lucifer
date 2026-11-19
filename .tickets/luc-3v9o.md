---
id: luc-3v9o
status: closed
deps: []
links: []
created: 2026-11-19T11:14:15Z
type: task
priority: 2
---
# struct packs and unpacks a run of items at a time

Gap report mechanism #7: struct per format. Struct expanded its format into one tuple per item at first use (a Struct('!%dI' % n) made per call by base64's _85encode expanded n items), then packed and unpacked each through _pack_item/_unpack_item: a dict lookup of the code, a slice, int.from_bytes or to_bytes, a reversed copy for big-endian, a generator to the tuple. Pack and unpack now walk the compiled runs; integers of up to 4 bytes are put together from and stored as bytes items in line, 8-byte ones keep from_bytes/to_bytes. _int (luc-nvdz's shift) is unchanged.


## Notes

**2026-11-19T13:06:26Z**

Landed. base64 0.538 of the stage below it (16.0 G -> 8.6 G instructions per iteration), pickle_pure_python 0.894, unpickle_pure_python 0.965. Kernel test struct_runs; identical to the old module over random formats and values except the pre-existing native 8-byte message (luc-jni6).
