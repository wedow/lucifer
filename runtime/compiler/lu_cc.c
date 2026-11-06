/* The runtime's side of CPython's compiler: the internals the vendored files
   take from CPython's runtime, mapped to the runtime's own, and the entry
   point compile() calls. Not a CPython file; functions derived from one name
   it. Declared in include/lu_cc.h. */

#include "lu_cc.h"
#include "lucifer.h"
#include "interp/code.h"
#include "pycore_ast.h"
#include "pycore_compile.h"
#include "pycore_instruction_sequence.h"
#include "pycore_parser.h"
#include "pycore_pyarena.h"
#include "pegen.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

PyObject *lu_cc_strings[LU_CC_NSTRINGS];
struct _expr lu_cc_dummy_name;

/* codegen's infer_type reads only the name of this type. */
const PyTypeObject lu_cc_template_type = { PyVarObject_HEAD_INIT(NULL, 0) .tp_name = "string.templatelib.Template" };

/* lu_cc_init: the interned strings _Py_ID and _Py_STR name, and the parser's
   dummy name; once per interpreter start. */
int
lu_cc_init(void)
{
    static const char *const ids[] = {
#define X(n) #n,
        LU_CC_IDS(X)
#undef X
#define X(n, s) s,
        LU_CC_STRS(X)
#undef X
    };
    for (int i = 0; i < LU_CC_NSTRINGS; i++) {
        PyObject *s = PyUnicode_InternFromString(ids[i]);
        if (s == NULL) {
            return -1;
        }
        Py_XSETREF(lu_cc_strings[i], s);
    }
    lu_cc_dummy_name = (struct _expr){ .kind = Name_kind, .v.Name.id = &_Py_STR(empty), .v.Name.ctx = Load,
                                       .lineno = 1, .end_lineno = 1 };
    return 0;
}

LU_EXPORT const PyConfig *
_Py_GetConfig(void)
{
    return &((struct _is *)PyInterpreterState_Get())->config;
}

