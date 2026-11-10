---
id: luc-hjnt
status: closed
deps: []
links: []
created: 2026-11-10T03:44:26Z
type: bug
priority: 2
---
# compiler: SyntaxError.text is an int for compile() errors, so tracebacks drop the source line

compile(' 1', '<console>', 'single') raises IndentationError with text == -1 (CPython: ' 1'), so traceback.format_exception_only omits the source line: test_code_module test_indentation_error and test_sysexcepthook_indentation_error FAIL (they passed in the 2026-11-09 full run, built from an older master). Present at f6a22ca already; found in luc-nvfb.


## Notes

**2026-11-10T04:54:27Z**

Root cause: e1c454f (exception inits use layout words) put text at word 4; PySyntaxErrorObject has end_lineno and end_offset before text. Fixed on luc-crash with tests/kernel/syntax_error_fields; test_code_module 17/18, test_exceptions 103/111, test_syntax 47/48.
