---
id: luc-ffdo
status: closed
deps: []
links: []
created: 2026-11-07T22:49:36Z
type: bug
priority: 2
---
# JIT: a global rewritten on every call disables its readers

Found with luc-ptxc. A function that reads and rebinds a module global on each call (_sre._text_of did, with its last-text cache) takes LOAD_GLOBAL guard exits and STORE_GLOBAL side exits; the guard failures recompile the code (GUARD_FAILS) until MAX_COMPILES, and the tier disables it (LUCIFER_JIT_DEBUG prints 'disabled _text_of'). _sre now keeps the cache in a list item, but the pattern is common in user code. re._parser._parse and _parse_sub, markdown's InlineProcessor.__processPlaceholders.<locals>.linkText and _IOBase.closed are also disabled on the regex corpus programs; their causes are not yet traced. A value guard that keeps failing should become a generic load once, not a recompile each time.


## Notes

**2026-11-08T04:41:12Z**

Fixed on luc-policy (luc-xboo): a LOAD_GLOBAL whose site is marked no-speculate compiles as a lookup (H_load_global, declined to the interpreter on a miss), STORE_GLOBAL compiles (H_store_global), and nothing is disabled by count. tests/kernel/jit_rebind.py: jitcheck rebind lane (exits and waiting entries flat, nothing disabled or retired).
