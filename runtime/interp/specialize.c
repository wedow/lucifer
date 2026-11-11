/* The specializer (Python/specialize.c, LOAD_GLOBAL and LOAD_ATTR). It looks
   at what a generic instruction has just met and, when the answer depends on
   state that a version tracks, rewrites the instruction into the shape that
   checks the version and takes the answer from its cache words. Anything it
   cannot prove stays generic. The guards live in the eval loop.

   Rules that keep a cached answer true:
   - A type's attributes are cached only against its tp_version_tag, which
     lu_type_modified clears for the type and every subclass. The answer's
     type must be a static builtin type, whose methods cannot change.
   - A globals or module answer is an entry number checked against the dict's
     keys version, which every added or removed key clears; its value is read
     at execution.
   - An instance attribute is found by entry number in the instance's dict,
     and the key at that entry is compared on every execution; or, while
     the instance keeps its inline values, at the fixed offset of its
     name's index in the class's shared names (runtime/object/attrs.c).
   - Nothing is cached while the kernel boots: it fills static types' dicts
     without telling anyone.
   - A cached descriptor is a pointer the cache does not own, so it is only
     an object that refcounts cannot free: immortal, or with a deferred
     refcount, as cp314t's specializer requires. A collection is the only
     thing that frees a deferred object, and before it does, with the world
     stopped, it makes every cache forget it (lu_specialize_forget). A cached
     pointer therefore never dangles: a reader that loaded it may take a
     reference with a plain incref before its next poll, and the JIT
     translator may too.
   - The version and the answer are read as one: the version first, then the
     lookup, then the version again, so that a change in between is caught. */

#include "interp/specialize.h"
#include "interp/code.h"
#include "interp/instrument.h"
#include "object/dict.h"
#include "jit/jit.h"

/* _PyOpcode_Caches: CACHE entries after each instruction, specialized or not. */
const uint8_t lu_opcode_caches[256] = {
    [TO_BOOL] = 3, [STORE_SUBSCR] = 1, [SEND] = 1, [UNPACK_SEQUENCE] = 1,
    [STORE_ATTR] = 4, [LOAD_GLOBAL] = 4, [LOAD_SUPER_ATTR] = 1, [LOAD_ATTR] = 9,
    [COMPARE_OP] = 1, [CONTAINS_OP] = 1, [JUMP_BACKWARD] = 1, [POP_JUMP_IF_TRUE] = 1,
    [POP_JUMP_IF_FALSE] = 1, [POP_JUMP_IF_NONE] = 1, [POP_JUMP_IF_NOT_NONE] = 1,
    [FOR_ITER] = 1, [CALL] = 3, [CALL_KW] = 3, [CALL_FUNCTION_EX] = 0, [BINARY_OP] = 5,
    [LOAD_GLOBAL_MODULE] = 4, [LOAD_GLOBAL_BUILTIN] = 4,
    [LOAD_ATTR_MODULE] = 9, [LOAD_ATTR_CLASS] = 9, [LOAD_ATTR_CLASS_WITH_METACLASS_CHECK] = 9,
    [LOAD_ATTR_WITH_HINT] = 9, [LOAD_ATTR_METHOD_NO_DICT] = 9, [LOAD_ATTR_METHOD_WITH_VALUES] = 9,
    [LOAD_ATTR_NONDESCRIPTOR_NO_DICT] = 9, [LOAD_ATTR_NONDESCRIPTOR_WITH_VALUES] = 9,
    [LOAD_ATTR_SLOT] = 9, [STORE_ATTR_SLOT] = 4, [STORE_ATTR_WITH_HINT] = 4,
    [LOAD_ATTR_INSTANCE_VALUE] = 9, [STORE_ATTR_INSTANCE_VALUE] = 4, [LOAD_ATTR_PROPERTY] = 9,
    [INSTRUMENTED_LOAD_SUPER_ATTR] = 1, [INSTRUMENTED_JUMP_BACKWARD] = 1,
    [INSTRUMENTED_POP_JUMP_IF_TRUE] = 1, [INSTRUMENTED_POP_JUMP_IF_FALSE] = 1,
    [INSTRUMENTED_POP_JUMP_IF_NONE] = 1, [INSTRUMENTED_POP_JUMP_IF_NOT_NONE] = 1,
    [INSTRUMENTED_FOR_ITER] = 1, [INSTRUMENTED_CALL] = 3, [INSTRUMENTED_CALL_KW] = 3,
};

