/* Where CPython's compiler meets the runtime. The vendored files include
   CPython's internal headers; those named pycore_*.h here that are one line
   long come to this header, which declares what the compiler takes from
   CPython's internals and maps it to the runtime (runtime/compiler/lu_cc.c).
   Not a CPython file. */
#ifndef LU_CC_H
#define LU_CC_H

#include "Python.h"
#include <stdbool.h>
#include <stdint.h>

/* The interned strings the compiler names (_Py_ID, _Py_STR): lu_cc_strings,
   filled by lu_cc_init. */
#define LU_CC_IDS(X) \
    X(__annotate__) X(__annotate_func__) X(__annotations__) X(__class__) X(__classcell__) \
    X(__classdict__) X(__classdictcell__) X(__conditional_annotations__) X(dictcomp) \
    X(difference_update) X(__doc__) X(_field_types) X(__firstlineno__) X(format) X(genexpr) \
    X(join) X(lambda) X(listcomp) X(__module__) X(__name__) X(__qualname__) X(return) \
    X(setcomp) X(__static_attributes__) X(top) X(__type_params__) X(readline) X(filename) \
    X(__dict__) X(_attributes) X(_fields) X(__match_args__) X(__module) X(__eq__) \
    X(lineno) X(col_offset) X(end_lineno) X(end_col_offset) X(__reduce__) X(__dict) \
    X(__mro__) X(__replace__) X(__ast_end_location) X(_ast) X(__init_subclass__) \
    X(inf) X(__enter__) X(__exit__) X(__aenter__) X(__aexit__) X(__slotnames__) X(__new__) X(__getstate__) X(__setstate__) X(__ast_attributes__)
#define LU_CC_STRS(X) \
    X(anon_dictcomp, "<dictcomp>") X(anon_genexpr, "<genexpr>") X(anon_lambda, "<lambda>") \
    X(anon_listcomp, "<listcomp>") X(anon_module, "<module>") X(anon_setcomp, "<setcomp>") \
    X(dbl_percent, "%%") X(defaults, ".defaults") X(dot_locals, ".<locals>") X(empty, "") \
    X(format, ".format") X(generic_base, ".generic_base") X(kwdefaults, ".kwdefaults") \
    X(type_params, ".type_params") \
    X(dbl_open_br, "{{") X(dbl_close_br, "}}") X(str_replace_inf, "1e309")
enum {
#define X(n) LU_CC_ID_##n,
    LU_CC_IDS(X)
#undef X
#define X(n, s) LU_CC_STR_##n,
    LU_CC_STRS(X)
#undef X
    LU_CC_NSTRINGS
};
extern PyObject *lu_cc_strings[LU_CC_NSTRINGS];
#define _Py_ID(n) (*lu_cc_strings[LU_CC_ID_##n])
#define _Py_STR(n) (*lu_cc_strings[LU_CC_STR_##n])
#define _Py_DECLARE_STR(name, str)

/* pycore_pystate.h, pycore_runtime.h */
#define _PyThreadState_GET() PyThreadState_Get()
#define _PyInterpreterState_GET() PyInterpreterState_Get()
const PyConfig *_Py_GetConfig(void);

/* pycore_ceval.h */
/* The compiler's recursion is bounded by the C stack, as in CPython 3.14, not
   by the Python recursion limit that Py_EnterRecursiveCall charges. */
int lu_cc_enter_recursive(const char *where);
#undef Py_EnterRecursiveCall
#undef Py_LeaveRecursiveCall
#define Py_EnterRecursiveCall lu_cc_enter_recursive
#define Py_LeaveRecursiveCall() ((void)0)
#define _Py_EnterRecursiveCall lu_cc_enter_recursive
#define _Py_LeaveRecursiveCall() ((void)0)
#define SPECIAL___ENTER__   0
#define SPECIAL___EXIT__    1
#define SPECIAL___AENTER__  2
#define SPECIAL___AEXIT__   3
#define SPECIAL_MAX   3
#define ENABLE_SPECIALIZATION 0
#define ENABLE_SPECIALIZATION_FT 1

/* pycore_object.h */
#define _Py_ANNOTATE_FORMAT_VALUE_WITH_FAKE_GLOBALS 2

/* pycore_lock.h */
typedef int _Py_once_fn_t(void *arg);

