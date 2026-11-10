---
id: luc-qt3f
status: open
deps: []
links: []
created: 2026-11-10T17:41:01Z
type: task
priority: 3
tags: [perf, jit]
---
# Polymorphic property getters leave their inlined copies

LOAD_ATTR_PROPERTY (luc-custom3) inlines a property's getter, but a getter shared by several classes (io's _IOBase.closed reads self.__closed on FileIO, BytesIO, BufferedReader...) specializes its own attribute load on the site's global feedback, so its inlined copies leave on the other classes' guards until the getter is marked not to inline. k_bytesio_read went 8415 -> 9564 instructions per call when properties began to specialize (6443 with method binding since). Lever: in an inlined getter whose owner's class is fixed by the caller's version guard, specialize the getter's attribute loads for that class instead of the site's feedback.