#define BACKOFF_MAX 12

/* The counter after a failed attempt: the next try comes after 2^n - 1 more
   executions, n growing to 12 (adaptive_counter_backoff). */
static uint16_t
backed_off(uint16_t counter)
{
    unsigned backoff = (counter & 15) + 1;
    if (backoff > BACKOFF_MAX) {
        backoff = BACKOFF_MAX;
    }
    return (uint16_t)((((1u << backoff) - 1) << 4) | backoff);
}

/* The counter after success: misses to forgive before looking again. */
#define COOLDOWN (52 << 4)

/* A failed attempt: the site stays generic and waits longer. */
static void
fail(lu_codeunit *site)
{
    lu_cache_store(site + 1, backed_off(lu_cache_load(site + 1)));
}

/* Rewriting a site, one writer at a time: begin makes it generic and clears
   its guard word; end sets the guard, the cooldown, and last the opcode.
   A site that monitoring has instrumented (INSTRUMENTED_LINE runs the
   instruction under it) is left alone and backs off as a failure would
   (CPython pauses its counter): begin returns 0, holding nothing. */
static int
begin(lu_codeunit *site, int generic, lu_codeunit *guard, int guard_words)
{
    PyMutex_Lock(&_PyRuntime.specialize_lock);
    if (site->op.code >= MIN_INSTRUMENTED_OPCODE) {
        PyMutex_Unlock(&_PyRuntime.specialize_lock);
        fail(site);
        return 0;
    }
    __atomic_store_n(&site->op.code, (uint8_t)generic, __ATOMIC_RELEASE);
    for (int i = 0; i < guard_words; i++) {
        lu_cache_store(guard + i, 0);
    }
    return 1;
}

static void
end(lu_codeunit *site, int opcode)
{
    lu_cache_store(site + 1, COOLDOWN);
    __atomic_store_n(&site->op.code, (uint8_t)opcode, __ATOMIC_RELEASE);
    PyMutex_Unlock(&_PyRuntime.specialize_lock);
}

static int
enabled(void)
{
    return PyInterpreterState_Get()->specializing;
}

/* LOAD_GLOBAL: the name is a key of the globals, or a key of the builtins
   and not of the globals. Both dicts must be exact, with str keys, and the
   entry number and the versions must fit their words. */

static int
unicode_dict(PyObject *d)
{
    return PyDict_CheckExact(d) && lu_dk_is_unicode(((PyDictObject *)d)->ma_keys);
}

void
lu_specialize_load_global(PyObject *globals, PyObject *builtins, lu_codeunit *site, PyObject *name)
{
    lu_codeunit *c = site + 1;
    if (!enabled() || !unicode_dict(globals)) {
        return;
    }
    Py_ssize_t index = lu_ns_index(globals, name);
    uint32_t globals_version = lu_dict_keys_version((PyDictObject *)globals);
    if (index >= 0) {
        if (index > UINT16_MAX || globals_version == 0) {
            return fail(site);
        }
        if (!begin(site, LOAD_GLOBAL, c + LG_MODULE_KEYS_VERSION, 2)) {
            return;
        }
        lu_cache_store(c + LG_INDEX, (uint16_t)index);
        lu_cache_store(c + LG_MODULE_KEYS_VERSION, (uint16_t)globals_version);
        end(site, LOAD_GLOBAL_MODULE);
        return;
    }
    if (!unicode_dict(builtins)) {
        return fail(site);
    }
    index = lu_ns_index(builtins, name);
    uint32_t builtins_version = lu_dict_keys_version((PyDictObject *)builtins);
    if (index < 0 || index > UINT16_MAX || globals_version == 0 || builtins_version == 0) {
        return fail(site);
    }
    if (!begin(site, LOAD_GLOBAL, c + LG_MODULE_KEYS_VERSION, 2)) {
        return;
    }
    lu_cache_store(c + LG_INDEX, (uint16_t)index);
    lu_cache_store(c + LG_BUILTIN_KEYS_VERSION, (uint16_t)builtins_version);
    lu_cache_store(c + LG_MODULE_KEYS_VERSION, (uint16_t)globals_version);
    end(site, LOAD_GLOBAL_BUILTIN);
}

