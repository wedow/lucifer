---
id: luc-lfnr
status: open
deps: []
links: []
created: 2026-11-19T07:51:51Z
type: task
priority: 2
tags: [jit, perf]
---
# str characters and one-character compares in line

Gap report 2026-11-18 mechanism #5, str part. s[i] on an exact str ran the kernel's str_item primitive through H_prim (PyUnicode_FromKindAndData per character) and s == 'x' (and !=, <, >=, either side) ran str_cmp through H_prim (an int box per compare): AWFY Json's and the kernel _json scanner's single-character work. str_item now reads the code point in line (STR_ORD) and returns the latin-1 singleton without a call (H_str_char's in-line path, any other code point through C); str_cmp against a constant one-character str is STR_CMP1 (first character and length, in line). Lane 'strs' (tests/kernel/jit_str_chars.py).

