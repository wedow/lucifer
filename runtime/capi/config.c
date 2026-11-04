/* PyConfig_Get, PyConfig_GetInt, PyConfig_Names and PyConfig_Set (PEP 741,
   Python/initconfig.c): a table naming each member of the interpreter's
   PyConfig - its type, whether it is public, its sys attribute or sys.flags
   field, and the legacy global variable it keeps in step - and the get, set
   and enumerate operations over that table. The preconfig's read-only members
   are served from the PyPreConfig the runtime's config implies, built on
   demand: lucifer has no preconfig of its own. _Py_GetConfigsAsDict (what
   _testinternalcapi.get_configs returns) turns the same tables, and the
   global variables, into dictionaries. */

#include "lucifer.h"

#include <stddef.h>
#include <string.h>

/* The spec table (initconfig.c's PyConfigSpec). SYS_ATTR names the sys
   attribute an option reads and writes, SYS_FLAG a sys.flags field, and
   GLOBAL a legacy global variable, stored negated when `not` is set. */

typedef PyObject *(*sys_flag_setter)(int value);

typedef enum {
    MEMBER_INT = 0, MEMBER_UINT = 1, MEMBER_ULONG = 2, MEMBER_BOOL = 3,
    MEMBER_WSTR = 10, MEMBER_WSTR_OPT = 11, MEMBER_WSTR_LIST = 12,
} member_type;

typedef enum { INIT_ONLY = 0, READ_ONLY = 1, PUBLIC = 2 } member_visibility;

typedef struct {
    const char *name;
    size_t offset;
    member_type type;
    member_visibility visibility;
    struct {
        const char *attr;
        int flag_index;
        sys_flag_setter flag_setter;
    } sys;
    struct {
        int *ptr;
        int not;
    } global_var;
} config_spec;