/* pycore_long.h */
#define _PY_NSMALLNEGINTS 5
#define _PY_NSMALLPOSINTS 1025
#define _PY_IS_SMALL_INT(val) (-_PY_NSMALLNEGINTS <= (val) && (val) < _PY_NSMALLPOSINTS)
#define _PyLong_GetZero() ((PyObject *)Py_GetConstantBorrowed(Py_CONSTANT_ZERO))
#define _PyLong_GetOne() ((PyObject *)Py_GetConstantBorrowed(Py_CONSTANT_ONE))
#define _PyLong_IsZero(x) PyLong_IsZero((PyObject *)(x))
#define _PyLong_IsPositive(x) PyLong_IsPositive((PyObject *)(x))

/* pycore_call.h */
#define _PyObject_CallNoArgs PyObject_CallNoArgs

/* pycore_pymem.h */
#define _PyMem_IsPtrFreed(ptr) 0

/* pycore_global_strings.h: the single-character latin-1 strings */
#define _Py_LATIN1_CHR(CH) ((CH) < 128 ? PyUnicode_FromOrdinal(CH) : PyUnicode_FromOrdinal(CH))

/* pycore_unicodeobject.h */
int _PyUnicode_EqualToASCIIString(PyObject *left, const char *right);
Py_ssize_t _PyUnicode_ScanIdentifier(PyObject *self);
void _PyUnicode_InternImmortal(PyInterpreterState *interp, PyObject **p);
PyObject *_PyUnicode_DecodeUnicodeEscapeInternal2(const char *string, Py_ssize_t length,
    const char *errors, Py_ssize_t *consumed, int *first_invalid_escape_char,
    const char **first_invalid_escape_ptr);

/* pycore_bytesobject.h */
PyObject *_PyBytes_DecodeEscape2(const char *, Py_ssize_t, const char *, int *, const char **);

/* pycore_setobject.h */
int _PySet_NextEntryRef(PyObject *set, Py_ssize_t *pos, PyObject **key, Py_hash_t *hash);
int _PySet_NextEntry(PyObject *set, Py_ssize_t *pos, PyObject **key, Py_hash_t *hash);

/* pycore_pyerrors.h */
int _PyErr_EmitSyntaxWarning(PyObject *msg, PyObject *filename, int lineno, int col_offset,
                             int end_lineno, int end_col_offset);
PyObject *_PyErr_ProgramDecodedTextObject(PyObject *filename, int lineno, const char *encoding);
extern PyTypeObject _PyExc_IncompleteInputError;
#define PyExc_IncompleteInputError ((PyObject *)(&_PyExc_IncompleteInputError))

/* pycore_modsupport.h */
int _PyArg_NoKeywords(const char *funcname, PyObject *kwargs);
#define _PyArg_NoKeywords(funcname, kwargs) \
    ((kwargs) == NULL || _PyArg_NoKeywords((funcname), (kwargs)))
int _PyArg_CheckPositional(const char *, Py_ssize_t, Py_ssize_t, Py_ssize_t);
#define _Py_ANY_VARARGS(n) ((n) == PY_SSIZE_T_MAX)
#define _PyArg_CheckPositional(funcname, nargs, min, max) \
    ((!_Py_ANY_VARARGS(max) && (min) <= (nargs) && (nargs) <= (max)) \
     || _PyArg_CheckPositional((funcname), (nargs), (min), (max)))
void _PyArg_BadArgument(const char *, const char *, const char *, PyObject *);
PyObject *const *_PyArg_UnpackKeywords(PyObject *const *args, Py_ssize_t nargs, PyObject *kwargs,
    PyObject *kwnames, struct _PyArg_Parser *parser, int minpos, int maxpos, int minkw,
    int varpos, PyObject **buf);
#define _PyArg_UnpackKeywords(args, nargs, kwargs, kwnames, parser, minpos, maxpos, minkw, varpos, buf) \
    (((minkw) == 0 && (kwargs) == NULL && (kwnames) == NULL && \
      (minpos) <= (nargs) && ((varpos) || (nargs) <= (maxpos)) && (args) != NULL) ? \
      (args) : \
     _PyArg_UnpackKeywords((args), (nargs), (kwargs), (kwnames), (parser), \
                           (minpos), (maxpos), (minkw), (varpos), (buf)))
#define _Py_CRITICAL_SECTION_ASSERT_OBJECT_LOCKED(op)

