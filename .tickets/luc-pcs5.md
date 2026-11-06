---
id: luc-pcs5
status: closed
deps: []
links: []
created: 2026-11-06T07:48:39Z
type: task
priority: 1
tags: [perf]
---
# _sre: searches without a literal prefix or charset run _match per position; json escaping 55x CPython

Found by the web benchmark (luc-pywv). On the Flask Todo app, in-process via the test client, Lucifer JIT on vs CPython 3.14t: list page 6.5ms vs 1.1ms, one-todo page 4.6ms vs 0.55ms, /api/todos (JSON of 20 dicts) 26ms vs 0.49ms. perf -X perf: ~65% of the JSON route is json.encoder.py_encode_basestring_ascii -> Pattern.sub -> _sre._search general case (ESCAPE_ASCII = '([\\"]|[^\\ -~])' has no INFO prefix/charset, so _match runs at every position), with py::compare:<frozen operators> ~50% inclusive under _sre._charset, i.e. int comparisons there reach the generic compare path instead of an inline int compare. Page routes spend ~24% in _sre (werkzeug get_current_url/uri_to_iri, host_is_trusted, Pattern.split/fullmatch). Fix generally: a first-character set computed for branch patterns (as the INFO charset), and int compares in _sre staying on the fast path. Separately, a _json accelerator (encode_basestring_ascii, scanstring) would match CPython's module set. No benchmark-specific changes.

