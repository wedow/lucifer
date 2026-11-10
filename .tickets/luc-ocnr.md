---
id: luc-ocnr
status: open
deps: []
links: []
created: 2026-11-08T23:25:28Z
type: bug
priority: 3
tags: [stdlib]
---
# array: an array is a bytearray instance

lib/modules/array.py makes array a bytearray subclass, so isinstance(array('Q', ...), bytearray) is True and code that takes len() of a bytearray as its byte count is wrong: test_bz2 test_issue44439 (BZ2File.write returns 5, not 40, for array('Q', [1, 2, 3, 4, 5])). CPython's array is not a bytearray.


## Notes

**2026-11-10T03:53:52Z**

Also behind: test_lzma FileTestCase.test_issue44439 and the five test_zipfile *WriterTests.test_issue44439 (write returns 5, not 40), test_zstd OpenTestCase.test_buffer_protocol (1000 != 4000 for an array('I')), and likely test_zstd test_read_readinto_readinto1 and ZstdDictTestCase.test_train_buffer_protocol_samples (buffer sizes of multi-byte arrays; unverified).