/* pycore_pyhash.h */
static inline Py_hash_t
_Py_HashPointerRaw(const void *ptr)
{
    size_t y = (size_t)ptr;
    y = (y >> 4) | (y << (8 * sizeof(void *) - 4));
    return (Py_hash_t)y;
}

/* pycore_unionobject.h */
PyObject *_Py_union_type_or(PyObject *, PyObject *);
extern PyTypeObject _PyUnion_Type;
#define _PyUnion_Check(op) Py_IS_TYPE((op), &_PyUnion_Type)
int _PySet_Update(PyObject *set, PyObject *iterable);
int _PyArg_NoPositional(const char *funcname, PyObject *args);

/* pycore_lock.h: _PyOnceFlag is in the public headers; run under a mutex (lu_cc.c) */
int _PyOnceFlag_CallOnce(_PyOnceFlag *flag, _Py_once_fn_t *fn, void *arg);
void _PyErr_RaiseSyntaxError(PyObject *msg, PyObject *filename, int lineno, int col_offset,
                             int end_lineno, int end_col_offset);
int _PyUnicode_Equal(PyObject *, PyObject *);

/* pycore_template.h */
extern const PyTypeObject lu_cc_template_type;
#define _PyTemplate_Type (*(PyTypeObject *)&lu_cc_template_type)

/* pycore_fileutils.h */
char *_Py_UniversalNewlineFgetsWithSize(char *, int, FILE *, PyObject *, size_t *);
int _Py_dup(int fd);

/* pycore_pythonrun.h */
const char *_Py_SourceAsString(PyObject *cmd, const char *funcname, const char *what,
                               PyCompilerFlags *cf, PyObject **cmd_copy);

/* pycore_code.h */
typedef union {
    uint16_t cache;
    struct {
        uint8_t code;
        uint8_t arg;
    } op;
} _Py_CODEUNIT;
typedef unsigned char _PyLocals_Kind;
#define _PyCode_CODE(CO) ((_Py_CODEUNIT *)(CO)->co_code_adaptive)
struct _PyCodeConstructor {
    PyObject *filename, *name, *qualname;
    int flags;
    PyObject *code;
    int firstlineno;
    PyObject *linetable;
    PyObject *consts, *names;
    PyObject *localsplusnames;
    PyObject *localspluskinds;
    int argcount, posonlyargcount, kwonlyargcount;
    int stacksize;
    PyObject *exceptiontable;
};
int _PyCode_Validate(struct _PyCodeConstructor *);
PyCodeObject *_PyCode_New(struct _PyCodeConstructor *);
PyObject *_PyCode_ConstantKey(PyObject *op);
#define CO_FAST_ARG_POS (0x02)
#define CO_FAST_ARG_KW  (0x04)
#define CO_FAST_ARG_VAR (0x08)
#define CO_FAST_ARG     (CO_FAST_ARG_POS | CO_FAST_ARG_KW | CO_FAST_ARG_VAR)
PyObject *_PyCode_GetFreevars(PyCodeObject *);
void _Py_set_localsplus_info(int offset, PyObject *name, _PyLocals_Kind kind, PyObject *names, PyObject *kinds);
#define CO_FAST_HIDDEN  0x10
#define CO_FAST_LOCAL   0x20
#define CO_FAST_CELL    0x40
#define CO_FAST_FREE    0x80

static inline int
write_varint(uint8_t *ptr, unsigned int val)
{
    int written = 1;
    while (val >= 64) {
        *ptr++ = 64 | (val & 63);
        val >>= 6;
        written++;
    }
    *ptr = (uint8_t)val;
    return written;
}

static inline int
write_signed_varint(uint8_t *ptr, int val)
{
    unsigned int uval;
    if (val < 0) {
        uval = ((0 - (unsigned int)val) << 1) | 1;
    }
    else {
        uval = (unsigned int)val << 1;
    }
    return write_varint(ptr, uval);
}

static inline int
write_location_entry_start(uint8_t *ptr, int code, int length)
{
    *ptr = 128 | (uint8_t)(code << 3) | (uint8_t)(length - 1);
    return 1;
}
/* Comparison bit masks, as in pycore_code.h. */
#define COMPARISON_UNORDERED 1
#define COMPARISON_LESS_THAN 2
#define COMPARISON_GREATER_THAN 4
#define COMPARISON_EQUALS 8
#define COMPARISON_NOT_EQUALS (COMPARISON_UNORDERED | COMPARISON_LESS_THAN | COMPARISON_GREATER_THAN)

#endif
