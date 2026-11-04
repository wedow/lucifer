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
     and the key at that entry is compared on every execution.
   - Nothing is cached while the kernel boots: it fills static types' dicts
     without telling anyone.
   - A cached descriptor is a borrowed pointer, kept alive only by the type
     dict that holds it. That is safe while one task runs at a time, as under
     -X lucifer_handoff, where each hand-over is an acquire and release of the
     run-queue lock: no carrier is between a guard and its use while another
     changes the type. Tasks running at once need what cp314t's specializer
     requires, a cached object whose refcount is deferred (freed only by a
     stopped-world collection), or reclamation through QSBR. */

#include "interp/specialize.h"
#include "interp/code.h"
#include "object/dict.h"

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

static enum descr_kind
analyze(PyTypeObject *tp, PyObject *name, PyObject **descr)
{
    *descr = _PyType_Lookup(tp, name);
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

static void
set_attr_cache(lu_codeunit *site, int opcode, uint32_t version, uint32_t second, PyObject *descr)
{
    lu_codeunit *c = site + 1;
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
    PyObject *descr;
    if (!default_getattribute(meta, &PyType_Type) || _PyType_Lookup(meta, name) != NULL) {
        return fail(site);
    }
    enum descr_kind kind = analyze(cls, name, &descr);
    if ((kind != DESCR_METHOD && kind != DESCR_NON_DESCRIPTOR) || !lu_type_assign_version(cls)
        || !lu_type_assign_version(meta)) {
        return fail(site);
    }
    if (meta == &PyType_Type) {
        set_attr_cache(site, LOAD_ATTR_CLASS, cls->tp_version_tag, 0, descr);
    }
    else {
        set_attr_cache(site, LOAD_ATTR_CLASS_WITH_METACLASS_CHECK, cls->tp_version_tag,
                       meta->tp_version_tag, descr);
    }
}

/* An instance's attribute, for a type whose __getattribute__ is object's. */
static void
specialize_instance(PyObject *owner, lu_codeunit *site, PyObject *name, int method)
{
    PyTypeObject *tp = Py_TYPE(owner);
    PyObject *descr;
    if (!default_getattribute(tp, &PyBaseObject_Type)) {
        return fail(site);
    }
    enum descr_kind kind = analyze(tp, name, &descr);
    if (!lu_type_assign_version(tp)) {
        return fail(site);
    }
    int managed = (tp->tp_flags & Py_TPFLAGS_MANAGED_DICT) != 0;
    if (!managed && tp->tp_dictoffset != 0) {
        return fail(site);
    }
    if (kind == DESCR_ABSENT) {
        /* In the instance's dict, at an entry whose key is this very str. */
        PyObject *dict = managed ? LU_MANAGED_DICT(owner) : NULL;
        Py_ssize_t index = dict != NULL && unicode_dict(dict) ? lu_ns_index(dict, name) : -1;
        if (index < 0 || index > UINT16_MAX
            || lu_dk_key(((PyDictObject *)dict)->ma_keys, index) != name) {
            return fail(site);
        }
        set_attr_cache(site, LOAD_ATTR_WITH_HINT, tp->tp_version_tag, (uint32_t)index, NULL);
    }
    else if (kind == DESCR_METHOD && method) {
        set_attr_cache(site, managed ? LOAD_ATTR_METHOD_WITH_VALUES : LOAD_ATTR_METHOD_NO_DICT,
                       tp->tp_version_tag, 0, descr);
    }
    else if (kind == DESCR_NON_DESCRIPTOR) {
        set_attr_cache(site, managed ? LOAD_ATTR_NONDESCRIPTOR_WITH_VALUES
                                     : LOAD_ATTR_NONDESCRIPTOR_NO_DICT,
                       tp->tp_version_tag, 0, descr);
    }
    else {
        fail(site);
    }
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
