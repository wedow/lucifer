/* The 215 data objects the cp314t ABI exports, statically initialized.

   Every object header comes from the canonical initializer macros, so each
   object is immortal and owned by no thread. Type objects carry only their
   names and fast-subclass flags here; the kernel fills in slots and dicts at
   startup. Data whose layout no public header defines is private: it is
   reserved at the oracle's size and defined when its code is ported. */

#include "lucifer.h"

/* CPython's internal _PyObject_HEAD_INIT, for a bare PyObject, which the
   public macro, with its trailing comma, cannot initialize. The GC bit is
   _PyGC_BITS_DEFERRED (deferred reference counting). */
#define LU_OBJECT_INIT(type) { \
    .ob_ref_local = _Py_IMMORTAL_REFCNT_LOCAL, \
    .ob_flags = _Py_STATICALLY_ALLOCATED_FLAG, \
    .ob_gc_bits = 1 << 6, \
    .ob_type = (type) }

/* Flags hold the fast-subclass bits, which inline checks such as
   PyLong_Check read, and Py_TPFLAGS_BASETYPE; values from the oracle's
   types. tp_base is the oracle's too; the runtime derives tp_bases and
   tp_mro from it at startup (runtime/prim/type.c). */
#define LU_TYPE(sym, name, flags, base) \
    LU_EXPORT PyTypeObject sym = { PyVarObject_HEAD_INIT(&PyType_Type, 0) \
        .tp_name = name, .tp_flags = flags, .tp_base = base };

#define LU_PRIVATE(sym, size) LU_EXPORT _Alignas(16) unsigned char sym[size];