/* LOAD_ATTR. */

/* What a name is in a type's MRO, for the purpose of caching it. */
enum descr_kind {
    DESCR_ABSENT,
    DESCR_METHOD,           /* a function or method descriptor: binds to the owner */
    DESCR_NON_DESCRIPTOR,   /* a value that is its own answer */
    DESCR_OTHER,            /* anything that runs code or whose type could change */
};

/* `name` in tp's MRO as of tp's version *version, which is 0 when tp has no
   version or changed during the lookup. *descr is a new reference, or NULL. */
static enum descr_kind
analyze(PyTypeObject *tp, PyObject *name, PyObject **descr, uint32_t *version)
{
    *version = lu_type_assign_version(tp) ? __atomic_load_n(&tp->tp_version_tag, __ATOMIC_ACQUIRE) : 0;
    *descr = _PyType_LookupRef(tp, name);
    if (__atomic_load_n(&tp->tp_version_tag, __ATOMIC_ACQUIRE) != *version) {
        *version = 0;
    }
    if (*descr == NULL) {
        return DESCR_ABSENT;
    }
    PyTypeObject *dt = Py_TYPE(*descr);
    /* An answer whose type could gain __get__ or __set__ later would not be
       the answer any more, and no version of ours would say so. */
    if (!(dt->tp_flags & Py_TPFLAGS_IMMUTABLETYPE) || dt->tp_descr_set != NULL) {
        return DESCR_OTHER;
    }
    if (dt->tp_flags & Py_TPFLAGS_METHOD_DESCRIPTOR) {
        return DESCR_METHOD;
    }
    return dt->tp_descr_get == NULL ? DESCR_NON_DESCRIPTOR : DESCR_OTHER;
}

/* Whether `tp` finds attributes with the default __getattribute__ of `base`. */
static int
default_getattribute(PyTypeObject *tp, PyTypeObject *base)
{
    return _PyType_Lookup(tp, LU_ID(__getattribute__)) == _PyType_Lookup(base, LU_ID(__getattribute__));
}

/* Whether a cache may name descr without owning it (see the rules above). */
static int
cacheable(PyObject *descr)
{
    return descr == NULL || _Py_IsImmortal(descr) || lu_has_deferred_refcount(descr);
}

static void
set_attr_cache(lu_codeunit *site, int opcode, uint32_t version, uint32_t second, PyObject *descr)
{
    lu_codeunit *c = site + 1;
    if (!cacheable(descr)) {
        return fail(site);
    }
    if (!begin(site, LOAD_ATTR, c + LA_VERSION, 2)) {
        return;
    }
    lu_cache_store32(c + LA_KEYS_VERSION, second);
    lu_cache_store_ptr(c + LA_DESCR, descr);
    lu_cache_store32(c + LA_VERSION, version);
    end(site, opcode);
}

static void
specialize_module(PyObject *owner, lu_codeunit *site, PyObject *name)
{
    PyObject *dict = ((PyModuleObject *)owner)->md_dict;
    if (!unicode_dict(dict)) {
        return fail(site);
    }
    Py_ssize_t index = lu_ns_index(dict, name);
    uint32_t version = lu_dict_keys_version((PyDictObject *)dict);
    if (index < 0 || index > UINT16_MAX || version == 0) {
        return fail(site);
    }
    set_attr_cache(site, LOAD_ATTR_MODULE, version, (uint32_t)index, NULL);
}

/* A class's attribute: what `type.__getattribute__` returns for a plain
   function, a method descriptor or a value of a builtin type, unless the
   metaclass has the name too. Only a metaclass that is not `type` can change. */
