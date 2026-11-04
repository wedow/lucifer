/* The codec registry C API (Python/codecs.c) and the str codec entry points
   whose C signature carries codec state: the consumed count, the UTF byte
   order, a charmap. Lucifer's registry is the _codecs module (lib/modules/
   _codecs.py) - its search function list, cache and error handler table - so
   these entry points call it and the C API and the Python codecs module
   register, find and unregister the very same objects. */

#include "lucifer.h"

#include <stdarg.h>

/* _codecs.name(*args), the args built by a Py_BuildValue format. */
static PyObject *
call_codecs(const char *name, const char *format, ...)
{
    PyObject *module = PyImport_ImportModule("_codecs");
    if (module == NULL) {
        return NULL;
    }
    va_list ap;
    va_start(ap, format);
    PyObject *args = Py_VaBuildValue(format, ap);
    va_end(ap);
    PyObject *r = NULL;
    if (args != NULL) {
        r = PyObject_CallMethod(module, name, "N", args);
    }
    Py_DECREF(module);
    return r;
}

/* --- The registry ------------------------------------------------------- */

int
PyCodec_Register(PyObject *search_function)
{
    PyObject *r = call_codecs("register", "(O)", search_function);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

int
PyCodec_Unregister(PyObject *search_function)
{
    PyObject *r = call_codecs("unregister", "(O)", search_function);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

/* _codecs.lookup(encoding): the CodecInfo, or NULL with LookupError set. */
static PyObject *
codec_lookup(const char *encoding)
{
    if (encoding == NULL) {
        PyErr_BadArgument();
        return NULL;
    }
    return call_codecs("lookup", "(s)", encoding);
}

int
PyCodec_KnownEncoding(const char *encoding)
{
    PyObject *codec = codec_lookup(encoding);
    if (codec == NULL) {
        PyErr_Clear();
        return 0;
    }
    Py_DECREF(codec);
    return 1;
}

/* A facility of a looked-up codec: an item of its CodecInfo tuple. */
static PyObject *
codec_item(const char *encoding, int index)
{
    PyObject *codec = codec_lookup(encoding);
    if (codec == NULL) {
        return NULL;
    }
    PyObject *v = PySequence_GetItem(codec, index);
    Py_DECREF(codec);
    return v;
}

PyObject *
PyCodec_Encoder(const char *encoding)
{
    return codec_item(encoding, 0);
}

PyObject *
PyCodec_Decoder(const char *encoding)
{
    return codec_item(encoding, 1);
}

static PyObject *
codec_incremental(const char *encoding, const char *errors, const char *attrname)
{
    PyObject *codec = codec_lookup(encoding);
    if (codec == NULL) {
        return NULL;
    }
    PyObject *cls = PyObject_GetAttrString(codec, attrname);
    Py_DECREF(codec);
    if (cls == NULL) {
        return NULL;
    }
    PyObject *r = errors != NULL ? PyObject_CallFunction(cls, "s", errors) : PyObject_CallNoArgs(cls);
    Py_DECREF(cls);
    return r;
}

PyObject *
PyCodec_IncrementalEncoder(const char *encoding, const char *errors)
{
    return codec_incremental(encoding, errors, "incrementalencoder");
}

PyObject *
PyCodec_IncrementalDecoder(const char *encoding, const char *errors)
{
    return codec_incremental(encoding, errors, "incrementaldecoder");
}

static PyObject *
codec_stream(const char *encoding, PyObject *stream, const char *errors, int index)
{
    PyObject *cls = codec_item(encoding, index);
    if (cls == NULL) {
        return NULL;
    }
    PyObject *r = errors != NULL ? PyObject_CallFunction(cls, "Os", stream, errors)
                                 : PyObject_CallOneArg(cls, stream);
    Py_DECREF(cls);
    return r;
}

PyObject *
PyCodec_StreamReader(const char *encoding, PyObject *stream, const char *errors)
{
    return codec_stream(encoding, stream, errors, 2);
}

PyObject *
PyCodec_StreamWriter(const char *encoding, PyObject *stream, const char *errors)
{
    return codec_stream(encoding, stream, errors, 3);
}

/* Encode and decode through the registry; NULL errors means "strict". */

PyObject *
PyCodec_Encode(PyObject *object, const char *encoding, const char *errors)
{
    if (encoding == NULL) {
        PyErr_BadArgument();
        return NULL;
    }
    return call_codecs("encode", "(Oss)", object, encoding, errors != NULL ? errors : "strict");
}

PyObject *
PyCodec_Decode(PyObject *object, const char *encoding, const char *errors)
{
    if (encoding == NULL) {
        PyErr_BadArgument();
        return NULL;
    }
    return call_codecs("decode", "(Oss)", object, encoding, errors != NULL ? errors : "strict");
}

/* --- The error handlers and their registry ------------------------------ */

int
PyCodec_RegisterError(const char *name, PyObject *error)
{
    PyObject *r = call_codecs("register_error", "(sO)", name, error);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

PyObject *
PyCodec_LookupError(const char *name)
{
    return call_codecs("lookup_error", "(s)", name != NULL ? name : "strict");
}

/* _codecs' private handler of the same behavior. */
static PyObject *
error_handler(const char *name, PyObject *exc)
{
    return call_codecs(name, "(O)", exc);
}

PyObject *
PyCodec_StrictErrors(PyObject *exc)
{
    return error_handler("_strict_errors", exc);
}

PyObject *
PyCodec_IgnoreErrors(PyObject *exc)
{
    return error_handler("_ignore_errors", exc);
}

PyObject *
PyCodec_ReplaceErrors(PyObject *exc)
{
    return error_handler("_replace_errors", exc);
}

PyObject *
PyCodec_XMLCharRefReplaceErrors(PyObject *exc)
{
    return error_handler("_xmlcharrefreplace_errors", exc);
}

PyObject *
PyCodec_BackslashReplaceErrors(PyObject *exc)
{
    return error_handler("_backslashreplace_errors", exc);
}

PyObject *
PyCodec_NameReplaceErrors(PyObject *exc)
{
    return error_handler("_namereplace_errors", exc);
}

/* --- str codec entry points with C-visible codec state ------------------ */

/* Item 0 of a codec result tuple (the text or bytes; the consumed count and
   UTF byte order beside it when the caller asked for them). */
static PyObject *
codec_result(PyObject *result, Py_ssize_t *consumed, int *byteorder)
{
    PyObject *text = NULL;
    if (PyTuple_Check(result) && PyTuple_GET_SIZE(result) == (byteorder != NULL ? 3 : 2)) {
        if (consumed != NULL) {
            *consumed = PyLong_AsSsize_t(PyTuple_GET_ITEM(result, 1));
        }
        if (byteorder != NULL) {
            *byteorder = PyLong_AsLong(PyTuple_GET_ITEM(result, 2));
        }
        text = Py_NewRef(PyTuple_GET_ITEM(result, 0));
    }
    else {
        PyErr_SetString(PyExc_SystemError, "codec must return a result tuple");
    }
    Py_DECREF(result);
    return text;
}

PyObject *
PyUnicode_DecodeUTF8Stateful(const char *s, Py_ssize_t size, const char *errors, Py_ssize_t *consumed)
{
    /* CPython errors on an incomplete tail unless the caller learns where
       decoding stopped, so only a reported consumed count makes it final. */
    PyObject *r = call_codecs("utf_8_decode", "(y#zi)", s, size, errors, consumed == NULL);
    return r != NULL ? codec_result(r, consumed, NULL) : NULL;
}

/* utf_16_ex_decode and utf_32_ex_decode: (text, consumed, byteorder), the
   byte order a BOM settled reported back through byteorder. */
static PyObject *
decode_wide(const char *s, Py_ssize_t size, const char *errors, int *byteorder, Py_ssize_t *consumed,
            const char *name)
{
    PyObject *r = call_codecs(name, "(y#zii)", s, size, errors, byteorder != NULL ? *byteorder : 0,
                              consumed == NULL);
    return r != NULL ? codec_result(r, consumed, byteorder) : NULL;
}

PyObject *
PyUnicode_DecodeUTF16(const char *s, Py_ssize_t size, const char *errors, int *byteorder)
{
    return decode_wide(s, size, errors, byteorder, NULL, "utf_16_ex_decode");
}

PyObject *
PyUnicode_DecodeUTF16Stateful(const char *s, Py_ssize_t size, const char *errors, int *byteorder,
                              Py_ssize_t *consumed)
{
    return decode_wide(s, size, errors, byteorder, consumed, "utf_16_ex_decode");
}

PyObject *
PyUnicode_DecodeUTF32(const char *s, Py_ssize_t size, const char *errors, int *byteorder)
{
    return decode_wide(s, size, errors, byteorder, NULL, "utf_32_ex_decode");
}

PyObject *
PyUnicode_DecodeUTF32Stateful(const char *s, Py_ssize_t size, const char *errors, int *byteorder,
                              Py_ssize_t *consumed)
{
    return decode_wide(s, size, errors, byteorder, consumed, "utf_32_ex_decode");
}

PyObject *
PyUnicode_DecodeCharmap(const char *s, Py_ssize_t size, PyObject *mapping, const char *errors)
{
    if (mapping == NULL) {
        return PyUnicode_DecodeLatin1(s, size, errors);
    }
    PyObject *r = call_codecs("charmap_decode", "(y#zO)", s, size, errors, mapping);
    return r != NULL ? codec_result(r, NULL, NULL) : NULL;
}

PyObject *
PyUnicode_AsCharmapString(PyObject *unicode, PyObject *mapping)
{
    if (!PyUnicode_Check(unicode) || mapping == NULL) {
        PyErr_BadArgument();
        return NULL;
    }
    PyObject *r = call_codecs("charmap_encode", "(OzO)", unicode, NULL, mapping);
    return r != NULL ? codec_result(r, NULL, NULL) : NULL;
}