/* Objects/codeobject.c */
#include <math.h>
PyObject*
_PyCode_ConstantKey(PyObject *op)
{
    PyObject *key;

    /* Py_None and Py_Ellipsis are singletons. */
    if (op == Py_None || op == Py_Ellipsis
       || PyLong_CheckExact(op)
       || PyUnicode_CheckExact(op)
          /* code_richcompare() uses _PyCode_ConstantKey() internally */
       || PyCode_Check(op))
    {
        /* Objects of these types are always different from object of other
         * type and from tuples. */
        key = Py_NewRef(op);
    }
    else if (PyBool_Check(op) || PyBytes_CheckExact(op)) {
        /* Make booleans different from integers 0 and 1.
         * Avoid BytesWarning from comparing bytes with strings. */
        key = PyTuple_Pack(2, Py_TYPE(op), op);
    }
    else if (PyFloat_CheckExact(op)) {
        double d = PyFloat_AS_DOUBLE(op);
        /* all we need is to make the tuple different in either the 0.0
         * or -0.0 case from all others, just to avoid the "coercion".
         */
        if (d == 0.0 && copysign(1.0, d) < 0.0)
            key = PyTuple_Pack(3, Py_TYPE(op), op, Py_None);
        else
            key = PyTuple_Pack(2, Py_TYPE(op), op);
    }
    else if (PyComplex_CheckExact(op)) {
        Py_complex z;
        int real_negzero, imag_negzero;
        /* For the complex case we must make complex(x, 0.)
           different from complex(x, -0.) and complex(0., y)
           different from complex(-0., y), for any x and y.
           All four complex zeros must be distinguished.*/
        z = PyComplex_AsCComplex(op);
        real_negzero = z.real == 0.0 && copysign(1.0, z.real) < 0.0;
        imag_negzero = z.imag == 0.0 && copysign(1.0, z.imag) < 0.0;
        /* use True, False and None singleton as tags for the real and imag
         * sign, to make tuples different */
        if (real_negzero && imag_negzero) {
            key = PyTuple_Pack(3, Py_TYPE(op), op, Py_True);
        }
        else if (imag_negzero) {
            key = PyTuple_Pack(3, Py_TYPE(op), op, Py_False);
        }
        else if (real_negzero) {
            key = PyTuple_Pack(3, Py_TYPE(op), op, Py_None);
        }
        else {
            key = PyTuple_Pack(2, Py_TYPE(op), op);
        }
    }
    else if (PyTuple_CheckExact(op)) {
        Py_ssize_t i, len;
        PyObject *tuple;

        len = PyTuple_GET_SIZE(op);
        tuple = PyTuple_New(len);
        if (tuple == NULL)
            return NULL;

        for (i=0; i < len; i++) {
            PyObject *item, *item_key;

            item = PyTuple_GET_ITEM(op, i);
            item_key = _PyCode_ConstantKey(item);
            if (item_key == NULL) {
                Py_DECREF(tuple);
                return NULL;
            }

            PyTuple_SET_ITEM(tuple, i, item_key);
        }

        key = PyTuple_Pack(2, tuple, op);
        Py_DECREF(tuple);
    }
    else if (PyFrozenSet_CheckExact(op)) {
        Py_ssize_t pos = 0;
        PyObject *item;
        Py_hash_t hash;
        Py_ssize_t i, len;
        PyObject *tuple, *set;

        len = PySet_GET_SIZE(op);
        tuple = PyTuple_New(len);
        if (tuple == NULL)
            return NULL;

        i = 0;
        while (_PySet_NextEntry(op, &pos, &item, &hash)) {
            PyObject *item_key;

            item_key = _PyCode_ConstantKey(item);
            if (item_key == NULL) {
                Py_DECREF(tuple);
                return NULL;
            }

            assert(i < len);
            PyTuple_SET_ITEM(tuple, i, item_key);
            i++;
        }
        set = PyFrozenSet_New(tuple);
        Py_DECREF(tuple);
        if (set == NULL)
            return NULL;

        key = PyTuple_Pack(2, set, op);
        Py_DECREF(set);
        return key;
    }
    else if (PySlice_Check(op)) {
        PySliceObject *slice = (PySliceObject *)op;
        PyObject *start_key = NULL;
        PyObject *stop_key = NULL;
        PyObject *step_key = NULL;
        key = NULL;

        start_key = _PyCode_ConstantKey(slice->start);
        if (start_key == NULL) {
            goto slice_exit;
        }

        stop_key = _PyCode_ConstantKey(slice->stop);
        if (stop_key == NULL) {
            goto slice_exit;
        }

        step_key = _PyCode_ConstantKey(slice->step);
        if (step_key == NULL) {
            goto slice_exit;
        }

        PyObject *slice_key = PySlice_New(start_key, stop_key, step_key);
        if (slice_key == NULL) {
            goto slice_exit;
        }

        key = PyTuple_Pack(2, slice_key, op);
        Py_DECREF(slice_key);
    slice_exit:
        Py_XDECREF(start_key);
        Py_XDECREF(stop_key);
        Py_XDECREF(step_key);
    }
    else {
        /* for other types, use the object identifier as a unique identifier
         * to ensure that they are seen as unequal. */
        PyObject *obj_id = PyLong_FromVoidPtr(op);
        if (obj_id == NULL)
            return NULL;

        key = PyTuple_Pack(2, obj_id, op);
        Py_DECREF(obj_id);
    }
    return key;
}

/* _PyCompile_Get{Unary,Binary}IntrinsicName (Python/intrinsics.c): the names of the INTRINSIC_ opargs. */
PyObject *
_PyCompile_GetUnaryIntrinsicName(int index)
{
    static const char *const names[] = {
        "INTRINSIC_1_INVALID", "INTRINSIC_PRINT", "INTRINSIC_IMPORT_STAR", "INTRINSIC_STOPITERATION_ERROR",
        "INTRINSIC_ASYNC_GEN_WRAP", "INTRINSIC_UNARY_POSITIVE", "INTRINSIC_LIST_TO_TUPLE", "INTRINSIC_TYPEVAR",
        "INTRINSIC_PARAMSPEC", "INTRINSIC_TYPEVARTUPLE", "INTRINSIC_SUBSCRIPT_GENERIC", "INTRINSIC_TYPEALIAS",
    };
    return index < 0 || index >= (int)(sizeof(names) / sizeof(*names)) ? NULL : PyUnicode_FromString(names[index]);
}