static void
specialize_class(PyObject *owner, lu_codeunit *site, PyObject *name)
{
    PyTypeObject *cls = (PyTypeObject *)owner, *meta = Py_TYPE(cls);
    PyObject *descr, *meta_descr;
    uint32_t version, meta_version;
    if (!default_getattribute(meta, &PyType_Type)) {
        return fail(site);
    }
    analyze(meta, name, &meta_descr, &meta_version);
    enum descr_kind kind = analyze(cls, name, &descr, &version);
    if (meta_descr != NULL || (kind != DESCR_METHOD && kind != DESCR_NON_DESCRIPTOR)
        || version == 0 || meta_version == 0) {
        fail(site);
    }
    else if (meta == &PyType_Type) {
        set_attr_cache(site, LOAD_ATTR_CLASS, version, 0, descr);
    }
    else {
        set_attr_cache(site, LOAD_ATTR_CLASS_WITH_METACLASS_CHECK, version, meta_version, descr);
    }
    Py_XDECREF(meta_descr);
    Py_XDECREF(descr);
}

/* A __slots__ member that is an object reference: its byte offset in the
   instance fits an index word, and the descriptor type is immutable. */
static int
is_slot_member(PyObject *descr)
{
    if (descr == NULL || !Py_IS_TYPE(descr, &PyMemberDescr_Type)) {
        return 0;
    }
    PyMemberDef *m = ((PyMemberDescrObject *)descr)->d_member;
    return m->type == Py_T_OBJECT_EX && m->offset >= 0 && m->offset <= UINT16_MAX;
}

/* The index of name's entry in owner's managed dict, whose key is this
   very str, or -1. */
static Py_ssize_t
dict_hint(PyObject *owner, int managed, PyObject *name)
{
    PyObject *dict = managed && owner != NULL ? LU_MANAGED_DICT(owner) : NULL;
    Py_ssize_t index = dict != NULL && unicode_dict(dict) ? lu_ns_index(dict, name) : -1;
    return index >= 0 && index <= UINT16_MAX && lu_dk_key(((PyDictObject *)dict)->ma_keys, index) == name ? index : -1;
}

/* What LOAD_ATTR of name specializes to on an instance of a type whose
   __getattribute__ is object's: the opcode (LOAD_ATTR for none), the
   version, the second word and the answer (a new reference). The owner may
   be NULL: an instance of tp that keeps its values inline and has no dict,
   as one compiled code just made. */
