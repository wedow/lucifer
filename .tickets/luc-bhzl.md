---
id: luc-bhzl
status: closed
deps: []
links: []
created: 2026-11-10T21:46:54Z
type: task
priority: 2
tags: [perf, jit]
---
# go: Square's attributes load generically in compiled code

pyperformance go on luc-custom4: Square.pos 2.1M, zobrist_strings 300k, neighbours 223k, timestamp 198k, board 127k generic attribute loads (h_load_attr_pic) in 2 loops; with dict probes and type lookups about 20% of the profile (probe_keys 6.7%, lu_dict_find_lockfree 4.4%, type_lookup 4.5%). Square gets all its names from __static_attributes__, so instances should keep their values in line; find why the sites are generic (a polymorphic site with another class that has pos, or instances in dict mode) and keep their arms.


## Notes

**2026-11-10T21:48:52Z**

Fixed on luc-custom4 (35e6591): Square's instances had moved their values to a dict, and a name in the class's shared names specialized only for an instance still in line, so the sites failed and their PICs found nothing to add. Such an instance now reads the dict by its hint. go 1956M -> 1620M instructions per loop. Left: module attribute loads (mt_random, random, sqrt, log) at about 42k per loop stay generic (their PIC tries run out with no entry); why Square's instances leave their inline values was not looked into.