PyObject *
_PyCompile_GetBinaryIntrinsicName(int index)
{
    static const char *const names[] = {
        "INTRINSIC_2_INVALID", "INTRINSIC_PREP_RERAISE_STAR", "INTRINSIC_TYPEVAR_WITH_BOUND",
        "INTRINSIC_TYPEVAR_WITH_CONSTRAINTS", "INTRINSIC_SET_FUNCTION_TYPE_PARAMS",
        "INTRINSIC_SET_TYPEPARAM_DEFAULT",
    };
    return index < 0 || index >= (int)(sizeof(names) / sizeof(*names)) ? NULL : PyUnicode_FromString(names[index]);
}

/* The parser and the AST walkers call this at every rule: whether this thread's
   C stack is within `margin` margins (32 KiB each, as CPython's) of its end. */
int
_Py_ReachedRecursionLimitWithMargin(PyThreadState *tstate, int margin_count)
{
    static __thread uintptr_t stack_floor;
    if (stack_floor == 0) {
        pthread_attr_t attr;
        void *base;
        size_t size;
        if (pthread_getattr_np(pthread_self(), &attr) != 0) {
            return 0;
        }
        pthread_attr_getstack(&attr, &base, &size);
        pthread_attr_destroy(&attr);
        stack_floor = (uintptr_t)base;
    }
    return (uintptr_t)__builtin_frame_address(0) < stack_floor + (uintptr_t)margin_count * 32768;
}

int
lu_cc_enter_recursive(const char *where)
{
    if (_Py_ReachedRecursionLimitWithMargin(NULL, 1)) {
        PyErr_Format(PyExc_RecursionError, "maximum recursion depth exceeded%s", where);
        return -1;
    }
    return 0;
}

/* The once flag (pycore_lock.h): done, or the slow path that runs fn once
   (runtime/thread/lock.c). */
int _PyOnceFlag_CallOnceSlow(_PyOnceFlag *flag, _Py_once_fn_t *fn, void *arg);

int
_PyOnceFlag_CallOnce(_PyOnceFlag *flag, _Py_once_fn_t *fn, void *arg)
{
    if (_Py_atomic_load_uint8(&flag->v) == 4) {     /* _Py_ONCE_INITIALIZED */
        return 0;
    }
    return _PyOnceFlag_CallOnceSlow(flag, fn, arg);
}

/* Objects/unicodeobject.c */
LU_EXPORT int
_PyUnicode_EqualToASCIIString(PyObject *unicode, const char *str)
{
    size_t len = strlen(str);
    return PyUnicode_IS_ASCII(unicode) && (size_t)PyUnicode_GET_LENGTH(unicode) == len
        && memcmp(PyUnicode_1BYTE_DATA(unicode), str, len) == 0;
}

LU_EXPORT int
_PyUnicode_Equal(PyObject *a, PyObject *b)
{
    return PyUnicode_Equal(a, b);
}

LU_EXPORT void
_PyUnicode_InternImmortal(PyInterpreterState *interp, PyObject **p)
{
    lu_intern(interp, p);
}

int _PyUnicode_IsXidStart(Py_UCS4);
int _PyUnicode_IsXidContinue(Py_UCS4);

LU_EXPORT Py_ssize_t
_PyUnicode_ScanIdentifier(PyObject *self)
{
    Py_ssize_t len = PyUnicode_GET_LENGTH(self);
    if (len == 0) {
        return 0;
    }
    int kind = PyUnicode_KIND(self);
    const void *data = PyUnicode_DATA(self);
    Py_UCS4 ch = PyUnicode_READ(kind, data, 0);
    if (!_PyUnicode_IsXidStart(ch) && ch != 0x5F) {
        return 0;
    }
    Py_ssize_t i;
    for (i = 1; i < len; i++) {
        if (!_PyUnicode_IsXidContinue(PyUnicode_READ(kind, data, i))) {
            return i;
        }
    }
    return i;
}

LU_EXPORT int
_PySet_NextEntry(PyObject *set, Py_ssize_t *pos, PyObject **key, Py_hash_t *hash)
{
    return _PySet_NextEntryRef(set, pos, key, hash) ? (Py_DECREF(*key), 1) : 0;
}

LU_EXPORT PyObject *
_Py_union_type_or(PyObject *a, PyObject *b)
{
    return PyNumber_Or(a, b);
}

PyObject *
_PyCode_GetFreevars(PyCodeObject *co)
{
    return PyObject_GetAttrString((PyObject *)co, "co_freevars");
}

/* Objects/codeobject.c */
void
_Py_set_localsplus_info(int offset, PyObject *name, _PyLocals_Kind kind, PyObject *names, PyObject *kinds)
{
    PyTuple_SET_ITEM(names, offset, Py_NewRef(name));
    PyBytes_AS_STRING(kinds)[offset] = (char)kind;
}

