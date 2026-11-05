---
id: luc-34ha
status: open
deps: []
links: []
created: 2026-11-04T13:30:43Z
type: bug
priority: 2
parent: luc-h4gc
---
# Module-level functions of lib/modules ports are plain functions: they bind as methods

class A: m = operator.matmul; A().m(a, b) passes self as the first argument, because _operator.add/matmul and math.sqrt are Python functions; in CPython they are builtin_function_or_method and do not bind (also repr, types.BuiltinFunctionType, inspect.isbuiltin). exec_builtin could rewrap each module-level function with _lucifer.builtin_function, but then __text_signature__ must come from the function's code (inspect.signature(math.sqrt) works today). numpytest: TestMatmulOperator (9 rows).

