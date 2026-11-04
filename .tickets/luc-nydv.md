---
id: luc-nydv
status: open
deps: []
links: []
created: 2026-11-04T04:33:39Z
type: bug
priority: 2
---
# STORE_ATTR_SLOT and PyMember_SetOne take no object lock; failing C getters run twice through lu_descr_get_native

Review of luc-njhp, unproven: CPython locks slot stores (a double release of the old value under concurrent stores). lu_descr_get_native clears a C getter's error and lets the kernel retry, so side effects run twice.