int
_PyCode_Validate(struct _PyCodeConstructor *con)
{
    return 0;
}

PyCodeObject *
_PyCode_New(struct _PyCodeConstructor *con)
{
    struct lu_code_fields f = {
        .filename = con->filename, .name = con->name, .qualname = con->qualname, .flags = con->flags,
        .code = con->code, .firstlineno = con->firstlineno, .linetable = con->linetable,
        .consts = con->consts, .names = con->names, .localsplusnames = con->localsplusnames,
        .localspluskinds = con->localspluskinds, .argcount = con->argcount,
        .posonlyargcount = con->posonlyargcount, .kwonlyargcount = con->kwonlyargcount,
        .stacksize = con->stacksize, .exceptiontable = con->exceptiontable,
    };
    return (PyCodeObject *)lu_code_new(&f);
}

/* Python/errors.c */
void
_PyErr_RaiseSyntaxError(PyObject *msg, PyObject *filename, int lineno, int col_offset,
                        int end_lineno, int end_col_offset)
{
    PyObject *text = _PyErr_ProgramDecodedTextObject(filename, lineno, NULL);
    if (text == NULL) {
        text = Py_NewRef(Py_None);
    }
    PyObject *args = Py_BuildValue("O(OiiOii)", msg, filename, lineno, col_offset, text,
                                   end_lineno, end_col_offset);
    if (args != NULL) {
        PyErr_SetObject(PyExc_SyntaxError, args);
    }
    Py_DECREF(text);
    Py_XDECREF(args);
}

int
_PyErr_EmitSyntaxWarning(PyObject *msg, PyObject *filename, int lineno, int col_offset,
                         int end_lineno, int end_col_offset)
{
    if (PyErr_WarnExplicitObject(PyExc_SyntaxWarning, msg, filename, lineno, NULL, NULL) < 0) {
        if (PyErr_ExceptionMatches(PyExc_SyntaxWarning)) {
            PyErr_Clear();
            _PyErr_RaiseSyntaxError(msg, filename, lineno, col_offset, end_lineno, end_col_offset);
        }
        return -1;
    }
    return 0;
}

/* Python/fileutils.c, Objects/fileobject.c */
LU_EXPORT int
_Py_dup(int fd)
{
    fd = dup(fd);
    if (fd < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
    }
    return fd;
}

LU_EXPORT char *
_Py_UniversalNewlineFgetsWithSize(char *buf, int n, FILE *stream, PyObject *fobj, size_t *size)
{
    char *p = buf;
    int c;

    if (fobj) {
        errno = ENXIO;
        return NULL;
    }
    flockfile(stream);
    while (--n > 0 && (c = getc_unlocked(stream)) != EOF) {
        if (c == '\r') {
            c = getc_unlocked(stream);
            if (c != '\n') {
                ungetc(c, stream);
                c = '\n';
            }
        }
        *p++ = c;
        if (c == '\n') {
            break;
        }
    }
    funlockfile(stream);
    *p = '\0';
    if (p == buf) {
        return NULL;
    }
    *size = p - buf;
    return buf;
}

/* Python/errors.c: the line of text an error refers to, read from the file,
   or NULL without an exception. */
extern char *_PyTokenizer_FindEncodingFilename(int, PyObject *);

static PyObject *
err_programtext(FILE *fp, int lineno, const char *encoding)
{
    char linebuf[1000];
    size_t line_size = 0;

    for (int i = 0; i < lineno;) {
        line_size = 0;
        if (_Py_UniversalNewlineFgetsWithSize(linebuf, sizeof(linebuf), fp, NULL, &line_size) == NULL) {
            return NULL;
        }
        if (i + 1 < lineno && line_size == sizeof(linebuf) - 1 && linebuf[sizeof(linebuf) - 2] != '\n') {
            continue;
        }
        i++;
    }
    const char *line = linebuf;
    if (lineno == 1 && line_size >= 3 && memcmp(line, "\xef\xbb\xbf", 3) == 0) {
        line += 3;
        line_size -= 3;
    }
    PyObject *res = PyUnicode_Decode(line, line_size, encoding, "replace");
    if (res == NULL) {
        PyErr_Clear();
    }
    return res;
}

