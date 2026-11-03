"""_types (Modules/_typesmodule.c): names for built-in types that are not
builtins. The static types come from the runtime; the type of a method
wrapper is the type of one."""

from _lucifer import static_type as _static_type
import sys as _sys

AsyncGeneratorType = _static_type('async_generator')
BuiltinFunctionType = BuiltinMethodType = _static_type('builtin_function_or_method')
CapsuleType = _static_type('PyCapsule')
CellType = _static_type('cell')
ClassMethodDescriptorType = _static_type('classmethod_descriptor')
CodeType = _static_type('code')
CoroutineType = _static_type('coroutine')
EllipsisType = _static_type('ellipsis')
FrameType = _static_type('frame')
FunctionType = LambdaType = _static_type('function')
GeneratorType = _static_type('generator')
GenericAlias = _static_type('types.GenericAlias')
GetSetDescriptorType = _static_type('getset_descriptor')
MappingProxyType = _static_type('mappingproxy')
MemberDescriptorType = _static_type('member_descriptor')
MethodDescriptorType = _static_type('method_descriptor')
MethodType = _static_type('method')
MethodWrapperType = type(object().__str__)
ModuleType = _static_type('module')
NoneType = _static_type('NoneType')
NotImplementedType = _static_type('NotImplementedType')
SimpleNamespace = type(_sys.implementation)
TracebackType = _static_type('traceback')
UnionType = _static_type('typing.Union')
WrapperDescriptorType = _static_type('wrapper_descriptor')
