---
id: luc-u7xx
status: open
deps: []
links: []
created: 2026-11-19T11:37:20Z
type: task
priority: 2
---
# BytesIO.write takes bytes without a memoryview

pickle rows: every _Framer.write went through BytesIO.write's 'with memoryview(b) as view' (memoryview.__enter__/__exit__, nbytes, a view splice) for a bytes argument. bytes is written directly; other bytes-like objects keep the view. The TypeError for a non-buffer now says what C says (no 'memoryview:' prefix).

