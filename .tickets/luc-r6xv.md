---
id: luc-r6xv
status: open
deps: []
links: []
created: 2026-11-04T19:51:51Z
type: bug
priority: 3
tags: [runtime, startup]
---
# set LC_CTYPE from the environment at startup

CPython calls setlocale(LC_CTYPE, "") during initialization (_Py_SetLocaleFromEnv), so locale.getlocale() is ('en_US', 'UTF-8') and nl_langinfo(CODESET) is UTF-8 under a UTF-8 environment; Lucifer stays in the C locale (getlocale() (None, None), CODESET ANSI_X3.4-1968). Visible now that _locale exists.