LU_EXPORT PyObject *
_PyErr_ProgramDecodedTextObject(PyObject *filename, int lineno, const char *encoding)
{
    char *found_encoding = NULL;
    if (filename == NULL || lineno <= 0) {
        return NULL;
    }
    PyObject *path = PyUnicode_EncodeFSDefault(filename);
    if (path == NULL) {
        PyErr_Clear();
        return NULL;
    }
    FILE *fp = fopen(PyBytes_AS_STRING(path), "r");
    Py_DECREF(path);
    if (fp == NULL) {
        return NULL;
    }
    if (encoding == NULL) {
        int fd = fileno(fp);
        found_encoding = _PyTokenizer_FindEncodingFilename(fd, filename);
        encoding = found_encoding;
        if (encoding == NULL) {
            PyErr_Clear();
            encoding = "utf-8";
        }
        if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
            fclose(fp);
            PyMem_Free(found_encoding);
            return NULL;
        }
    }
    PyObject *res = err_programtext(fp, lineno, encoding);
    fclose(fp);
    PyMem_Free(found_encoding);
    return res;
}

static int
hexval(char c)
{
    return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
}

/* Objects/bytesobject.c, for strict errors only (the only kind the parser asks for). */
LU_EXPORT PyObject *
_PyBytes_DecodeEscape2(const char *s, Py_ssize_t len, const char *errors,
                       int *first_invalid_escape_char, const char **first_invalid_escape_ptr)
{
    char *buf = PyMem_Malloc(len + 1);
    if (buf == NULL) {
        return PyErr_NoMemory();
    }
    char *p = buf;
    const char *end = s + len;
    *first_invalid_escape_char = -1;
    *first_invalid_escape_ptr = NULL;
    while (s < end) {
        if (*s != '\\') {
            *p++ = *s++;
            continue;
        }
        s++;
        if (s == end) {
            PyErr_SetString(PyExc_ValueError, "Trailing \\ in string");
            goto failed;
        }
        int c;
        switch (*s++) {
        case '\n': break;
        case '\\': *p++ = '\\'; break;
        case '\'': *p++ = '\''; break;
        case '\"': *p++ = '\"'; break;
        case 'b': *p++ = '\b'; break;
        case 'f': *p++ = '\014'; break;
        case 't': *p++ = '\t'; break;
        case 'n': *p++ = '\n'; break;
        case 'r': *p++ = '\r'; break;
        case 'v': *p++ = '\013'; break;
        case 'a': *p++ = '\007'; break;
        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7':
            c = s[-1] - '0';
            if (s < end && '0' <= *s && *s <= '7') {
                c = (c << 3) + *s++ - '0';
                if (s < end && '0' <= *s && *s <= '7') {
                    c = (c << 3) + *s++ - '0';
                }
            }
            if (c > 0377 && *first_invalid_escape_char == -1) {
                *first_invalid_escape_char = c;
                *first_invalid_escape_ptr = s - 3;
            }
            *p++ = c;
            break;
        case 'x':
            if (s + 1 < end && Py_ISXDIGIT(s[0]) && Py_ISXDIGIT(s[1])) {
                *p++ = (char)((hexval(s[0]) << 4) + hexval(s[1]));
                s += 2;
                break;
            }
            PyErr_Format(PyExc_ValueError, "invalid \\x escape at position %zd", s - 2 - (end - len));
            goto failed;
        default:
            if (*first_invalid_escape_char == -1) {
                *first_invalid_escape_char = (unsigned char)s[-1];
                *first_invalid_escape_ptr = s - 1;
            }
            *p++ = '\\';
            s--;
        }
    }
    PyObject *result = PyBytes_FromStringAndSize(buf, p - buf);
    PyMem_Free(buf);
    return result;
failed:
    PyMem_Free(buf);
    return NULL;
}

int lu_ucd_getcode(const char *name, int namelen, Py_UCS4 *code);