/* Types (public and private); names from the oracle's tp_name. */
#define LU_STATIC_TYPES(X) \
    X(PyAsyncGen_Type, "async_generator", 0, &PyBaseObject_Type) \
    X(PyBaseObject_Type, "object", Py_TPFLAGS_BASETYPE, NULL) \
    X(PyBool_Type, "bool", Py_TPFLAGS_LONG_SUBCLASS, &PyLong_Type) \
    X(PyByteArrayIter_Type, "bytearray_iterator", 0, &PyBaseObject_Type) \
    X(PyByteArray_Type, "bytearray", Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PyBytesIter_Type, "bytes_iterator", 0, &PyBaseObject_Type) \
    X(PyBytes_Type, "bytes", Py_TPFLAGS_BYTES_SUBCLASS | Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PyCFunction_Type, "builtin_function_or_method", 0, &PyBaseObject_Type) \
    X(PyCMethod_Type, "builtin_method", 0, &PyCFunction_Type) \
    X(PyCallIter_Type, "callable_iterator", 0, &PyBaseObject_Type) \
    X(PyCapsule_Type, "PyCapsule", 0, &PyBaseObject_Type) \
    X(PyCell_Type, "cell", 0, &PyBaseObject_Type) \
    X(PyClassMethodDescr_Type, "classmethod_descriptor", 0, &PyBaseObject_Type) \
    X(PyClassMethod_Type, "classmethod", Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PyCode_Type, "code", 0, &PyBaseObject_Type) \
    X(PyComplex_Type, "complex", Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PyContextToken_Type, "_contextvars.Token", 0, &PyBaseObject_Type) \
    X(PyContextVar_Type, "_contextvars.ContextVar", 0, &PyBaseObject_Type) \
    X(PyContext_Type, "_contextvars.Context", 0, &PyBaseObject_Type) \
    /* datetime's C API bases: static, as C API types must be; the Python classes are rebased onto them */ \
    X(lu_date_base_type, "datetime._date_base", Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(lu_datetime_base_type, "datetime._datetime_base", Py_TPFLAGS_BASETYPE, &lu_date_base_type) \
    X(lu_time_base_type, "datetime._time_base", Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(lu_timedelta_base_type, "datetime._timedelta_base", Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(lu_tzinfo_base_type, "datetime._tzinfo_base", Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PyCoro_Type, "coroutine", 0, &PyBaseObject_Type) \
    X(PyDictItems_Type, "dict_items", 0, &PyBaseObject_Type) \
    X(PyDictIterItem_Type, "dict_itemiterator", 0, &PyBaseObject_Type) \
    X(PyDictIterKey_Type, "dict_keyiterator", 0, &PyBaseObject_Type) \
    X(PyDictIterValue_Type, "dict_valueiterator", 0, &PyBaseObject_Type) \
    X(PyDictKeys_Type, "dict_keys", 0, &PyBaseObject_Type) \
    X(PyDictProxy_Type, "mappingproxy", 0, &PyBaseObject_Type) \
    X(PyDictRevIterItem_Type, "dict_reverseitemiterator", 0, &PyBaseObject_Type) \
    X(PyDictRevIterKey_Type, "dict_reversekeyiterator", 0, &PyBaseObject_Type) \
    X(PyDictRevIterValue_Type, "dict_reversevalueiterator", 0, &PyBaseObject_Type) \
    X(PyDictValues_Type, "dict_values", 0, &PyBaseObject_Type) \
    X(PyDict_Type, "dict", Py_TPFLAGS_DICT_SUBCLASS | Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PyEllipsis_Type, "ellipsis", 0, &PyBaseObject_Type) \
    X(PyEnum_Type, "enumerate", Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PyFilter_Type, "filter", Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PyFloat_Type, "float", Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PyFrameLocalsProxy_Type, "FrameLocalsProxy", 0, &PyBaseObject_Type) \
    X(PyFrame_Type, "frame", 0, &PyBaseObject_Type) \
    X(PyFrozenSet_Type, "frozenset", Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PyFunction_Type, "function", 0, &PyBaseObject_Type) \
    X(PyGen_Type, "generator", 0, &PyBaseObject_Type) \
    X(PyGetSetDescr_Type, "getset_descriptor", 0, &PyBaseObject_Type) \
    X(PyInstanceMethod_Type, "instancemethod", 0, &PyBaseObject_Type) \
    X(PyListIter_Type, "list_iterator", 0, &PyBaseObject_Type) \
    X(PyListRevIter_Type, "list_reverseiterator", 0, &PyBaseObject_Type) \
    X(PyList_Type, "list", Py_TPFLAGS_LIST_SUBCLASS | Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PyLongRangeIter_Type, "longrange_iterator", 0, &PyBaseObject_Type) \
    X(PyLong_Type, "int", Py_TPFLAGS_LONG_SUBCLASS | Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PyMap_Type, "map", Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PyMemberDescr_Type, "member_descriptor", 0, &PyBaseObject_Type) \
    X(PyMemoryView_Type, "memoryview", 0, &PyBaseObject_Type) \
    X(PyMethodDescr_Type, "method_descriptor", 0, &PyBaseObject_Type) \
    X(PyMethod_Type, "method", 0, &PyBaseObject_Type) \
    X(PyModuleDef_Type, "moduledef", 0, &PyBaseObject_Type) \
    X(PyModule_Type, "module", Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PyODictItems_Type, "odict_items", 0, &PyDictItems_Type) \
    X(PyODictIter_Type, "odict_iterator", 0, &PyBaseObject_Type) \
    X(PyODictKeys_Type, "odict_keys", 0, &PyDictKeys_Type) \
    X(PyODictValues_Type, "odict_values", 0, &PyDictValues_Type) \
    X(PyODict_Type, "collections.OrderedDict", Py_TPFLAGS_DICT_SUBCLASS | Py_TPFLAGS_BASETYPE, &PyDict_Type) \
    X(PyPickleBuffer_Type, "pickle.PickleBuffer", 0, &PyBaseObject_Type) \
    X(PyProperty_Type, "property", Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PyRangeIter_Type, "range_iterator", 0, &PyBaseObject_Type) \
    X(PyRange_Type, "range", 0, &PyBaseObject_Type) \
    X(PyReversed_Type, "reversed", Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PySeqIter_Type, "iterator", 0, &PyBaseObject_Type) \
    X(PySetIter_Type, "set_iterator", 0, &PyBaseObject_Type) \
    X(PySet_Type, "set", Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PySlice_Type, "slice", 0, &PyBaseObject_Type) \
    X(PyStaticMethod_Type, "staticmethod", Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PyStdPrinter_Type, "stderrprinter", 0, &PyBaseObject_Type) \
    X(PySuper_Type, "super", Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PyTraceBack_Type, "traceback", 0, &PyBaseObject_Type) \
    X(PyTupleIter_Type, "tuple_iterator", 0, &PyBaseObject_Type) \
    X(PyTuple_Type, "tuple", Py_TPFLAGS_TUPLE_SUBCLASS | Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PyType_Type, "type", Py_TPFLAGS_TYPE_SUBCLASS | Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PyUnicodeIter_Type, "str_iterator", 0, &PyBaseObject_Type) \
    X(PyUnicode_Type, "str", Py_TPFLAGS_UNICODE_SUBCLASS | Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(PyWrapperDescr_Type, "wrapper_descriptor", 0, &PyBaseObject_Type) \
    X(PyZip_Type, "zip", Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(Py_GenericAliasType, "types.GenericAlias", Py_TPFLAGS_BASETYPE, &PyBaseObject_Type) \
    X(_PyAsyncGenASend_Type, "async_generator_asend", 0, &PyBaseObject_Type) \
    X(_PyExc_IncompleteInputError, "_IncompleteInputError", Py_TPFLAGS_BASE_EXC_SUBCLASS | Py_TPFLAGS_BASETYPE, &lu_exc_SyntaxError) \
    X(_PyNone_Type, "NoneType", 0, &PyBaseObject_Type) \
    X(_PyNotImplemented_Type, "NotImplementedType", 0, &PyBaseObject_Type) \
    X(_PyUnion_Type, "typing.Union", 0, &PyBaseObject_Type) \
    X(_PyWeakref_CallableProxyType, "weakref.CallableProxyType", 0, &PyBaseObject_Type) \
    X(_PyWeakref_ProxyType, "weakref.ProxyType", 0, &PyBaseObject_Type) \
    X(_PyWeakref_RefType, "weakref.ReferenceType", Py_TPFLAGS_BASETYPE, &PyBaseObject_Type)

/* Exceptions: the exported symbols are pointers to unexported types. */
#define LU_EXCEPTIONS(X) \
    X(ArithmeticError, "ArithmeticError", &lu_exc_Exception) \
    X(AssertionError, "AssertionError", &lu_exc_Exception) \
    X(AttributeError, "AttributeError", &lu_exc_Exception) \
    X(BaseException, "BaseException", &PyBaseObject_Type) \
    X(BaseExceptionGroup, "BaseExceptionGroup", &lu_exc_BaseException) \
    X(BlockingIOError, "BlockingIOError", &lu_exc_OSError) \
    X(BrokenPipeError, "BrokenPipeError", &lu_exc_ConnectionError) \
    X(BufferError, "BufferError", &lu_exc_Exception) \
    X(BytesWarning, "BytesWarning", &lu_exc_Warning) \
    X(ChildProcessError, "ChildProcessError", &lu_exc_OSError) \
    X(ConnectionAbortedError, "ConnectionAbortedError", &lu_exc_ConnectionError) \
    X(ConnectionError, "ConnectionError", &lu_exc_OSError) \
    X(ConnectionRefusedError, "ConnectionRefusedError", &lu_exc_ConnectionError) \
    X(ConnectionResetError, "ConnectionResetError", &lu_exc_ConnectionError) \
    X(DeprecationWarning, "DeprecationWarning", &lu_exc_Warning) \
    X(EOFError, "EOFError", &lu_exc_Exception) \
    X(EncodingWarning, "EncodingWarning", &lu_exc_Warning) \
    X(Exception, "Exception", &lu_exc_BaseException) \
    X(FileExistsError, "FileExistsError", &lu_exc_OSError) \
    X(FileNotFoundError, "FileNotFoundError", &lu_exc_OSError) \
    X(FloatingPointError, "FloatingPointError", &lu_exc_ArithmeticError) \
    X(FutureWarning, "FutureWarning", &lu_exc_Warning) \
    X(GeneratorExit, "GeneratorExit", &lu_exc_BaseException) \
    X(ImportError, "ImportError", &lu_exc_Exception) \
    X(ImportWarning, "ImportWarning", &lu_exc_Warning) \
    X(IndentationError, "IndentationError", &lu_exc_SyntaxError) \
    X(IndexError, "IndexError", &lu_exc_LookupError) \
    X(InterpreterError, "concurrent.interpreters.InterpreterError", &lu_exc_Exception) \
    X(InterpreterNotFoundError, "concurrent.interpreters.InterpreterNotFoundError", &lu_exc_InterpreterError) \
    X(InterruptedError, "InterruptedError", &lu_exc_OSError) \
    X(IsADirectoryError, "IsADirectoryError", &lu_exc_OSError) \
    X(KeyError, "KeyError", &lu_exc_LookupError) \
    X(KeyboardInterrupt, "KeyboardInterrupt", &lu_exc_BaseException) \
    X(LookupError, "LookupError", &lu_exc_Exception) \
    X(MemoryError, "MemoryError", &lu_exc_Exception) \
    X(ModuleNotFoundError, "ModuleNotFoundError", &lu_exc_ImportError) \
    X(NameError, "NameError", &lu_exc_Exception) \
    X(NotADirectoryError, "NotADirectoryError", &lu_exc_OSError) \
    X(NotImplementedError, "NotImplementedError", &lu_exc_RuntimeError) \
    X(OSError, "OSError", &lu_exc_Exception) \
    X(OverflowError, "OverflowError", &lu_exc_ArithmeticError) \
    X(PendingDeprecationWarning, "PendingDeprecationWarning", &lu_exc_Warning) \
    X(PermissionError, "PermissionError", &lu_exc_OSError) \
    X(ProcessLookupError, "ProcessLookupError", &lu_exc_OSError) \
    X(PythonFinalizationError, "PythonFinalizationError", &lu_exc_RuntimeError) \
    X(RecursionError, "RecursionError", &lu_exc_RuntimeError) \
    X(ReferenceError, "ReferenceError", &lu_exc_Exception) \
    X(ResourceWarning, "ResourceWarning", &lu_exc_Warning) \
    X(RuntimeError, "RuntimeError", &lu_exc_Exception) \
    X(RuntimeWarning, "RuntimeWarning", &lu_exc_Warning) \
    X(StopAsyncIteration, "StopAsyncIteration", &lu_exc_Exception) \
    X(StopIteration, "StopIteration", &lu_exc_Exception) \
    X(SyntaxError, "SyntaxError", &lu_exc_Exception) \
    X(SyntaxWarning, "SyntaxWarning", &lu_exc_Warning) \
    X(SystemError, "SystemError", &lu_exc_Exception) \
    X(SystemExit, "SystemExit", &lu_exc_BaseException) \
    X(TabError, "TabError", &lu_exc_IndentationError) \
    X(TimeoutError, "TimeoutError", &lu_exc_OSError) \
    X(TypeError, "TypeError", &lu_exc_Exception) \
    X(UnboundLocalError, "UnboundLocalError", &lu_exc_NameError) \
    X(UnicodeDecodeError, "UnicodeDecodeError", &lu_exc_UnicodeError) \
    X(UnicodeEncodeError, "UnicodeEncodeError", &lu_exc_UnicodeError) \
    X(UnicodeError, "UnicodeError", &lu_exc_ValueError) \
    X(UnicodeTranslateError, "UnicodeTranslateError", &lu_exc_UnicodeError) \
    X(UnicodeWarning, "UnicodeWarning", &lu_exc_Warning) \
    X(UserWarning, "UserWarning", &lu_exc_Warning) \
    X(ValueError, "ValueError", &lu_exc_Exception) \
    X(Warning, "Warning", &lu_exc_Exception) \
    X(ZeroDivisionError, "ZeroDivisionError", &lu_exc_ArithmeticError)

#define LU_EXC_DECLARE(sym, name, base) static PyTypeObject lu_exc_##sym;
LU_EXCEPTIONS(LU_EXC_DECLARE)
LU_STATIC_TYPES(LU_TYPE)

#define LU_EXC(sym, name, base) \
    static PyTypeObject lu_exc_##sym = { PyVarObject_HEAD_INIT(&PyType_Type, 0) \
        .tp_name = name, .tp_flags = Py_TPFLAGS_BASE_EXC_SUBCLASS | Py_TPFLAGS_BASETYPE, \
        .tp_base = base }; \
    LU_EXPORT PyObject *PyExc_##sym = (PyObject *)&lu_exc_##sym;
LU_EXCEPTIONS(LU_EXC)

/* Every exception type, for the native layout setup (runtime/capi/errors.c). */
#define LU_EXC_ENTRY(sym, name, base) &lu_exc_##sym,
PyTypeObject *const lu_exception_types[] = { LU_EXCEPTIONS(LU_EXC_ENTRY) NULL };

/* method-wrapper (Objects/descrobject.c wrapperobject): a slot wrapper bound
   to an instance. CPython does not export it. */
PyTypeObject lu_method_wrapper_type = { PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "method-wrapper", .tp_base = &PyBaseObject_Type };
/* Defined with their code (runtime/interp/gen.c). */
extern PyTypeObject lu_coro_wrapper_type, lu_async_gen_athrow_type, lu_async_gen_wrapped_type;

/* Every static builtin type, in index order: its dict and subclasses live in
   each interpreter's static_types[index] (FOUNDATIONS 2.1). Object first,
   then type, so that both are ready before any other. */
#define LU_TYPE_ENTRY(sym, name, flags, base) &sym,
PyTypeObject *const lu_static_types[] = {
    &PyBaseObject_Type, &PyType_Type, LU_STATIC_TYPES(LU_TYPE_ENTRY)
    LU_EXCEPTIONS(LU_EXC_ENTRY) &lu_method_wrapper_type, &lu_coro_wrapper_type,
    &lu_async_gen_athrow_type, &lu_async_gen_wrapped_type, NULL };
PyObject *PyExc_EnvironmentError = (PyObject *)&lu_exc_OSError;
PyObject *PyExc_IOError = (PyObject *)&lu_exc_OSError;

/* Singletons. */
PyObject _Py_NoneStruct = LU_OBJECT_INIT(&_PyNone_Type);
PyObject _Py_NotImplementedStruct = LU_OBJECT_INIT(&_PyNotImplemented_Type);
PyObject _Py_EllipsisObject = LU_OBJECT_INIT(&PyEllipsis_Type);
/* lv_tag: sign and digit count, as TAG_FROM_SIGN_AND_SIZE in CPython. */
PyLongObject _Py_FalseStruct = { PyObject_HEAD_INIT(&PyBool_Type) { 1, { 0 } } };
PyLongObject _Py_TrueStruct = { PyObject_HEAD_INIT(&PyBool_Type) { 1 << _PyLong_NON_SIZE_BITS, { 1 } } };

/* Legacy configuration globals, zero until the runtime sets them. */
int Py_BytesWarningFlag;
int Py_DebugFlag;
int Py_DontWriteBytecodeFlag;
int Py_FrozenFlag;
int Py_HasFileSystemDefaultEncoding;
int Py_HashRandomizationFlag;
int Py_IgnoreEnvironmentFlag;
int Py_InspectFlag;
int Py_InteractiveFlag;
int Py_IsolatedFlag;
int Py_NoSiteFlag;
int Py_NoUserSiteDirectory;
int Py_OptimizeFlag;
int Py_QuietFlag;
int Py_UTF8Mode;
int Py_UnbufferedStdioFlag;
int Py_VerboseFlag;
const char *Py_FileSystemDefaultEncodeErrors;
const char *Py_FileSystemDefaultEncoding;

/* Hooks and tables extensions or embedders may set. */
static struct _inittab lu_inittab[] = { { NULL, NULL } };
struct _inittab *PyImport_Inittab = lu_inittab;
const struct _frozen *PyImport_FrozenModules;
int (*PyOS_InputHook)(void);
char *(*PyOS_ReadlineFunctionPointer)(FILE *, FILE *, const char *);

/* Constants. */
const unsigned long Py_Version = PY_VERSION_HEX;
const char *Py_hexdigits = "0123456789abcdef";
const char * const PyStructSequence_UnnamedField = "unnamed field";
const long long PY_TIMEOUT_MAX = LLONG_MAX / 1000;
char _PyByteArray_empty_string[1];
const PyTypeObject *const PyUnstable_ExecutableKinds[PyUnstable_EXECUTABLE_KINDS + 1] = {
    [PyUnstable_EXECUTABLE_KIND_SKIP] = &_PyNone_Type,
    [PyUnstable_EXECUTABLE_KIND_PY_FUNCTION] = &PyCode_Type,
    [PyUnstable_EXECUTABLE_KIND_BUILTIN_FUNCTION] = &PyMethod_Type,
    [PyUnstable_EXECUTABLE_KIND_METHOD_DESCRIPTOR] = &PyMethodDescr_Type,
};

/* Character classes, as Python/pyctype.c and Objects/unicodeobject.c. */
const unsigned int _Py_ctype_table[256] = {
    ['\t' ... '\r'] = PY_CTF_SPACE,
    [' '] = PY_CTF_SPACE,
    ['0' ... '9'] = PY_CTF_DIGIT | PY_CTF_XDIGIT,
    ['A' ... 'F'] = PY_CTF_UPPER | PY_CTF_XDIGIT,
    ['G' ... 'Z'] = PY_CTF_UPPER,
    ['a' ... 'f'] = PY_CTF_LOWER | PY_CTF_XDIGIT,
    ['g' ... 'z'] = PY_CTF_LOWER,
};
const unsigned char _Py_ascii_whitespace[128] = {
    [0x09 ... 0x0D] = 1,
    [0x1C ... 0x20] = 1,
};
const unsigned char _Py_ctype_tolower[256] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
    0x40, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
    0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
    0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
};
const unsigned char _Py_ctype_toupper[256] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
    0x60, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
    0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
    0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
};

/* The perf trampoline template (Python/asm_trampoline.S): the bytes between
   the two labels are copied into executable memory. */
__asm__(".section .rodata\n"
        ".globl _Py_trampoline_func_start\n"
        "_Py_trampoline_func_start:\n"
        "    push %rbp\n"
        "    mov %rsp, %rbp\n"
        "    call *%rcx\n"
        "    pop %rbp\n"
        "    ret\n"
        ".globl _Py_trampoline_func_end\n"
        "_Py_trampoline_func_end:\n"
        ".previous\n");

/* Private data, at the oracle's sizes. */
LU_PRIVATE(_PyEval_BinaryOps, 216)
LU_PRIVATE(_PyEval_ConversionFuncs, 32)
LU_PRIVATE(_PyImport_FrozenBootstrap, 8)
LU_PRIVATE(_PyImport_FrozenStdlib, 8)
LU_PRIVATE(_PyImport_FrozenTest, 8)
LU_PRIVATE(_PyIntrinsics_BinaryFunctions, 96)
LU_PRIVATE(_PyIntrinsics_UnaryFunctions, 192)
LU_PRIVATE(_PyLong_DigitValue, 256)
LU_PRIVATE(_PyOS_ReadlineTState, 8)
LU_PRIVATE(_PySet_Dummy, 8)
LU_PRIVATE(_Py_FunctionAttributeOffsets, 136)
LU_PRIVATE(_Py_InitCleanup, 240)
LU_PRIVATE(_Py_SpecialMethods, 96)
LU_PRIVATE(_Py_SwappedOp, 24)