static int
instance_shape(PyTypeObject *tp, PyObject *owner, PyObject *name, int method, uint32_t *version,
               uint32_t *second, PyObject **descr)
{
    *second = 0;
    *descr = NULL;
    if (!default_getattribute(tp, &PyBaseObject_Type)) {
        return LOAD_ATTR;
    }
    enum descr_kind kind = analyze(tp, name, descr, version);
    int managed = (tp->tp_flags & Py_TPFLAGS_MANAGED_DICT) != 0;
    /* After the version: a name that shadows the class's joins the shared
       names before the version changes. */
    Py_ssize_t inline_index = (tp->tp_flags & Py_TPFLAGS_INLINE_VALUES) ? lu_inline_index(tp, name) : -1;
    int op = LOAD_ATTR;
    if (*version == 0 || (!managed && tp->tp_dictoffset != 0)) {
        op = LOAD_ATTR;
    }
    else if (is_slot_member(*descr)) {
        *second = (uint32_t)((PyMemberDescrObject *)*descr)->d_member->offset;
        Py_CLEAR(*descr);
        op = LOAD_ATTR_SLOT;
    }
    else if (inline_index >= 0 && kind != DESCR_OTHER) {
        /* An inline value, which shadows a non-data attribute of the class:
           no instance of it has a dict that lacks the name for sure. An
           instance whose values moved to a dict reads it by its hint, when
           its dict has the name (the class's value would ignore the inline
           values of others). */
        size_t offset = (size_t)tp->tp_basicsize + (size_t)inline_index * LU_INLINE_STRIDE * sizeof(PyObject *);
        Py_ssize_t hint;
        if (owner == NULL || lu_inline_live(owner)) {
            if (offset <= UINT16_MAX) {
                *second = (uint32_t)offset;
                op = LOAD_ATTR_INSTANCE_VALUE;
            }
            if (op != LOAD_ATTR_INSTANCE_VALUE || kind != DESCR_NON_DESCRIPTOR || method || !cacheable(*descr)) {
                Py_CLEAR(*descr);
            }
            /* else the class's value, for compiled code to answer an
               instance that has none of its own (an _IOBase's __closed) */
        }
        else {
            if ((hint = dict_hint(owner, managed, name)) >= 0) {
                *second = (uint32_t)hint;
                op = LOAD_ATTR_WITH_HINT;
            }
            Py_CLEAR(*descr);
        }
    }
    else if (kind == DESCR_ABSENT || (kind == DESCR_NON_DESCRIPTOR && !method && dict_hint(owner, managed, name) >= 0)) {
        /* In the instance's dict, at an entry whose key is this very str;
           also where that entry shadows a class attribute that is no
           descriptor (BytesIO's _buffer). */
        Py_ssize_t index = dict_hint(owner, managed, name);
        if (index >= 0) {
            *second = (uint32_t)index;
            op = LOAD_ATTR_WITH_HINT;
        }
        Py_CLEAR(*descr);
    }
    else if (kind == DESCR_METHOD && method) {
        op = managed ? LOAD_ATTR_METHOD_WITH_VALUES : LOAD_ATTR_METHOD_NO_DICT;
    }
    else if (kind == DESCR_METHOD && !managed && tp->tp_dictoffset == 0
             && (PyFunction_Check(*descr) || Py_IS_TYPE(*descr, &PyMethodDescr_Type)
                 || Py_IS_TYPE(*descr, &PyWrapperDescr_Type))) {
        /* The method as an attribute (f = x.append) of an instance with no
           dict to shadow it: bound in C (lu_bind_method). */
        op = LOAD_ATTR_METHOD_NO_DICT;
    }
    else if (kind == DESCR_NON_DESCRIPTOR) {
        op = managed ? LOAD_ATTR_NONDESCRIPTOR_WITH_VALUES : LOAD_ATTR_NONDESCRIPTOR_NO_DICT;
    }
    else if (!method && (Py_IS_TYPE(*descr, &PyProperty_Type) || Py_IS_TYPE(*descr, &PyGetSetDescr_Type))
             && lu_property_fget(*descr) != NULL && PyFunction_Check(lu_property_fget(*descr))) {
        /* A property whose getter is a function, or a kernel getset (whose
           getter is kernel Python): the getter called, which is read from
           the property each time (its __init__ may change it). */
        op = LOAD_ATTR_PROPERTY;
    }
    if (op == LOAD_ATTR || !cacheable(*descr)) {
        Py_CLEAR(*descr);
        return LOAD_ATTR;
    }
    return op;
}

/* An instance's attribute, for a type whose __getattribute__ is object's. */
static void
specialize_instance(PyObject *owner, lu_codeunit *site, PyObject *name, int method)
{
    uint32_t version, second;
    PyObject *descr;
    int op = instance_shape(Py_TYPE(owner), owner, name, method, &version, &second, &descr);
    if (op == LOAD_ATTR) {
        fail(site);
    }
    else {
        set_attr_cache(site, op, version, second, descr);
    }
    Py_XDECREF(descr);
}

