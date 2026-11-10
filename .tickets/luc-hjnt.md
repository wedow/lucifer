---
id: luc-hjnt
status: open
deps: []
links: []
created: 2026-11-10T03:44:26Z
type: bug
priority: 2
---
# compiler: SyntaxError.text is an int for compile() errors, so tracebacks drop the source line

compile(' 1', '<console>', 'single') raises IndentationError with text == -1 (CPython: ' 1'), so traceback.format_exception_only omits the source line: test_code_module test_indentation_error and test_sysexcepthook_indentation_error FAIL (they passed in the 2026-11-09 full run, built from an older master). Present at f6a22ca already; found in luc-nvfb.

