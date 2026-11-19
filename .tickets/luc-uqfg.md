---
id: luc-uqfg
status: open
deps: []
links: []
created: 2026-11-19T12:00:49Z
type: task
priority: 3
---
# truth of an instance whose class has no __bool__ or __len__ is folded

unpickle_pure_python: _Unframer.read's 'if self.current_frame' (a BytesIO) and similar tests call lu_truth_exact out of line (1.5% with lu_jit_truth 0.8%). Under the class's version guard the truth of an instance of a class with neither __bool__ nor __len__ is True: fold it like IS_OP's known types.