PyObject *
lu_specialize_class_value(PyTypeObject *cls, PyObject *name, uint32_t v[3])
{
    PyTypeObject *meta = Py_TYPE(cls);
    if (!enabled() || !default_getattribute(meta, &PyType_Type)) {
        return NULL;
    }
    PyObject *descr, *meta_descr;
    analyze(meta, name, &meta_descr, &v[1]);
    analyze(cls, name, &descr, &v[0]);
    Py_XDECREF(meta_descr);
    if (descr == NULL || meta_descr != NULL || v[0] == 0 || v[1] == 0) {
        Py_XDECREF(descr);
        return NULL;
    }
    PyTypeObject *dt = Py_TYPE(descr);
    v[2] = lu_type_assign_version(dt) ? __atomic_load_n(&dt->tp_version_tag, __ATOMIC_ACQUIRE) : 0;
    if (v[2] == 0 || dt->tp_descr_get != NULL || __atomic_load_n(&dt->tp_version_tag, __ATOMIC_ACQUIRE) != v[2]) {
        Py_DECREF(descr);
        return NULL;
    }
    return descr;
}

PyObject *
lu_specialize_class_method(PyTypeObject *cls, PyObject *name, uint32_t v[2])
{
    PyTypeObject *meta = Py_TYPE(cls);
    if (!enabled() || !default_getattribute(meta, &PyType_Type)) {
        return NULL;
    }
    PyObject *descr, *meta_descr;
    analyze(meta, name, &meta_descr, &v[1]);
    analyze(cls, name, &descr, &v[0]);
    Py_XDECREF(meta_descr);
    if (descr == NULL || meta_descr != NULL || v[0] == 0 || v[1] == 0
        || !(Py_IS_TYPE(descr, &PyClassMethod_Type) || Py_IS_TYPE(descr, &PyStaticMethod_Type))) {
        Py_XDECREF(descr);
        return NULL;
    }
    return descr;
}

PyObject *
lu_specialize_instance_method(PyTypeObject *tp, PyObject *name, uint32_t *version, int *managed)
{
    getattrofunc get = tp->tp_getattro;
    if (!enabled() || (get != lu_slot_tp_getattro && get != PyObject_GenericGetAttr)
        || !default_getattribute(tp, &PyBaseObject_Type)) {
        return NULL;
    }
    PyObject *descr;
    analyze(tp, name, &descr, version);
    *managed = (tp->tp_flags & Py_TPFLAGS_MANAGED_DICT) != 0;
    int shadowed = (tp->tp_flags & Py_TPFLAGS_INLINE_VALUES) && lu_inline_index(tp, name) >= 0;
    if (descr == NULL || *version == 0 || shadowed || (!*managed && tp->tp_dictoffset != 0)
        || !(Py_IS_TYPE(descr, &PyClassMethod_Type) || Py_IS_TYPE(descr, &PyStaticMethod_Type))) {
        Py_XDECREF(descr);
        return NULL;
    }
    return descr;
}

int
lu_specialize_load_fresh(PyTypeObject *tp, PyObject *name, int method, uint32_t *version, uint16_t *index,
                         PyObject **descr)
{
    getattrofunc get = tp->tp_getattro;
    uint32_t second = 0;
    int op = enabled() && (get == lu_slot_tp_getattro || get == PyObject_GenericGetAttr)
        ? instance_shape(tp, NULL, name, method, version, &second, descr) : LOAD_ATTR;
    *index = (uint16_t)second;
    if (op == LOAD_ATTR) {
        *descr = NULL;
    }
    return op;
}

void
lu_specialize_load_attr(PyObject *owner, lu_codeunit *site, PyObject *name)
{
    if (!enabled()) {
        return;
    }
    getattrofunc get = Py_TYPE(owner)->tp_getattro;
    if (get != lu_slot_tp_getattro && get != PyObject_GenericGetAttr) {
        return fail(site);
    }
    if (PyModule_CheckExact(owner)) {
        specialize_module(owner, site, name);
    }
    else if (PyType_Check(owner)) {
        specialize_class(owner, site, name);
    }
    else {
        specialize_instance(owner, site, name, site->op.arg & 1);
    }
}

/* STORE_ATTR on an instance of a type whose __setattr__ is object's: a
   writable __slots__ member is stored at its offset; a name the type does not
   have goes into the instance's dict. */
/* STORE_ATTR's inline value for name on an instance of tp that keeps its
   values inline: its byte offset, or -1. */
