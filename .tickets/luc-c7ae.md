---
id: luc-c7ae
status: open
deps: []
links: []
created: 2026-11-10T01:54:16Z
type: task
priority: 2
assignee: Greg Wedow
tags: [conformance, glm]
---
# difftest: harvest python-formalization's case generators as Lucifer fuzzers

github.com/Broyojo/python-formalization (Apache-2.0) models CPython 3.15.2 in Lean and differential-tests each area with 52 Python case generators (difftest/*.py: longobj, longobj_str, binary64 float formatting, complex, decimal, datetime/zoneinfo, dictset + set hooks, listsort, json, pickle, _pyio buffered/state/callback, itertools, ipaddress, fractions, pygrammar, bytecode frames, ...), each run to 50k+ agreeing cases. Fetch them pinned by commit (runtime fetch, as the corpus does; nothing committed but a lock entry), replace the Lean side with Lucifer, and diff against CPython 3.14t as oracle. Skip cases whose behaviour changed between 3.14 and 3.15 (check against 3.14t itself: oracle and model disagree -> skip). Run each generator at its default case count on several seeds; batch mismatches by cause into tickets; add a periodic lane (weekly with libtest) once stable. The Lean model and its 418 CPython bug records are not used: Lucifer matches CPython 3.14t behaviour, bugs included.


## Notes

**2026-11-11T04:20:24Z**

Results: `make fuzzdiff` (tools/fuzzdiff), Lucifer against CPython 3.14t, seeds 1 and 2 at 1,000 cases each (mathint_diff and rangeslice_diff at 500 each; mathint_diff with a 30 s process bound, since a few math.perm/comb cases hang Lucifer).
"Skipped" are cases that need 3.15 (frozendict, json array_hook, pickletools.get_theme); json_diff also dropped about 650 generator draws per seed that build frozendicts. "Gen/oracle errors" are generator exceptions on the oracle and oracle crashes (CPython itself segfaults on a few dictset/OrderedDict cases).

| Generator | Cases | Match | Mismatch | Skipped (3.15) | Gen/oracle errors |
| --- | ---: | ---: | ---: | ---: | ---: |
| accel_diff | 2,000 | 1,999 | 1 | 0 | 0 |
| binary64_diff | 2,000 | 1,982 | 18 | 0 | 0 |
| binary64_complex_diff | 2,000 | 2,000 | 0 | 0 | 0 |
| bytecode_diff | 2,000 | 1,762 | 238 | 0 | 0 |
| bytecode_frame_diff | 2,000 | 894 | 1,106 | 0 | 0 |
| bytecode_gen_diff | 2,000 | 1,434 | 566 | 0 | 0 |
| bytesobj_diff | 2,000 | 983 | 1,017 | 0 | 47 |
| corelibs_diff | 2,000 | 1,842 | 128 | 11 | 19 |
| datetime_diff | 2,000 | 1,777 | 223 | 0 | 0 |
| datetime_zoneinfo_diff | 2,000 | 1,664 | 336 | 0 | 0 |
| decimal_diff | 2,000 | 1,994 | 0 | 6 | 0 |
| dictset_diff | 2,000 | 1,233 | 587 | 173 | 7 |
| dictset_sethooks_diff | 2,000 | 1,508 | 492 | 0 | 0 |
| fractions_diff | 2,000 | 2,000 | 0 | 0 | 0 |
| ipaddress_diff | 2,000 | 2,000 | 0 | 0 | 0 |
| itertools_diff | 2,000 | 1,675 | 325 | 0 | 0 |
| json_diff | 2,000 | 643 | 323 | 1,034 | 0 |
| listsort_diff | 2,000 | 2,000 | 0 | 0 | 0 |
| longobj_diff | 2,000 | 1,907 | 93 | 0 | 0 |
| longobj_complex_diff | 2,000 | 1,920 | 80 | 0 | 0 |
| longobj_str_diff | 2,000 | 1,973 | 27 | 0 | 0 |
| mathint_diff | 1,000 | 971 | 29 | 0 | 0 |
| mathint_float_diff | 2,000 | 1,954 | 46 | 0 | 0 |
| pickle_diff | 2,000 | 1,817 | 8 | 175 | 0 |
| pygrammar_peg | 2,000 | 2,000 | 0 | 0 | 0 |
| pygrammar_lexer | 2,000 | 2,000 | 0 | 0 | 0 |
| pygrammar_check | 2,000 | 2,000 | 0 | 0 | 0 |
| pygrammar_unparse | 2,000 | 2,000 | 0 | 0 | 0 |
| pyio_base_diff | 2,000 | 1,455 | 545 | 0 | 0 |
| pyio_buffered_diff | 2,000 | 1,126 | 870 | 0 | 4 |
| pyio_callback_diff | 2,000 | 1,281 | 719 | 0 | 0 |
| pyio_diff | 2,000 | 1,681 | 319 | 0 | 0 |
| pyio_share_diff | 2,000 | 0 | 2,000 | 0 | 0 |
| pyio_state_diff | 2,000 | 66 | 1,934 | 0 | 0 |
| pyio_text_diff | 2,000 | 1,687 | 313 | 0 | 0 |
| rangeslice_diff | 1,000 | 816 | 184 | 0 | 0 |
| sequences_diff | 2,000 | 912 | 1,033 | 55 | 6 |
| strsearch_diff | 2,000 | 1,852 | 148 | 0 | 0 |
| textlibs_diff | 2,000 | 1,843 | 78 | 50 | 51 |
| typeobj_abstract_diff | 2,000 | 763 | 1,237 | 0 | 0 |
| typeobj_diff | 2,000 | 1,019 | 965 | 2 | 14 |
| typeobj_exc_diff | 2,000 | 1,186 | 814 | 0 | 0 |
| typeobj_structseq_diff | 2,000 | 0 | 1,984 | 0 | 16 |
| unicodeobj_diff | 2,000 | 0 | 1,997 | 3 | 0 |
| **total** | **86,000** | **63,619** | **20,783** | **1,509** | **164** |

Whole generators blocked on Lucifer: typeobj_structseq_diff (missing os.times_result and three other structseq types), unicodeobj_diff (no unicodedata._ucnhash_CAPI), pyio_share_diff (every case ends in BytesIO.__sizeof__). None is a small fix.
Crashes found: throw() re-entry into a generator (segfault), memoryview over a PIL buffer with a NULL entry (segfault), zoneinfo with a huge TZif count, and hangs in math.perm/comb, range containment and str.expandtabs with huge arguments.

Mismatches are filed by root cause with reproducers, tags conformance and fuzzdiff (`tk query 'select(.tags | index("fuzzdiff"))'`).
