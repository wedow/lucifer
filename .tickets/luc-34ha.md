---
id: luc-34ha
status: closed
deps: []
links: []
created: 2026-11-04T13:30:43Z
type: bug
priority: 2
parent: luc-h4gc
---
# Module-level functions of lib/modules ports are plain functions: they bind as methods

class A: m = operator.matmul; A().m(a, b) passes self as the first argument, because _operator.add/matmul and math.sqrt are Python functions; in CPython they are builtin_function_or_method and do not bind (also repr, types.BuiltinFunctionType, inspect.isbuiltin). exec_builtin could rewrap each module-level function with _lucifer.builtin_function, but then __text_signature__ must come from the function's code (inspect.signature(math.sqrt) works today). numpytest: TestMatmulOperator (9 rows).


## Notes

**2026-11-05T14:19:32Z**

port_exec (and start_module for _imp) rewraps every function a port leaves in its namespace as a builtin of the module (lu_kernel_builtin); the ad hoc rewraps in _codecs and _io go. Kernel builtins take __text_signature__ from their code (None when a default is not a literal) and are deferred like the top-level functions they wrap, so LOAD_ATTR_CLASS still caches them. _operator runs operator.py's definitions privately so operator takes its builtins in either import order. numpytest: TestMatmulOperator 9 FAIL -> 10/10 PASS; test_typing's 178 rows now pass the binding error and stop at the mypyc MRO bug noted on luc-wwqe.