static Py_ssize_t
store_inline_offset(PyTypeObject *tp, PyObject *name, enum descr_kind kind)
{
    Py_ssize_t index = kind != DESCR_OTHER ? lu_inline_index(tp, name) : -1;
    if (index >= 0) {
        index = tp->tp_basicsize + index * LU_INLINE_STRIDE * (Py_ssize_t)sizeof(PyObject *);
    }
    return index <= UINT16_MAX ? index : -1;
}

int
lu_specialize_store_fresh(PyTypeObject *tp, PyObject *name, uint32_t *version, uint16_t *index)
{
    if (!enabled() || _PyType_Lookup(tp, LU_ID(__setattr__)) != _PyType_Lookup(&PyBaseObject_Type, LU_ID(__setattr__))
        || !(tp->tp_flags & Py_TPFLAGS_INLINE_VALUES)) {
        return STORE_ATTR;
    }
    PyObject *descr;
    enum descr_kind kind = analyze(tp, name, &descr, version);
    Py_ssize_t offset = *version != 0 && !is_slot_member(descr) ? store_inline_offset(tp, name, kind) : -1;
    Py_XDECREF(descr);
    *index = (uint16_t)(offset >= 0 ? offset : 0);
    return offset >= 0 ? STORE_ATTR_INSTANCE_VALUE : STORE_ATTR;
}

void
lu_specialize_store_attr(PyObject *owner, lu_codeunit *site, PyObject *name)
{
    PyTypeObject *tp = Py_TYPE(owner);
    if (!enabled() || _PyType_Lookup(tp, LU_ID(__setattr__)) != _PyType_Lookup(&PyBaseObject_Type, LU_ID(__setattr__))) {
        return;
    }
    PyObject *descr;
    uint32_t version;
    enum descr_kind kind = analyze(tp, name, &descr, &version);
    Py_ssize_t index;
    lu_codeunit *c = site + 1;
    if (version == 0) {
        fail(site);
    }
    else if (is_slot_member(descr) && !(((PyMemberDescrObject *)descr)->d_member->flags & Py_READONLY)) {
        if (begin(site, STORE_ATTR, c + LA_VERSION, 3)) {
            lu_cache_store(c + LA_INDEX, (uint16_t)((PyMemberDescrObject *)descr)->d_member->offset);
            lu_cache_store32(c + LA_VERSION, version);
            end(site, STORE_ATTR_SLOT);
        }
    }
    else if (lu_inline_live(owner) && (index = store_inline_offset(tp, name, kind)) >= 0) {
        if (begin(site, STORE_ATTR, c + LA_VERSION, 3)) {
            lu_cache_store(c + LA_INDEX, (uint16_t)index);      /* the byte offset */
            lu_cache_store32(c + LA_VERSION, version);
            end(site, STORE_ATTR_INSTANCE_VALUE);
        }
    }
    else if (descr == NULL && (tp->tp_flags & Py_TPFLAGS_MANAGED_DICT)) {
        if (begin(site, STORE_ATTR, c + LA_VERSION, 3)) {
            lu_cache_store32(c + LA_VERSION, version);
            end(site, STORE_ATTR_WITH_HINT);
        }
    }
    else {
        fail(site);
    }
    Py_XDECREF(descr);
}

/* A collection is about to free what `dead` answers true for, with the world
   stopped: the code's caches stop naming it, and their sites fail their
   guard until they specialize again. */
void
lu_specialize_forget(PyCodeObject *code, int (*dead)(PyObject *))
{
    lu_codeunit *start = lu_code_start(code);
    for (Py_ssize_t i = 0, n = Py_SIZE(code); i < n; ) {
        int base = lu_base_opcode(code, i);
        lu_codeunit *c = start + i + 1;
        PyObject *descr = base == LOAD_ATTR ? lu_cache_load_ptr(c + LA_DESCR) : NULL;
        if (descr != NULL && dead(descr)) {
            lu_cache_store32(c + LA_VERSION, 0);
            lu_cache_store_ptr(c + LA_DESCR, NULL);
        }
        i += 1 + lu_opcode_caches[base];
    }
    lu_jit_pic_forget(code, dead);
}