/* Objects/unicodeobject.c, for strict errors only. */
LU_EXPORT PyObject *
_PyUnicode_DecodeUnicodeEscapeInternal2(const char *s, Py_ssize_t size, const char *errors,
                                        Py_ssize_t *consumed, int *first_invalid_escape_char,
                                        const char **first_invalid_escape_ptr)
{
    const char *starts = s, *end = s + size;
    Py_UCS4 *buf = PyMem_Malloc((size + 1) * sizeof(Py_UCS4));
    Py_ssize_t n = 0;
    if (buf == NULL) {
        return PyErr_NoMemory();
    }
    *first_invalid_escape_char = -1;
    *first_invalid_escape_ptr = NULL;
    while (s < end) {
        unsigned char c = (unsigned char)*s++;
        Py_UCS4 ch;
        int count;
        const char *message;
        if (c != '\\') {
            buf[n++] = c;
            continue;
        }
        Py_ssize_t startinpos = s - starts - 1;
        if (s >= end) {
            message = "\\ at end of string";
            goto error;
        }
        c = (unsigned char)*s++;
        switch (c) {
        case '\n': continue;
        case '\\': buf[n++] = '\\'; continue;
        case '\'': buf[n++] = '\''; continue;
        case '\"': buf[n++] = '\"'; continue;
        case 'b': buf[n++] = '\b'; continue;
        case 'f': buf[n++] = '\014'; continue;
        case 't': buf[n++] = '\t'; continue;
        case 'n': buf[n++] = '\n'; continue;
        case 'r': buf[n++] = '\r'; continue;
        case 'v': buf[n++] = '\013'; continue;
        case 'a': buf[n++] = '\007'; continue;
        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7':
            ch = c - '0';
            if (s < end && '0' <= *s && *s <= '7') {
                ch = (ch << 3) + *s++ - '0';
                if (s < end && '0' <= *s && *s <= '7') {
                    ch = (ch << 3) + *s++ - '0';
                }
            }
            if (ch > 0377 && *first_invalid_escape_char == -1) {
                *first_invalid_escape_char = ch;
                *first_invalid_escape_ptr = s - 3;
            }
            buf[n++] = ch;
            continue;
        case 'x': count = 2; message = "truncated \\xXX escape"; goto hexescape;
        case 'u': count = 4; message = "truncated \\uXXXX escape"; goto hexescape;
        case 'U': count = 8; message = "truncated \\UXXXXXXXX escape";
        hexescape:
            for (ch = 0; count; ++s, --count) {
                if (s >= end) {
                    goto error;
                }
                c = (unsigned char)*s;
                ch <<= 4;
                if (c >= '0' && c <= '9') {
                    ch += c - '0';
                }
                else if (c >= 'a' && c <= 'f') {
                    ch += c - ('a' - 10);
                }
                else if (c >= 'A' && c <= 'F') {
                    ch += c - ('A' - 10);
                }
                else {
                    goto error;
                }
            }
            if (ch > 0x10ffff) {
                message = "illegal Unicode character";
                goto error;
            }
            buf[n++] = ch;
            continue;
        case 'N':
            message = "malformed \\N character escape";
            if (s >= end) {
                goto error;
            }
            if (*s == '{') {
                const char *start = ++s;
                while (s < end && *s != '}') {
                    s++;
                }
                if (s >= end) {
                    goto error;
                }
                size_t namelen = s - start;
                if (namelen) {
                    s++;
                    if (namelen <= INT_MAX && lu_ucd_getcode(start, (int)namelen, &ch)) {
                        buf[n++] = ch;
                        continue;
                    }
                    message = "unknown Unicode character name";
                }
            }
            goto error;
        default:
            if (*first_invalid_escape_char == -1) {
                *first_invalid_escape_char = c;
                *first_invalid_escape_ptr = s - 1;
            }
            buf[n++] = '\\';
            buf[n++] = c;
            continue;
        }
    error:;
        PyObject *exc = PyUnicodeDecodeError_Create("unicodeescape", starts, size, startinpos, s - starts, message);
        if (exc != NULL) {
            PyErr_SetObject(PyExc_UnicodeDecodeError, exc);
            Py_DECREF(exc);
        }
        PyMem_Free(buf);
        return NULL;
    }
    PyObject *result = PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND, buf, n);
    PyMem_Free(buf);
    return result;
}

/* Python/pythonrun.c */
const char *
_Py_SourceAsString(PyObject *cmd, const char *funcname, const char *what, PyCompilerFlags *cf,
                   PyObject **cmd_copy)
{
    const char *str;
    Py_ssize_t size;
    *cmd_copy = NULL;
    if (PyUnicode_Check(cmd)) {
        cf->cf_flags |= PyCF_IGNORE_COOKIE;
        str = PyUnicode_AsUTF8AndSize(cmd, &size);
        if (str == NULL) {
            return NULL;
        }
    }
    else if (PyBytes_Check(cmd)) {
        str = PyBytes_AS_STRING(cmd);
        size = PyBytes_GET_SIZE(cmd);
    }
    else {
        PyErr_Format(PyExc_TypeError, "%s() arg 1 must be a %s object", funcname, what);
        return NULL;
    }
    if (strlen(str) != (size_t)size) {
        PyErr_SetString(PyExc_SyntaxError, "source code string cannot contain null bytes");
        return NULL;
    }
    return str;
}

