---
id: luc-0vrh
status: open
deps: []
links: []
created: 2026-11-21T14:34:28Z
type: bug
priority: 3
tags: [gc, startup]
---
# gc: code-image objects invisible to gc.get_objects/get_referrers/is_tracked

Since 526804bc (code image) the frozen and builtin-port modules' code objects and their constant tuples live in the image, outside the collector's heaps. CPython 3.14t unmarshals them onto the heap, so it reports them. Repro (lucifer vs oracle):

    import gc, posixpath
    co = posixpath.join.__code__
    print(gc.is_tracked(co), any(o is co for o in gc.get_objects()), gc.is_tracked(co.co_consts))
    import sys; print(sys._is_immortal(co))

CPython: True True True / False. Lucifer: False False False / True. gc.get_referrers(x) also misses image tuples and code that hold x. Options: have get_objects/get_referrers also walk loaded image code (lu_image_each_code) and its const tuples, and report image code as tracked; immortality itself stays (sys._is_immortal, sys.getrefcount differ as for CPython's static objects). Low priority: no known program depends on it.

