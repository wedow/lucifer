---
id: luc-6ljp
status: open
deps: []
links: [luc-9ags, luc-j2vs, luc-uexr, luc-4yfw, luc-n6yj, luc-r1pk, luc-mltz, luc-m3wi, luc-xtmp, luc-po0x, luc-o8i6]
created: 2026-11-08T20:03:49Z
type: bug
priority: 2
tags: [jit]
---
# jit: interpreter-fed polymorphic method loads break the CFG in load_attr_poly's tail duplication

luc-r1pk lets the interpreter's generic LOAD_ATTR note shapes in the site's polymorphic cache (luc-9ags). For method-form loads (oparg & 1) that made load_attr_poly duplicate the following call per arm (call_end, through) in code it had not run that way before, and builds died with 'a block with a third successor' (tests/kernel/exc_kernel.py under -X lucifer_jit=always: module code of _collections_abc, _coro.close()). Method-form loads are excluded from interpreter noting for now; find the duplication bug and include them, which AWFY Richards' self.append (a staticmethod through an instance) wants.

