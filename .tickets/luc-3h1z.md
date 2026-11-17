---
id: luc-3h1z
status: open
deps: []
links: []
created: 2026-11-17T05:59:28Z
type: bug
priority: 3
assignee: Greg Wedow
---
# passwd/grp text decoding and NULL fields differ from CPython

runtime/prim/passwd.c text() decodes strict UTF-8 where CPython uses the filesystem encoding with surrogateescape, and would crash on NULL pw_gecos/gr_passwd from NSS where CPython returns None. Also grp.getgrgid(2**128) raises KeyError instead of OverflowError (lib/modules/grp.py).

