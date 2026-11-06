---
id: luc-v1cz
status: closed
deps: []
links: []
created: 2026-11-06T14:10:23Z
type: task
priority: 2
assignee: Greg Wedow
---
# Python _json accelerator: scanstring, make_scanner, make_encoder

Flask /api/todos spends ~19% in py_encode_basestring_ascii regex escaping after luc-pcs5. A _json module must be complete (scanstring, make_scanner, make_encoder, encode_basestring(_ascii)) or test_json's C-module classes lose passes. Measure json.dumps/loads vs CPython before and after.