/* Python/bltinmodule.c builtin_compile_impl, after the arguments are checked
   and the caller's future flags merged (the kernel's compile does both):
   source is a str, a bytes or an AST object. Unlike Py_CompileStringObject,
   the parser does not audit; the kernel already has. */
static int
ready_types(void *arg)
{
    return PyType_Ready(&_PyInstructionSequence_Type);
}

PyObject *
lu_cc_compile(PyObject *source, PyObject *filename, const char *mode, int flags, int optimize,
              int feature_version)
{
    static const int start[] = { Py_file_input, Py_eval_input, Py_single_input, Py_func_type_input };
    int compile_mode = strcmp(mode, "exec") == 0 ? 0 : strcmp(mode, "eval") == 0 ? 1
                     : strcmp(mode, "single") == 0 ? 2 : 3;
    PyCompilerFlags cf = _PyCompilerFlags_INIT;
    cf.cf_flags = flags | PyCF_SOURCE_IS_UTF8;
    if (feature_version >= 0 && (flags & PyCF_ONLY_AST)) {
        cf.cf_feature_version = feature_version;
    }
    static _PyOnceFlag types_ready;
    if (_PyOnceFlag_CallOnce(&types_ready, ready_types, NULL) < 0) {
        return NULL;
    }
    PyArena *arena = _PyArena_New();
    if (arena == NULL) {
        return NULL;
    }
    PyObject *result = NULL;
    int is_ast = PyUnicode_Check(source) || PyBytes_Check(source) ? 0 : PyAST_Check(source);
    mod_ty mod;
    if (is_ast < 0) {
        goto done;
    }
    if (is_ast) {
        mod = PyAST_obj2mod(source, arena, compile_mode);
        if (mod == NULL || !_PyAST_Validate(mod)) {
            goto done;
        }
    }
    else {
        PyObject *copy;
        const char *str = _Py_SourceAsString(source, "compile", "string, bytes or AST", &cf, &copy);
        if (str == NULL) {
            goto done;
        }
        mod = _PyPegen_run_parser_from_string(str, start[compile_mode], filename, &cf, arena);
        Py_XDECREF(copy);
        if (mod == NULL) {
            goto done;
        }
    }
    if (flags & PyCF_ONLY_AST) {
        int syntax_check_only = ((flags & PyCF_OPTIMIZED_AST) == PyCF_ONLY_AST);
        if (_PyCompile_AstPreprocess(mod, filename, &cf, optimize, arena, syntax_check_only) == 0) {
            result = PyAST_mod2obj(mod);
        }
    }
    else {
        result = (PyObject *)_PyAST_Compile(mod, filename, &cf, optimize, arena);
    }
done:
    _PyArena_Free(arena);
    return result;
}

/* compile(source, filename, mode, flags, optimize, feature_version): lu_cc_compile. */
static PyObject *
prim_compile(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long flags, optimize, feature_version;
    const char *mode = nargs == 6 && PyUnicode_Check(args[2]) ? PyUnicode_AsUTF8(args[2]) : NULL;
    if (mode == NULL || !PyLong_CheckExact(args[3]) || !PyLong_CheckExact(args[4])
        || !PyLong_CheckExact(args[5])) {
        return PyErr_Occurred() ? NULL : (lu_err_format(PyExc_TypeError, "compile(source, filename, mode, flags, optimize, feature_version)"), NULL);
    }
    flags = PyLong_AsLong(args[3]);
    optimize = PyLong_AsLong(args[4]);
    feature_version = PyLong_AsLong(args[5]);
    if (PyErr_Occurred()) {
        return NULL;
    }
    return lu_cc_compile(args[0], args[1], mode, (int)flags, (int)optimize, (int)feature_version);
}

PyMethodDef lu_prim_compiler_methods[] = {
    { "compile", (PyCFunction)(void (*)(void))prim_compile, METH_FASTCALL, NULL },
    { NULL, NULL, 0, NULL },
};
