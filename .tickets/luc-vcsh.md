---
id: luc-vcsh
status: open
deps: []
links: []
created: 2026-11-14T18:20:17Z
type: bug
priority: 2
---
# test_statistics test_kde_random takes 2900 s interpreted

Lib/test test_statistics TestKDE.test_kde_random runs 2903 s with the JIT off, 403 s compiled at first call and 2978 s under deopt:7; CPython runs it far faster (not timed here). In the compiled run the volume is 242M float comparisons through the kernel's compare unit entered out of line (float.__lt__ behind it) and 91M list.__delitem__ calls through delitem. Find the caller that reaches compare and delitem out of line (a sort, a bisect or a del in a loop over 1M samples) and why neither is taken in line or answered natively.

