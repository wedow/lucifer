---
id: luc-wy7i
status: open
deps: []
links: []
created: 2026-11-09T08:43:22Z
type: bug
priority: 1
---
# Two Lib/test cases grow Lucifer past the test host memory cap: test_statistics TestKDE.test_kde_random, test_struct StructTest.test_count_overflow

In the full Lib/test run (2026-11-09) the worker running test.test_statistics.TestKDE.test_kde_random grew from 10 GB to over 20 GB in 15 seconds and the test host's per-command memory cap killed the whole run, twice. CPython 3.14t passes the case in well under a second. The case calls statistics.kde_random for each kernel and draws ten values; a non-converging inverse-CDF loop that allocates is the likely shape (unverified). The case is left out in results/libtest/lucifer.excluded.tsv against this ticket; remove the row with the fix.


Also test.test_struct.StructTest.test_count_overflow (CPython raises struct.error for huge repeat counts in struct.calcsize; Lucifer grew past 8 GB when run alone). Both are left out against this ticket.