#define SYS_ATTR(name) {name, -1, NULL}
#define SYS_FLAG_SETTER(index, setter) {NULL, index, setter}
#define SYS_FLAG(index) SYS_FLAG_SETTER(index, NULL)
#define NO_SYS SYS_ATTR(NULL)
#define GLOBAL(ptr, not) {ptr, not}
#define NO_GLOBAL GLOBAL(NULL, 0)
#define SPEC(MEMBER, TYPE, VISIBILITY, sys, global_var) \
    {#MEMBER, offsetof(PyConfig, MEMBER), MEMBER_##TYPE, VISIBILITY, sys, global_var}

static PyObject *
flag_not(int value)
{
    return PyLong_FromLong(!value);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

static const config_spec PYCONFIG_SPEC[] = {
    // --- Public options -----------

    SPEC(argv, WSTR_LIST, PUBLIC, SYS_ATTR("argv"), NO_GLOBAL),
    SPEC(base_exec_prefix, WSTR_OPT, PUBLIC, SYS_ATTR("base_exec_prefix"), NO_GLOBAL),
    SPEC(base_executable, WSTR_OPT, PUBLIC, SYS_ATTR("_base_executable"), NO_GLOBAL),
    SPEC(base_prefix, WSTR_OPT, PUBLIC, SYS_ATTR("base_prefix"), NO_GLOBAL),
    SPEC(bytes_warning, UINT, PUBLIC, SYS_FLAG(9), GLOBAL(&Py_BytesWarningFlag, 0)),
    SPEC(cpu_count, INT, PUBLIC, NO_SYS, NO_GLOBAL),
    SPEC(exec_prefix, WSTR_OPT, PUBLIC, SYS_ATTR("exec_prefix"), NO_GLOBAL),
    SPEC(executable, WSTR_OPT, PUBLIC, SYS_ATTR("executable"), NO_GLOBAL),
    SPEC(inspect, BOOL, PUBLIC, SYS_FLAG(1), GLOBAL(&Py_InspectFlag, 0)),
    SPEC(int_max_str_digits, UINT, PUBLIC, NO_SYS, NO_GLOBAL),
    SPEC(interactive, BOOL, PUBLIC, SYS_FLAG(2), GLOBAL(&Py_InteractiveFlag, 0)),
    SPEC(module_search_paths, WSTR_LIST, PUBLIC, SYS_ATTR("path"), NO_GLOBAL),
    SPEC(optimization_level, UINT, PUBLIC, SYS_FLAG(3), GLOBAL(&Py_OptimizeFlag, 0)),
    SPEC(parser_debug, BOOL, PUBLIC, SYS_FLAG(0), GLOBAL(&Py_DebugFlag, 0)),
    SPEC(platlibdir, WSTR, PUBLIC, SYS_ATTR("platlibdir"), NO_GLOBAL),
    SPEC(prefix, WSTR_OPT, PUBLIC, SYS_ATTR("prefix"), NO_GLOBAL),
    SPEC(pycache_prefix, WSTR_OPT, PUBLIC, SYS_ATTR("pycache_prefix"), NO_GLOBAL),
    SPEC(quiet, BOOL, PUBLIC, SYS_FLAG(10), GLOBAL(&Py_QuietFlag, 0)),
    SPEC(stdlib_dir, WSTR_OPT, PUBLIC, SYS_ATTR("_stdlib_dir"), NO_GLOBAL),
    SPEC(use_environment, BOOL, PUBLIC,
         SYS_FLAG_SETTER(7, flag_not), GLOBAL(&Py_IgnoreEnvironmentFlag, 1)),
    SPEC(verbose, UINT, PUBLIC, SYS_FLAG(8), GLOBAL(&Py_VerboseFlag, 0)),
    SPEC(warnoptions, WSTR_LIST, PUBLIC, SYS_ATTR("warnoptions"), NO_GLOBAL),
    SPEC(write_bytecode, BOOL, PUBLIC, SYS_FLAG_SETTER(4, flag_not),
         GLOBAL(&Py_DontWriteBytecodeFlag, 1)),
    SPEC(xoptions, WSTR_LIST, PUBLIC, SYS_ATTR("_xoptions"), NO_GLOBAL),

    // --- Read-only options -----------

    SPEC(buffered_stdio, BOOL, READ_ONLY, NO_SYS, GLOBAL(&Py_UnbufferedStdioFlag, 1)),
    SPEC(check_hash_pycs_mode, WSTR, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(code_debug_ranges, BOOL, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(configure_c_stdio, BOOL, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(dev_mode, BOOL, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(dump_refs, BOOL, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(dump_refs_file, WSTR_OPT, READ_ONLY, NO_SYS, NO_GLOBAL),
#ifdef Py_GIL_DISABLED
    SPEC(enable_gil, INT, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(tlbc_enabled, INT, READ_ONLY, NO_SYS, NO_GLOBAL),
#endif
    SPEC(faulthandler, BOOL, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(filesystem_encoding, WSTR, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(filesystem_errors, WSTR, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(hash_seed, ULONG, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(home, WSTR_OPT, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(thread_inherit_context, INT, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(context_aware_warnings, INT, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(import_time, UINT, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(install_signal_handlers, BOOL, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(isolated, BOOL, READ_ONLY, NO_SYS, GLOBAL(&Py_IsolatedFlag, 0)),
    SPEC(malloc_stats, BOOL, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(orig_argv, WSTR_LIST, READ_ONLY, SYS_ATTR("orig_argv"), NO_GLOBAL),
    SPEC(parse_argv, BOOL, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(pathconfig_warnings, BOOL, READ_ONLY, NO_SYS, GLOBAL(&Py_FrozenFlag, 1)),
    SPEC(perf_profiling, UINT, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(remote_debug, BOOL, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(program_name, WSTR, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(run_command, WSTR_OPT, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(run_filename, WSTR_OPT, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(run_module, WSTR_OPT, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(safe_path, BOOL, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(show_ref_count, BOOL, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(site_import, BOOL, READ_ONLY, NO_SYS, GLOBAL(&Py_NoSiteFlag, 1)),
    SPEC(skip_source_first_line, BOOL, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(stdio_encoding, WSTR, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(stdio_errors, WSTR, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(tracemalloc, UINT, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(use_frozen_modules, BOOL, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(use_hash_seed, BOOL, READ_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(user_site_directory, BOOL, READ_ONLY, NO_SYS, GLOBAL(&Py_NoUserSiteDirectory, 1)),
    SPEC(warn_default_encoding, BOOL, READ_ONLY, NO_SYS, NO_GLOBAL),

    // --- Init-only options -----------

    SPEC(_config_init, UINT, INIT_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(_init_main, BOOL, INIT_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(_install_importlib, BOOL, INIT_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(module_search_paths_set, BOOL, INIT_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(pythonpath_env, WSTR_OPT, INIT_ONLY, NO_SYS, NO_GLOBAL),
    SPEC(sys_path_0, WSTR_OPT, INIT_ONLY, NO_SYS, NO_GLOBAL),

    {NULL, 0, 0, 0, NO_SYS, NO_GLOBAL},
};

#undef SPEC
#define SPEC(MEMBER, TYPE, VISIBILITY) \
    {#MEMBER, offsetof(PyPreConfig, MEMBER), MEMBER_##TYPE, VISIBILITY, NO_SYS, NO_GLOBAL}

static const config_spec PYPRECONFIG_SPEC[] = {
    // --- Read-only options -----------

    SPEC(allocator, INT, READ_ONLY),
    SPEC(coerce_c_locale, BOOL, READ_ONLY),
    SPEC(coerce_c_locale_warn, BOOL, READ_ONLY),
    SPEC(configure_locale, BOOL, READ_ONLY),
    SPEC(utf8_mode, BOOL, READ_ONLY),

    // --- Init-only options -----------
    // Members already present in PYCONFIG_SPEC

    SPEC(_config_init, INT, INIT_ONLY),
    SPEC(dev_mode, BOOL, INIT_ONLY),
    SPEC(isolated, BOOL, INIT_ONLY),
    SPEC(parse_argv, BOOL, INIT_ONLY),
    SPEC(use_environment, BOOL, INIT_ONLY),

    {NULL, 0, 0, 0, NO_SYS, NO_GLOBAL},
};

#pragma GCC diagnostic pop

#undef SPEC
#undef SYS_ATTR
#undef SYS_FLAG_SETTER
#undef SYS_FLAG
#undef NO_SYS
#undef GLOBAL
#undef NO_GLOBAL

static const config_spec *
find_spec(const config_spec *table, const char *name)
{
    for (; table->name != NULL; table++) {
        if (table->visibility != INIT_ONLY && strcmp(name, table->name) == 0) {
            return table;
        }
    }
    return NULL;
}

/* The PyPreConfig the runtime's config implies: UTF-8 mode as lucifer reads
   it, the command line's decisions, no locale coercion, the default
   allocator. */
static PyPreConfig
preconfig_of(const PyConfig *config)
{
    PyPreConfig pre;
    memset(&pre, 0, sizeof pre);
    pre._config_init = 1;
    pre.configure_locale = 1;
    pre.utf8_mode = lu_utf8_mode(config);
    pre.allocator = 0;
    pre.dev_mode = config->dev_mode > 0;
    pre.isolated = config->isolated;
    pre.parse_argv = config->parse_argv;
    pre.use_environment = config->use_environment;
    return pre;
}

/* --- PyConfig_Get() ----------------------------------------------- */

/* sys.NAME as a new reference, an error when absent
   (_PySys_GetRequiredAttrString). */
static PyObject *
sys_attr(const char *name)
{
    PyObject *value = PySys_GetObject(name);
    if (value == NULL) {
        PyErr_Format(PyExc_RuntimeError, "lost sys.%s", name);
        return NULL;
    }
    return Py_NewRef(value);
}

/* _PyConfig_CreateXOptionsDict: name[=value] entries as a dict, a bare name
   valued True. */
static PyObject *
xoptions_dict(const PyWideStringList *xoptions)
{
    PyObject *dict = PyDict_New();
    for (Py_ssize_t i = 0; dict != NULL && i < xoptions->length; i++) {
        const wchar_t *option = xoptions->items[i];
        const wchar_t *equals = wcschr(option, L'=');
        PyObject *name = PyUnicode_FromWideChar(option, equals != NULL ? equals - option : -1);
        PyObject *value = name != NULL && equals != NULL
            ? PyUnicode_FromWideChar(equals + 1, -1) : Py_NewRef(Py_True);
        int err = name == NULL || value == NULL || PyDict_SetItem(dict, name, value) < 0;
        Py_XDECREF(name);
        Py_XDECREF(value);
        if (err) {
            Py_CLEAR(dict);
        }
    }
    return dict;
}

/* config_get(object, spec, use_sys): the option as a Python object - its sys
   attribute or its member, an int, a bool, a str or None, or a list (the
   xoptions as a dict). `object` is the PyConfig or, for the preconfig's
   members, the PyPreConfig, both read at the spec's offset. */
static PyObject *
config_get(const void *object, const config_spec *spec, int use_sys)
{
    if (use_sys) {
        if (spec->sys.attr != NULL) {
            return sys_attr(spec->sys.attr);
        }
        if (strcmp(spec->name, "write_bytecode") == 0) {
            PyObject *attr = sys_attr("dont_write_bytecode");
            int is_true = attr != NULL ? PyObject_IsTrue(attr) : -1;
            Py_XDECREF(attr);
            return is_true >= 0 ? PyBool_FromLong(!is_true) : NULL;
        }
        if (strcmp(spec->name, "int_max_str_digits") == 0) {
            return PyLong_FromLong(PyInterpreterState_Get()->max_str_digits);
        }
    }

    void *member = (char *)object + spec->offset;
    switch (spec->type) {
    case MEMBER_INT:
    case MEMBER_UINT:
        return PyLong_FromLong(*(int *)member);
    case MEMBER_BOOL:
        return PyBool_FromLong(*(int *)member != 0);
    case MEMBER_ULONG:
        return PyLong_FromUnsignedLong(*(unsigned long *)member);
    case MEMBER_WSTR:
    case MEMBER_WSTR_OPT: {
        wchar_t *wstr = *(wchar_t **)member;
        return wstr != NULL ? PyUnicode_FromWideChar(wstr, -1) : Py_NewRef(Py_None);
    }
    case MEMBER_WSTR_LIST:
        if (strcmp(spec->name, "xoptions") == 0) {
            return xoptions_dict(member);
        }
        else {
            const PyWideStringList *list = member;
            PyObject *tuple = PyTuple_New(list->length);
            for (Py_ssize_t i = 0; tuple != NULL && i < list->length; i++) {
                PyObject *item = PyUnicode_FromWideChar(list->items[i], -1);
                if (item == NULL) {
                    Py_CLEAR(tuple);
                    break;
                }
                PyTuple_SET_ITEM(tuple, i, item);
            }
            return tuple;
        }
    default:
        PyErr_Format(PyExc_SystemError, "unknown config option type: %s", spec->name);
        return NULL;
    }
}

static void
unknown_name_error(const char *name)
{
    PyErr_Format(PyExc_ValueError, "unknown config option name: %s", name);
}

PyObject *
PyConfig_Get(const char *name)
{
    const config_spec *spec = find_spec(PYCONFIG_SPEC, name);
    if (spec != NULL) {
        return config_get(&PyInterpreterState_Get()->config, spec, 1);
    }
    if ((spec = find_spec(PYPRECONFIG_SPEC, name)) != NULL) {
        PyPreConfig preconfig = preconfig_of(&PyInterpreterState_Get()->config);
        return config_get(&preconfig, spec, 0);
    }
    unknown_name_error(name);
    return NULL;
}

int
PyConfig_GetInt(const char *name, int *value)
{
    PyObject *obj = PyConfig_Get(name);
    if (obj == NULL) {
        return -1;
    }
    if (!PyLong_Check(obj)) {
        Py_DECREF(obj);
        PyErr_Format(PyExc_TypeError, "config option %s is not an int", name);
        return -1;
    }
    int as_int = PyLong_AsInt(obj);
    Py_DECREF(obj);
    if (as_int == -1 && PyErr_Occurred()) {
        PyErr_Format(PyExc_OverflowError, "config option %s value does not fit into a C int", name);
        return -1;
    }
    *value = as_int;
    return 0;
}

PyObject *
PyConfig_Names(void)
{
    PyObject *names = PyList_New(0);
    for (int table = 0; names != NULL && table < 2; table++) {
        for (const config_spec *spec = table == 0 ? PYCONFIG_SPEC : PYPRECONFIG_SPEC;
             spec->name != NULL; spec++) {
            if (spec->visibility == INIT_ONLY) {
                continue;
            }
            PyObject *name = PyUnicode_FromString(spec->name);
            int err = name == NULL || PyList_Append(names, name) < 0;
            Py_XDECREF(name);
            if (err) {
                Py_CLEAR(names);
                break;
            }
        }
    }
    PyObject *frozen = names != NULL ? PyFrozenSet_New(names) : NULL;
    Py_XDECREF(names);
    return frozen;
}

/* --- PyConfig_Set() ----------------------------------------------- */

/* sys.flags has 21 fields: the first 18 are the tuple's items, gil,
   thread_inherit_context and context_aware_warnings live in the dict. */
#define SYS_FLAG_FIELDS 21
#define SYS_FLAG_HIDDEN 18

/* A fresh sys.flags with field pos replaced (_PySys_SetFlagObj). */
static int
sys_set_flag(Py_ssize_t pos, PyObject *value)
{
    static const char *const hidden[] = { "gil", "thread_inherit_context", "context_aware_warnings" };
    PyObject *old = sys_attr("flags");
    PyObject *fresh = old != NULL ? PyStructSequence_New(Py_TYPE(old)) : NULL;
    for (Py_ssize_t i = 0; fresh != NULL && i < SYS_FLAG_FIELDS; i++) {
        PyObject *item;
        if (i == pos) {
            item = Py_NewRef(value);
        }
        else if (i < SYS_FLAG_HIDDEN) {
            item = Py_NewRef(PyTuple_GET_ITEM(old, i));
        }
        else {
            item = PyObject_GetAttrString(old, hidden[i - SYS_FLAG_HIDDEN]);
        }
        if (item == NULL) {
            Py_CLEAR(fresh);
            break;
        }
        PyStructSequence_SetItem(fresh, i, item);       /* steals the item */
    }
    int err = fresh != NULL ? PySys_SetObject("flags", fresh) : -1;
    Py_XDECREF(fresh);
    Py_XDECREF(old);
    return err;
}

/* _PySys_SetIntMaxStrDigits: the flags field, the interpreter's limit and the
   config member, with the 0-or-640 rule. */
static int
sys_set_int_max_str_digits(int maxdigits)
{
    if (maxdigits != 0 && maxdigits < 640) {
        PyErr_Format(PyExc_ValueError, "maxdigits must be >= %d or 0 for unlimited", 640);
        return -1;
    }
    PyObject *value = PyLong_FromLong(maxdigits);
    int err = value != NULL ? sys_set_flag(17, value) : -1;
    Py_XDECREF(value);
    if (err < 0) {
        return -1;
    }
    PyInterpreterState *interp = PyInterpreterState_Get();
    interp->max_str_digits = maxdigits;
    interp->config.int_max_str_digits = maxdigits;
    return 0;
}

/* config_set_sys_flag: sys.flags.FIELD, the config member, and for
   write_bytecode sys.dont_write_bytecode; bools clamp to 0 or 1. */
static int
config_set_sys_flag(const config_spec *spec, int int_value)
{
    PyConfig *config = &PyInterpreterState_Get()->config;
    if (spec->type == MEMBER_BOOL && int_value != 0) {
        int_value = 1;
    }
    PyObject *value = spec->sys.flag_setter != NULL ? spec->sys.flag_setter(int_value)
                                                    : PyLong_FromLong(int_value);
    if (value == NULL) {
        return -1;
    }
    int err = sys_set_flag(spec->sys.flag_index, value);
    if (err == 0) {
        *(int *)((char *)config + spec->offset) = int_value;
        if (strcmp(spec->name, "write_bytecode") == 0) {
            err = PySys_SetObject("dont_write_bytecode", value);
        }
    }
    Py_DECREF(value);
    return err;
}

int
PyConfig_Set(const char *name, PyObject *value)
{
    if (PySys_Audit("cpython.PyConfig_Set", "sO", name, value) < 0) {
        return -1;
    }

    const config_spec *spec = find_spec(PYCONFIG_SPEC, name);
    if (spec == NULL) {
        spec = find_spec(PYPRECONFIG_SPEC, name);
        if (spec == NULL) {
            unknown_name_error(name);
            return -1;
        }
    }
    if (spec->visibility != PUBLIC) {
        PyErr_Format(PyExc_ValueError, "cannot set read-only option %s", name);
        return -1;
    }

    int int_value = 0;
    int has_int_value = 0;
    switch (spec->type) {
    case MEMBER_INT:
    case MEMBER_UINT:
    case MEMBER_BOOL:
        if (!PyLong_Check(value)) {
            PyErr_Format(PyExc_TypeError, "expected int or bool, got %T", value);
            return -1;
        }
        int_value = PyLong_AsInt(value);
        if (int_value == -1 && PyErr_Occurred()) {
            return -1;
        }
        if (int_value < 0 && spec->type != MEMBER_INT) {
            PyErr_Format(PyExc_ValueError, "value must be >= 0");
            return -1;
        }
        has_int_value = 1;
        break;

    case MEMBER_ULONG:
        /* only hash_seed uses this type, and it is read-only */
        PyErr_Format(PyExc_ValueError, "cannot set option %s", name);
        return -1;

    case MEMBER_WSTR:
        if (!PyUnicode_CheckExact(value)) {
            PyErr_Format(PyExc_TypeError, "expected str, got %T", value);
            return -1;
        }
        break;

    case MEMBER_WSTR_OPT:
        if (value != Py_None && !PyUnicode_CheckExact(value)) {
            PyErr_Format(PyExc_TypeError, "expected str or None, got %T", value);
            return -1;
        }
        break;

    case MEMBER_WSTR_LIST:
        if (strcmp(spec->name, "xoptions") != 0) {
            if (!PyList_Check(value)) {
                PyErr_Format(PyExc_TypeError, "expected list[str], got %T", value);
                return -1;
            }
            for (Py_ssize_t i = 0; i < PyList_GET_SIZE(value); i++) {
                PyObject *item = PyList_GET_ITEM(value, i);
                if (!PyUnicode_Check(item)) {
                    PyErr_Format(PyExc_TypeError, "expected str, list item %zd has type %T", i, item);
                    return -1;
                }
            }
        }
        else {
            /* xoptions type is dict[str, str | bool] */
            if (!PyDict_Check(value)) {
                PyErr_Format(PyExc_TypeError, "expected dict[str, str | bool], got %T", value);
                return -1;
            }
            Py_ssize_t pos = 0;
            PyObject *key, *item;
            while (PyDict_Next(value, &pos, &key, &item)) {
                if (!PyUnicode_Check(key)) {
                    PyErr_Format(PyExc_TypeError, "expected str, got dict key type %T", key);
                    return -1;
                }
                if (!PyUnicode_Check(item) && !PyBool_Check(item)) {
                    PyErr_Format(PyExc_TypeError, "expected str or bool, got dict value type %T", key);
                    return -1;
                }
            }
        }
        break;

    default:
        PyErr_Format(PyExc_SystemError, "unknown config option type: %s", spec->name);
        return -1;
    }

    if (spec->global_var.ptr != NULL) {
        int global = spec->global_var.not ? !int_value : int_value;
        *spec->global_var.ptr = global;
    }

    if (spec->sys.attr != NULL) {
        /* the sys attribute holds the value; the config member does not */
        return PySys_SetObject(spec->sys.attr, value);
    }
    if (has_int_value) {
        if (spec->sys.flag_index >= 0) {
            return config_set_sys_flag(spec, int_value);
        }
        if (strcmp(spec->name, "int_max_str_digits") == 0) {
            return sys_set_int_max_str_digits(int_value);
        }
        *(int *)((char *)&PyInterpreterState_Get()->config + spec->offset) = int_value;
        return 0;
    }
    PyErr_Format(PyExc_ValueError, "cannot set option %s", name);
    return -1;
}

/* --- _Py_GetConfigsAsDict() --------------------------------------- */

static int
dict_put(PyObject *dict, const char *key, PyObject *value)
{
    int err = value != NULL ? PyDict_SetItemString(dict, key, value) : -1;
    Py_XDECREF(value);
    return err;
}

/* _PyConfig_AsDict and _PyPreConfig_AsDict: every member of a spec table. */
static PyObject *
table_as_dict(const void *object, const config_spec *table)
{
    PyObject *dict = PyDict_New();
    for (const config_spec *spec = table; dict != NULL && spec->name != NULL; spec++) {
        if (dict_put(dict, spec->name, config_get(object, spec, 0)) < 0) {
            Py_CLEAR(dict);
        }
    }
    return dict;
}

/* _Py_GetGlobalVariablesAsDict: the legacy globals as a dict. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
static PyObject *
globals_as_dict(void)
{
    PyObject *dict = PyDict_New();
    int err = dict != NULL
        && (dict_put(dict, "Py_FileSystemDefaultEncoding",
                     Py_FileSystemDefaultEncoding != NULL
                         ? PyUnicode_FromString(Py_FileSystemDefaultEncoding) : Py_NewRef(Py_None)) < 0
            || dict_put(dict, "Py_HasFileSystemDefaultEncoding",
                        PyLong_FromLong(Py_HasFileSystemDefaultEncoding)) < 0
            || dict_put(dict, "Py_FileSystemDefaultEncodeErrors",
                        Py_FileSystemDefaultEncodeErrors != NULL
                            ? PyUnicode_FromString(Py_FileSystemDefaultEncodeErrors) : Py_NewRef(Py_None)) < 0
            || dict_put(dict, "Py_UTF8Mode", PyLong_FromLong(Py_UTF8Mode)) < 0
            || dict_put(dict, "Py_DebugFlag", PyLong_FromLong(Py_DebugFlag)) < 0
            || dict_put(dict, "Py_VerboseFlag", PyLong_FromLong(Py_VerboseFlag)) < 0
            || dict_put(dict, "Py_QuietFlag", PyLong_FromLong(Py_QuietFlag)) < 0
            || dict_put(dict, "Py_InteractiveFlag", PyLong_FromLong(Py_InteractiveFlag)) < 0
            || dict_put(dict, "Py_InspectFlag", PyLong_FromLong(Py_InspectFlag)) < 0
            || dict_put(dict, "Py_OptimizeFlag", PyLong_FromLong(Py_OptimizeFlag)) < 0
            || dict_put(dict, "Py_NoSiteFlag", PyLong_FromLong(Py_NoSiteFlag)) < 0
            || dict_put(dict, "Py_BytesWarningFlag", PyLong_FromLong(Py_BytesWarningFlag)) < 0
            || dict_put(dict, "Py_FrozenFlag", PyLong_FromLong(Py_FrozenFlag)) < 0
            || dict_put(dict, "Py_IgnoreEnvironmentFlag", PyLong_FromLong(Py_IgnoreEnvironmentFlag)) < 0
            || dict_put(dict, "Py_DontWriteBytecodeFlag", PyLong_FromLong(Py_DontWriteBytecodeFlag)) < 0
            || dict_put(dict, "Py_NoUserSiteDirectory", PyLong_FromLong(Py_NoUserSiteDirectory)) < 0
            || dict_put(dict, "Py_UnbufferedStdioFlag", PyLong_FromLong(Py_UnbufferedStdioFlag)) < 0
            || dict_put(dict, "Py_HashRandomizationFlag", PyLong_FromLong(Py_HashRandomizationFlag)) < 0
            || dict_put(dict, "Py_IsolatedFlag", PyLong_FromLong(Py_IsolatedFlag)) < 0);
    if (err) {
        Py_CLEAR(dict);
    }
    return dict;
}
#pragma GCC diagnostic pop

/* _testinternalcapi.get_configs: the globals, the preconfig and the config. */
LU_EXPORT PyObject *
_Py_GetConfigsAsDict(void)
{
    PyObject *result = PyDict_New();
    PyInterpreterState *interp = PyInterpreterState_Get();
    PyPreConfig preconfig = preconfig_of(&interp->config);
    int err = result != NULL
        && (dict_put(result, "global_config", globals_as_dict()) < 0
            || dict_put(result, "pre_config", table_as_dict(&preconfig, PYPRECONFIG_SPEC)) < 0
            || dict_put(result, "config", table_as_dict(&interp->config, PYCONFIG_SPEC)) < 0);
    if (err) {
        Py_CLEAR(result);
    }
    return result;
}
