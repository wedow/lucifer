/* PyConfig (Python/initconfig.c): the status type, wide-string lists, the
   command line and the environment read into a PyConfig, and the one parser
   for the runtime's own -X options (FOUNDATIONS 2.7, 2.13).

   Supported on the command line: -B -E -I -S -s -u -P -q -v -d -O -b -i,
   -W arg, -X opt[=value], --version, and a .pyc path with its arguments.
   -c, -m and source scripts need the compiler and stay unsupported. */

#include "jit/jit.h"
#include "lucifer.h"

#include <errno.h>
#include <langinfo.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#define PYTHON_VERSION "3.14.10"
#define INT_MAX_STR_DIGITS_DEFAULT 4300
#define INT_MAX_STR_DIGITS_THRESHOLD 640

/* PyStatus */

PyStatus
PyStatus_Ok(void)
{
    return (PyStatus){ ._type = _PyStatus_TYPE_OK };
}

PyStatus
PyStatus_Error(const char *err_msg)
{
    return (PyStatus){ ._type = _PyStatus_TYPE_ERROR, .err_msg = err_msg };
}

PyStatus
PyStatus_NoMemory(void)
{
    return PyStatus_Error("memory allocation failed");
}

PyStatus
PyStatus_Exit(int exitcode)
{
    return (PyStatus){ ._type = _PyStatus_TYPE_EXIT, .exitcode = exitcode };
}

int
PyStatus_IsError(PyStatus err)
{
    return err._type == _PyStatus_TYPE_ERROR;
}

int
PyStatus_IsExit(PyStatus err)
{
    return err._type == _PyStatus_TYPE_EXIT;
}

int
PyStatus_Exception(PyStatus err)
{
    return err._type != _PyStatus_TYPE_OK;
}

/* A failed startup: an error prints Python's fatal-error line and exits 1;
   an exit status leaves with its code (its message was printed already). */
void
Py_ExitStatusException(PyStatus status)
{
    if (status._type == _PyStatus_TYPE_EXIT) {
        exit(status.exitcode);
    }
    fprintf(stderr, "Fatal Python error: %s%s%s\n", status.func != NULL ? status.func : "",
            status.func != NULL ? ": " : "", status.err_msg != NULL ? status.err_msg : "");
    exit(1);
}

/* An exit status after a message on stderr. */
static PyStatus
exit_with(int code, const char *format, const char *a, const char *b)
{
    fprintf(stderr, format, a, b);
    return PyStatus_Exit(code);
}

/* Locale decoding: UTF-8, with undecodable bytes as the lone surrogates
   U+DC80..U+DCFF (surrogateescape), as Py_DecodeLocale does in UTF-8 mode. */

wchar_t *
Py_DecodeLocale(const char *arg, size_t *size)
{
    size_t n = strlen(arg);
    wchar_t *out = PyMem_RawMalloc((n + 1) * sizeof(wchar_t));
    if (out == NULL) {
        return NULL;
    }
    size_t o = 0;
    for (size_t i = 0; i < n;) {
        unsigned char c = (unsigned char)arg[i];
        int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        wchar_t ch = len == 1 ? c : len == 2 ? c & 0x1f : len == 3 ? c & 0x0f : c & 0x07;
        int ok = len > 0 && i + (size_t)len <= n;
        for (int k = 1; ok && k < len; k++) {
            unsigned char cc = (unsigned char)arg[i + (size_t)k];
            ok = (cc & 0xc0) == 0x80;
            ch = (ch << 6) | (cc & 0x3f);
        }
        ok = ok && !(len == 2 && ch < 0x80) && !(len == 3 && ch < 0x800)
            && !(len == 4 && (ch < 0x10000 || ch > 0x10ffff)) && !(ch >= 0xd800 && ch < 0xe000);
        if (ok) {
            out[o++] = ch;
            i += (size_t)len;
        }
        else {
            out[o++] = (wchar_t)(0xdc00 + c);
            i++;
        }
    }
    out[o] = 0;
    if (size != NULL) {
        *size = o;
    }
    return out;
}

/* Py_EncodeLocale's inverse of the above. */
char *
Py_EncodeLocale(const wchar_t *text, size_t *error_pos)
{
    size_t n = wcslen(text);
    char *out = PyMem_Malloc(n * 4 + 1);
    if (out == NULL) {
        return NULL;
    }
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned long ch = (unsigned long)text[i];
        if (ch >= 0xdc80 && ch <= 0xdcff) {
            out[o++] = (char)(ch - 0xdc00);
        }
        else if (ch < 0x80) {
            out[o++] = (char)ch;
        }
        else if (ch < 0x800) {
            out[o++] = (char)(0xc0 | (ch >> 6));
            out[o++] = (char)(0x80 | (ch & 0x3f));
        }
        else if (ch < 0x10000) {
            out[o++] = (char)(0xe0 | (ch >> 12));
            out[o++] = (char)(0x80 | ((ch >> 6) & 0x3f));
            out[o++] = (char)(0x80 | (ch & 0x3f));
        }
        else {
            out[o++] = (char)(0xf0 | (ch >> 18));
            out[o++] = (char)(0x80 | ((ch >> 12) & 0x3f));
            out[o++] = (char)(0x80 | ((ch >> 6) & 0x3f));
            out[o++] = (char)(0x80 | (ch & 0x3f));
        }
    }
    out[o] = 0;
    return out;
}

/* PyWideStringList */

PyStatus
PyWideStringList_Insert(PyWideStringList *list, Py_ssize_t index, const wchar_t *item)
{
    if (index < 0 || index > list->length) {
        index = list->length;
    }
    wchar_t *copy = PyMem_RawMalloc((wcslen(item) + 1) * sizeof(wchar_t));
    wchar_t **items = PyMem_RawRealloc(list->items, (size_t)(list->length + 1) * sizeof(wchar_t *));
    if (copy == NULL || items == NULL) {
        PyMem_RawFree(copy);
        return PyStatus_NoMemory();
    }
    wcscpy(copy, item);
    memmove(items + index + 1, items + index, (size_t)(list->length - index) * sizeof(wchar_t *));
    items[index] = copy;
    list->items = items;
    list->length++;
    return PyStatus_Ok();
}

PyStatus
PyWideStringList_Append(PyWideStringList *list, const wchar_t *item)
{
    return PyWideStringList_Insert(list, list->length, item);
}

static void
list_clear(PyWideStringList *list)
{
    for (Py_ssize_t i = 0; i < list->length; i++) {
        PyMem_RawFree(list->items[i]);
    }
    PyMem_RawFree(list->items);
    list->items = NULL;
    list->length = 0;
}

static PyStatus
list_set(PyWideStringList *list, Py_ssize_t length, wchar_t *const *items)
{
    PyWideStringList fresh = { 0, NULL };
    for (Py_ssize_t i = 0; i < length; i++) {
        PyStatus status = PyWideStringList_Append(&fresh, items[i]);
        if (PyStatus_Exception(status)) {
            list_clear(&fresh);
            return status;
        }
    }
    list_clear(list);
    *list = fresh;
    return PyStatus_Ok();
}

/* Strings of a PyConfig */

PyStatus
PyConfig_SetString(PyConfig *config, wchar_t **config_str, const wchar_t *str)
{
    wchar_t *copy = NULL;
    if (str != NULL) {
        copy = PyMem_RawMalloc((wcslen(str) + 1) * sizeof(wchar_t));
        if (copy == NULL) {
            return PyStatus_NoMemory();
        }
        wcscpy(copy, str);
    }
    PyMem_RawFree(*config_str);
    *config_str = copy;
    return PyStatus_Ok();
}

PyStatus
PyConfig_SetBytesString(PyConfig *config, wchar_t **config_str, const char *str)
{
    wchar_t *wide = Py_DecodeLocale(str, NULL);
    if (wide == NULL) {
        return PyStatus_NoMemory();
    }
    PyStatus status = PyConfig_SetString(config, config_str, wide);
    PyMem_RawFree(wide);
    return status;
}

PyStatus
PyConfig_SetWideStringList(PyConfig *config, PyWideStringList *list, Py_ssize_t length,
                           wchar_t **items)
{
    return list_set(list, length, items);
}

PyStatus
PyConfig_SetArgv(PyConfig *config, Py_ssize_t argc, wchar_t *const *argv)
{
    config->parse_argv = 1;
    return list_set(&config->argv, argc, argv);
}

PyStatus
PyConfig_SetBytesArgv(PyConfig *config, Py_ssize_t argc, char *const *argv)
{
    PyWideStringList wide = { 0, NULL };
    for (Py_ssize_t i = 0; i < argc; i++) {
        wchar_t *w = Py_DecodeLocale(argv[i], NULL);
        PyStatus status = w != NULL ? PyWideStringList_Append(&wide, w) : PyStatus_NoMemory();
        PyMem_RawFree(w);
        if (PyStatus_Exception(status)) {
            list_clear(&wide);
            return status;
        }
    }
    PyStatus status = PyConfig_SetArgv(config, argc, wide.items);
    list_clear(&wide);
    return status;
}

#define STRING_FIELDS(X) \
    X(dump_refs_file) X(filesystem_encoding) X(filesystem_errors) X(pycache_prefix) \
    X(stdio_encoding) X(stdio_errors) X(check_hash_pycs_mode) X(program_name) \
    X(pythonpath_env) X(home) X(platlibdir) X(stdlib_dir) X(executable) X(base_executable) \
    X(prefix) X(base_prefix) X(exec_prefix) X(base_exec_prefix) X(run_command) X(run_module) \
    X(run_filename) X(sys_path_0)
#define LIST_FIELDS(X) \
    X(orig_argv) X(argv) X(xoptions) X(warnoptions) X(module_search_paths)

void
PyConfig_Clear(PyConfig *config)
{
#define CLEAR_STRING(name) PyMem_RawFree(config->name); config->name = NULL;
#define CLEAR_LIST(name) list_clear(&config->name);
    STRING_FIELDS(CLEAR_STRING)
    LIST_FIELDS(CLEAR_LIST)
#undef CLEAR_STRING
#undef CLEAR_LIST
}

enum { INIT_PYTHON = 2, INIT_ISOLATED = 3 };

static void
config_init(PyConfig *config, int isolated)
{
    memset(config, 0, sizeof *config);
    config->_config_init = isolated ? INIT_ISOLATED : INIT_PYTHON;
    config->isolated = isolated;
    config->use_environment = !isolated;
    config->dev_mode = -1;
    config->install_signal_handlers = !isolated;
    config->use_hash_seed = -1;
    config->faulthandler = -1;
    config->tracemalloc = -1;
    config->perf_profiling = -1;
    config->remote_debug = -1;
    config->import_time = -1;
    config->code_debug_ranges = 1;
    config->parse_argv = !isolated;
    config->site_import = !isolated;
    config->write_bytecode = 1;
    config->user_site_directory = !isolated;
    config->configure_c_stdio = !isolated;
    config->buffered_stdio = 1;
    config->use_frozen_modules = -1;
    config->safe_path = isolated;
    config->int_max_str_digits = -1;
    config->thread_inherit_context = -1;
    config->context_aware_warnings = -1;
    config->cpu_count = -1;
    config->enable_gil = -1;
    config->tlbc_enabled = 1;
    config->pathconfig_warnings = !isolated;
    config->module_search_paths_set = 0;
    config->_install_importlib = 1;
    config->_init_main = 1;
}

void
PyConfig_InitPythonConfig(PyConfig *config)
{
    config_init(config, 0);
}

void
PyConfig_InitIsolatedConfig(PyConfig *config)
{
    config_init(config, 1);
}

/* Copies every field of src into dst, which starts out cleared. */
static PyStatus
config_copy(PyConfig *dst, const PyConfig *src)
{
    PyConfig fresh = *src;
#define NULL_STRING(name) fresh.name = NULL;
#define NULL_LIST(name) fresh.name = (PyWideStringList){ 0, NULL };
    STRING_FIELDS(NULL_STRING)
    LIST_FIELDS(NULL_LIST)
#undef NULL_STRING
#undef NULL_LIST
    PyStatus status = PyStatus_Ok();
#define COPY_STRING(name) \
    if (!PyStatus_Exception(status)) status = PyConfig_SetString(&fresh, &fresh.name, src->name);
#define COPY_LIST(name) \
    if (!PyStatus_Exception(status)) status = list_set(&fresh.name, src->name.length, src->name.items);
    STRING_FIELDS(COPY_STRING)
    LIST_FIELDS(COPY_LIST)
#undef COPY_STRING
#undef COPY_LIST
    if (PyStatus_Exception(status)) {
        PyConfig_Clear(&fresh);
        return status;
    }
    *dst = fresh;
    return PyStatus_Ok();
}

/* The command line */

static PyStatus
usage_error(const PyConfig *config, const char *message)
{
    const wchar_t *w = config->orig_argv.length > 0 ? config->orig_argv.items[0] : L"lucifer";
    char *program = Py_EncodeLocale(w, NULL);
    PyStatus status = exit_with(2, "%s\nusage: %s [option] ... file.pyc [arg] ...\n"
                                "Try `python -h' for more information.\n", message,
                                program != NULL ? program : "lucifer");
    PyMem_Free(program);
    return status;
}

/* One -X value or a PYTHON* variable as a wide "name[=value]" string. */
static PyStatus
add_xoption(PyConfig *config, const char *text)
{
    wchar_t *w = Py_DecodeLocale(text, NULL);
    PyStatus status = w != NULL ? PyWideStringList_Append(&config->xoptions, w) : PyStatus_NoMemory();
    PyMem_RawFree(w);
    return status;
}

static PyStatus
add_bytes(PyWideStringList *list, const char *text)
{
    wchar_t *w = Py_DecodeLocale(text, NULL);
    PyStatus status = w != NULL ? PyWideStringList_Append(list, w) : PyStatus_NoMemory();
    PyMem_RawFree(w);
    return status;
}

/* argv for -c, -m and for a program read from standard input:
   ["-c", "-m" or "", rest...]. */
static PyStatus
set_command_argv(PyConfig *config, wchar_t **rest, Py_ssize_t count)
{
    PyWideStringList argv = { 0, NULL };
    PyStatus status = PyWideStringList_Append(&argv, config->run_command != NULL ? L"-c"
                                              : config->run_module != NULL ? L"-m" : L"");
    for (Py_ssize_t k = 0; !PyStatus_Exception(status) && k < count; k++) {
        status = PyWideStringList_Append(&argv, rest[k]);
    }
    if (!PyStatus_Exception(status)) {
        list_clear(&config->argv);
        config->argv = argv;
    }
    return status;
}

/* config_parse_cmdline: the options, then the script and its arguments.
   argv is rebuilt to start at the script. */
static PyStatus
parse_cmdline(PyConfig *config)
{
    Py_ssize_t argc = config->argv.length;
    wchar_t **argv = config->argv.items;
    PyStatus status = list_set(&config->orig_argv, argc, argv);
    if (PyStatus_Exception(status)) {
        return status;
    }
    Py_ssize_t i = 1;
    for (; i < argc && argv[i][0] == L'-' && argv[i][1] != L'\0'; i++) {
        char *arg = Py_EncodeLocale(argv[i], NULL);
        if (arg == NULL) {
            return PyStatus_NoMemory();
        }
        int stop = strcmp(arg, "--") == 0;
        if (strcmp(arg, "--version") == 0) {
            PyMem_Free(arg);
            printf("Python %s\n", PYTHON_VERSION);
            return PyStatus_Exit(0);
        }
        for (const char *c = arg + 1; !stop && *c; c++) {
            const char *value = NULL;
            switch (*c) {
            case 'B': config->write_bytecode = 0; break;
            case 'E': config->use_environment = 0; break;
            case 'I': config->isolated = 1; break;
            case 'S': config->site_import = 0; break;
            case 's': config->user_site_directory = 0; break;
            case 'u': config->buffered_stdio = 0; break;
            case 'P': config->safe_path = 1; break;
            case 'q': config->quiet++; break;
            case 'v': config->verbose++; break;
            case 'd': config->parser_debug++; break;
            case 'O': config->optimization_level++; break;
            case 'b': config->bytes_warning++; break;
            case 'i': config->inspect = 1; config->interactive++; break;
            case 'V':
                printf("Python %s\n", PYTHON_VERSION);
                PyMem_Free(arg);
                return PyStatus_Exit(0);
            case 'X': case 'W': {
                char option = *c;
                if (c[1] != '\0') {
                    value = c + 1;
                }
                else if (i + 1 < argc) {
                    PyMem_Free(arg);
                    arg = Py_EncodeLocale(argv[++i], NULL);
                    value = arg;
                }
                else {
                    PyMem_Free(arg);
                    return usage_error(config, option == 'X' ? "Argument expected for the -X option"
                                                             : "Argument expected for the -W option");
                }
                status = option == 'X' ? add_xoption(config, value) : add_bytes(&config->warnoptions, value);
                if (PyStatus_Exception(status)) {
                    PyMem_Free(arg);
                    return status;
                }
                c = value + strlen(value) - 1;         /* the value ends the argument */
                break;
            }
            case 'c':
            case 'm': {
                /* The command or module is the rest of this argument or the next one, and ends the options. */
                int is_module = *c == 'm';
                const char *command = c[1] != '\0' ? c + 1 : NULL;
                char *next = NULL;
                if (command == NULL && i + 1 < argc) {
                    next = Py_EncodeLocale(argv[++i], NULL);
                    command = next;
                }
                if (command == NULL) {
                    PyMem_Free(arg);
                    return usage_error(config, is_module ? "Argument expected for the -m option"
                                                         : "Argument expected for the -c option");
                }
                wchar_t *w = Py_DecodeLocale(command, NULL);
                PyMem_Free(next);
                PyMem_Free(arg);
                if (w == NULL) {
                    return PyStatus_NoMemory();
                }
                status = PyConfig_SetString(config, is_module ? &config->run_module : &config->run_command, w);
                PyMem_RawFree(w);
                if (!PyStatus_Exception(status)) {
                    status = set_command_argv(config, argv + i + 1, argc - i - 1);
                }
                return status;
            }
            default: {
                char message[32];
                snprintf(message, sizeof message, "Unknown option: -%c", *c);
                PyMem_Free(arg);
                return usage_error(config, message);
            }
            }
        }
        PyMem_Free(arg);
        if (stop) {
            i++;
            break;
        }
    }
    if (i >= argc) {
        if (isatty(0)) {
            return usage_error(config, "lucifer: a program to run is required (no interactive mode yet)");
        }
        return set_command_argv(config, NULL, 0);       /* standard input; argv[0] is '' */
    }
    status = PyConfig_SetString(config, &config->run_filename, argv[i]);
    if (PyStatus_Exception(status)) {
        return status;
    }
    /* argv: the script and what follows. */
    PyWideStringList rest = { 0, NULL };
    status = list_set(&rest, argc - i, argv + i);
    if (!PyStatus_Exception(status)) {
        list_clear(&config->argv);
        config->argv = rest;
    }
    return status;
}

/* Environment */

static int
env_set(const char *name)
{
    const char *v = getenv(name);
    return v != NULL && *v != '\0';
}

static PyStatus
read_env_flags(PyConfig *config)
{
    if (env_set("PYTHONDONTWRITEBYTECODE")) { config->write_bytecode = 0; }
    if (env_set("PYTHONUNBUFFERED")) { config->buffered_stdio = 0; }
    if (env_set("PYTHONNOUSERSITE")) { config->user_site_directory = 0; }
    if (env_set("PYTHONSAFEPATH")) { config->safe_path = 1; }
    if (env_set("PYTHONINSPECT")) { config->inspect = 1; }
    if (env_set("PYTHONVERBOSE") && config->verbose == 0) { config->verbose = 1; }
    if (env_set("PYTHONDEBUG") && config->parser_debug == 0) { config->parser_debug = 1; }
    if (env_set("PYTHONOPTIMIZE") && config->optimization_level == 0) {
        config->optimization_level = atoi(getenv("PYTHONOPTIMIZE")) > 0 ? atoi(getenv("PYTHONOPTIMIZE")) : 1;
    }
    PyStatus status = PyStatus_Ok();
    if (env_set("PYTHONPYCACHEPREFIX") && config->pycache_prefix == NULL) {
        status = PyConfig_SetBytesString(config, &config->pycache_prefix, getenv("PYTHONPYCACHEPREFIX"));
    }
    if (!PyStatus_Exception(status) && env_set("PYTHONPATH")) {
        status = PyConfig_SetBytesString(config, &config->pythonpath_env, getenv("PYTHONPATH"));
    }
    if (!PyStatus_Exception(status) && env_set("PYTHONWARNINGS")) {
        /* PYTHONWARNINGS options come before those of -W. */
        char *copy = strdup(getenv("PYTHONWARNINGS"));
        PyWideStringList env = { 0, NULL };
        for (char *save, *tok = strtok_r(copy, ",", &save); tok != NULL && !PyStatus_Exception(status);
             tok = strtok_r(NULL, ",", &save)) {
            status = *tok != '\0' ? add_bytes(&env, tok) : PyStatus_Ok();
        }
        free(copy);
        for (Py_ssize_t i = 0; i < config->warnoptions.length && !PyStatus_Exception(status); i++) {
            status = PyWideStringList_Append(&env, config->warnoptions.items[i]);
        }
        if (!PyStatus_Exception(status)) {
            list_clear(&config->warnoptions);
            config->warnoptions = env;
        }
        else {
            list_clear(&env);
        }
    }
    return status;
}

/* config_init_hash_seed: PYTHONHASHSEED, unless -E or -I. */
static PyStatus
read_hash_seed(PyConfig *config)
{
    const char *env = config->use_environment ? getenv("PYTHONHASHSEED") : NULL;
    if (config->use_hash_seed < 0) {
        config->use_hash_seed = 0;
        config->hash_seed = 0;
        if (env != NULL && *env != '\0' && strcmp(env, "random") != 0) {
            char *end;
            errno = 0;
            unsigned long value = strtoul(env, &end, 10);
            if (*end != '\0' || errno != 0 || value > 4294967295UL || *env == '-') {
                return (PyStatus){ ._type = _PyStatus_TYPE_ERROR, .func = "config_init_hash_seed",
                    .err_msg = "PYTHONHASHSEED must be \"random\" or an integer in range [0; 4294967295]" };
            }
            config->use_hash_seed = 1;
            config->hash_seed = value;
        }
    }
    return PyStatus_Ok();
}

/* The value of the last -X name[=value] for `name`: NULL when absent, "" for
   a bare name. The result lives in the config. */
static const wchar_t *
xoption(const PyConfig *config, const wchar_t *name)
{
    size_t n = wcslen(name);
    for (Py_ssize_t i = config->xoptions.length; i-- > 0;) {
        const wchar_t *x = config->xoptions.items[i];
        if (wcsncmp(x, name, n) == 0 && (x[n] == L'\0' || x[n] == L'=')) {
            return x[n] == L'=' ? x + n + 1 : x + n;
        }
    }
    return NULL;
}

/* config_init_int_max_str_digits: -X int_max_str_digits, then the variable. */
static PyStatus
read_int_max_str_digits(PyConfig *config)
{
    if (config->int_max_str_digits >= 0) {
        return PyStatus_Ok();
    }
    config->int_max_str_digits = INT_MAX_STR_DIGITS_DEFAULT;
    char *text = NULL;
    const wchar_t *x = xoption(config, L"int_max_str_digits");
    int from_option = x != NULL;
    if (from_option) {
        text = Py_EncodeLocale(x, NULL);
    }
    else if (config->use_environment && env_set("PYTHONINTMAXSTRDIGITS")) {
        wchar_t *w = Py_DecodeLocale(getenv("PYTHONINTMAXSTRDIGITS"), NULL);
        text = w != NULL ? Py_EncodeLocale(w, NULL) : NULL;
        PyMem_RawFree(w);
    }
    else {
        return PyStatus_Ok();
    }
    if (text == NULL) {
        return PyStatus_NoMemory();
    }
    char *end;
    errno = 0;
    long value = strtol(text, &end, 10);
    int ok = *text != '\0' && *end == '\0' && errno == 0 && value <= INT_MAX
        && (value == 0 || value >= INT_MAX_STR_DIGITS_THRESHOLD);
    PyMem_Free(text);
    if (!ok) {
        return (PyStatus){ ._type = _PyStatus_TYPE_ERROR, .func = "config_init_int_max_str_digits",
            .err_msg = from_option
                ? "-X int_max_str_digits: invalid limit; must be >= 640 or 0 for unlimited."
                : "PYTHONINTMAXSTRDIGITS: invalid limit; must be >= 640 or 0 for unlimited." };
    }
    config->int_max_str_digits = (int)value;
    return PyStatus_Ok();
}

static PyStatus
read_executable(PyConfig *config)
{
    if (config->executable != NULL) {
        return PyStatus_Ok();
    }
    char path[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
    if (n <= 0) {
        return PyStatus_Ok();
    }
    path[n] = '\0';
    return PyConfig_SetBytesString(config, &config->executable, path);
}

/* getpath's landmark search: from the executable's directory upward, the
   first directory D holding lib/python3.14t/os.py (an installed tree) or
   build/lib/python3.14t/os.py (a build tree). D (or D/build) is the prefix. */
static PyStatus
read_stdlib(PyConfig *config)
{
    char dir[PATH_MAX], probe[PATH_MAX + 64];
    size_t n = config->executable == NULL ? (size_t)-1 : wcstombs(dir, config->executable, sizeof dir);
    if (n == 0 || n >= sizeof dir) {
        return PyStatus_Ok();
    }
    static const char *const layouts[] = { "", "/build" };
    for (char *slash; (slash = strrchr(dir, '/')) != NULL; *slash = '\0') {
        for (int i = 0; i < 2; i++) {
            snprintf(probe, sizeof probe, "%.*s%s/lib/python3.14t/os.py", (int)(slash - dir), dir, layouts[i]);
            if (access(probe, R_OK) != 0) {
                continue;
            }
            snprintf(probe, sizeof probe, "%.*s%s", (int)(slash - dir), dir, layouts[i]);
            PyStatus status = PyConfig_SetBytesString(config, &config->prefix, probe);
            wchar_t **others[] = { &config->base_prefix, &config->exec_prefix, &config->base_exec_prefix };
            for (int j = 0; j < 3 && !PyStatus_Exception(status); j++) {
                status = PyConfig_SetString(config, others[j], config->prefix);
            }
            strcat(probe, "/lib/python3.14t");
            return PyStatus_Exception(status) ? status
                 : PyConfig_SetBytesString(config, &config->stdlib_dir, probe);
        }
    }
    return PyStatus_Ok();
}

/* config_init_perf_profiling: PYTHONPERFSUPPORT and -X perf ask for the
   perf map trampolines (1), PYTHON_PERF_JIT_SUPPORT and -X perf_jit for
   the jitdump ones (2), each later one winning; else none. */
static void
read_perf_profiling(PyConfig *config)
{
    if (config->perf_profiling >= 0) {
        return;
    }
    config->perf_profiling = 0;
    const char *env = config->use_environment ? getenv("PYTHONPERFSUPPORT") : NULL;
    if (env != NULL && atoi(env) != 0) {
        config->perf_profiling = 1;
    }
    if (xoption(config, L"perf") != NULL) {
        config->perf_profiling = 1;
    }
    env = config->use_environment ? getenv("PYTHON_PERF_JIT_SUPPORT") : NULL;
    if (env != NULL && atoi(env) != 0) {
        config->perf_profiling = 2;
    }
    if (xoption(config, L"perf_jit") != NULL) {
        config->perf_profiling = 2;
    }
}

/* config_init_warnoptions: the dev mode filter first, then PYTHONWARNINGS
   and -W (already there, in that order), then the BytesWarning filter. */
static PyStatus
finish_warnoptions(PyConfig *config)
{
    if (config->dev_mode < 0) {
        const wchar_t *x = xoption(config, L"dev");
        config->dev_mode = x != NULL || (config->use_environment && env_set("PYTHONDEVMODE")
                                         && strcmp(getenv("PYTHONDEVMODE"), "0") != 0);
    }
    PyStatus status = PyStatus_Ok();
    if (config->dev_mode) {
        status = PyWideStringList_Insert(&config->warnoptions, 0, L"default");
    }
    if (!PyStatus_Exception(status) && config->bytes_warning > 0) {
        status = PyWideStringList_Append(&config->warnoptions,
                                         config->bytes_warning > 1 ? L"error::BytesWarning"
                                                                   : L"default::BytesWarning");
    }
    return status;
}

/* _Py_abspath and _Py_normpath: the program file as an absolute path whose
   "." and ".." segments are collapsed, without resolving symbolic links. */
static PyStatus
run_filename_abspath(PyConfig *config)
{
    if (config->run_filename == NULL || config->run_command != NULL || wcscmp(config->run_filename, L"-") == 0) {
        return PyStatus_Ok();
    }
    char *path = Py_EncodeLocale(config->run_filename, NULL);
    if (path == NULL) {
        return PyStatus_NoMemory();
    }
    char joined[PATH_MAX * 2] = "";
    if (path[0] != '/') {
        if (getcwd(joined, PATH_MAX) == NULL) {
            joined[0] = '\0';
        }
        strcat(joined, "/");
    }
    snprintf(joined + strlen(joined), sizeof joined - strlen(joined), "%s", path);
    PyMem_Free(path);
    char normal[PATH_MAX * 2];
    size_t length = 0;
    for (char *save, *part = strtok_r(joined, "/", &save); part != NULL; part = strtok_r(NULL, "/", &save)) {
        if (strcmp(part, ".") == 0) {
            continue;
        }
        if (strcmp(part, "..") == 0) {
            while (length > 0 && normal[--length] != '/') {
            }
            continue;
        }
        normal[length++] = '/';
        length += (size_t)snprintf(normal + length, sizeof normal - length, "%s", part);
    }
    normal[length] = '\0';
    return PyConfig_SetBytesString(config, &config->run_filename, length > 0 ? normal : "/");
}

/* _PyPathConfig_ComputeSysPath0: the directory of the program file, with
   symbolic links resolved, or "" for -c and standard input; none when the
   path is safe. */
static PyStatus
compute_sys_path_0(PyConfig *config)
{
    if (config->safe_path || config->sys_path_0 != NULL || !config->parse_argv) {
        return PyStatus_Ok();
    }
    if (config->run_module != NULL) {
        char cwd[PATH_MAX];                 /* -m puts the working directory first */
        return PyConfig_SetBytesString(config, &config->sys_path_0, getcwd(cwd, sizeof cwd) ? cwd : "");
    }
    if (config->run_command != NULL || config->run_filename == NULL || wcscmp(config->run_filename, L"-") == 0) {
        return PyConfig_SetString(config, &config->sys_path_0, L"");
    }
    char *path = Py_EncodeLocale(config->run_filename, NULL);
    if (path == NULL) {
        return PyStatus_NoMemory();
    }
    char resolved[PATH_MAX];
    if (realpath(path, resolved) == NULL) {
        snprintf(resolved, sizeof resolved, "%s", path);
    }
    PyMem_Free(path);
    char *slash = strrchr(resolved, '/');
    if (slash == NULL) {
        resolved[0] = '\0';
    }
    else if (slash == resolved) {
        slash[1] = '\0';
    }
    else {
        *slash = '\0';
    }
    return PyConfig_SetBytesString(config, &config->sys_path_0, resolved);
}

/* The codeset of the user's LC_CTYPE locale (nl_langinfo_l of a locale made
   from the environment, whatever the process's own locale is), or UTF-8. */
static void
locale_codeset(char out[64])
{
    locale_t locale = newlocale(LC_CTYPE_MASK, "", (locale_t)0);
    const char *codeset = locale != (locale_t)0 ? nl_langinfo_l(CODESET, locale) : "";
    snprintf(out, 64, "%s", codeset[0] != '\0' ? codeset : "UTF-8");
    if (locale != (locale_t)0) {
        freelocale(locale);
    }
}

/* config_init_stdio_encoding: PYTHONIOENCODING's encoding:errors, then the
   UTF-8 mode or the locale's encoding, with surrogateescape in the UTF-8 and
   ASCII locales. Settled here, the kernel's init_streams takes the config's
   word for both. */
static PyStatus
read_stdio(PyConfig *config)
{
    if (config->stdio_encoding != NULL && config->stdio_errors != NULL) {
        return PyStatus_Ok();
    }
    char codeset[64];
    locale_codeset(codeset);
    if (config->use_environment && env_set("PYTHONIOENCODING")) {
        char *copy = strdup(getenv("PYTHONIOENCODING"));
        char *errors = copy != NULL ? strchr(copy, ':') : NULL;
        if (errors != NULL) {
            *errors++ = '\0';               /* the encoding ends at the colon */
            if (*errors == '\0') {
                errors = NULL;
            }
        }
        PyStatus status = copy != NULL ? PyStatus_Ok() : PyStatus_NoMemory();
        if (!PyStatus_Exception(status) && copy[0] != '\0') {
            if (config->stdio_encoding == NULL) {
                status = PyConfig_SetBytesString(config, &config->stdio_encoding, copy);
            }
            if (errors == NULL) {
                errors = "strict";      /* latin1 behaves as latin1:strict */
            }
        }
        if (!PyStatus_Exception(status) && config->stdio_errors == NULL && errors != NULL) {
            status = PyConfig_SetBytesString(config, &config->stdio_errors, errors);
        }
        free(copy);
        if (PyStatus_Exception(status)) {
            return status;
        }
    }
    int utf8 = lu_utf8_mode(config);
    PyStatus status = PyStatus_Ok();
    if (config->stdio_encoding == NULL) {
        status = utf8 ? PyConfig_SetString(config, &config->stdio_encoding, L"utf-8")
                      : PyConfig_SetBytesString(config, &config->stdio_encoding, codeset);
    }
    if (!PyStatus_Exception(status) && config->stdio_errors == NULL) {
        status = PyConfig_SetString(config, &config->stdio_errors,
                                    utf8 || strcmp(codeset, "ANSI_X3.4-1968") == 0
                                        ? L"surrogateescape" : L"strict");
    }
    return status;
}

/* The string members PyConfig_Get reads as settled values: getpath's program
   name and the hash-based pyc default, the filesystem always UTF-8 here
   (Py_EncodeLocale), and the stdio encoding and errors. */
static PyStatus
read_strings(PyConfig *config)
{
    PyStatus status = read_stdio(config);
    if (!PyStatus_Exception(status) && config->program_name == NULL) {
        status = PyConfig_SetString(config, &config->program_name,
                                    config->orig_argv.length > 0 ? config->orig_argv.items[0]
                                                                 : L"lucifer");
    }
    if (!PyStatus_Exception(status) && config->check_hash_pycs_mode == NULL) {
        status = PyConfig_SetString(config, &config->check_hash_pycs_mode, L"default");
    }
    if (!PyStatus_Exception(status) && config->filesystem_encoding == NULL) {
        status = PyConfig_SetString(config, &config->filesystem_encoding, L"utf-8");
    }
    if (!PyStatus_Exception(status) && config->filesystem_errors == NULL) {
        status = PyConfig_SetString(config, &config->filesystem_errors, L"surrogateescape");
    }
    return status;
}

PyStatus
PyConfig_Read(PyConfig *config)
{
    PyStatus status = PyStatus_Ok();
    if (config->parse_argv && config->orig_argv.length == 0) {
        status = parse_cmdline(config);
        if (PyStatus_Exception(status)) {
            return status;
        }
    }
    if (config->isolated) {
        config->use_environment = 0;
        config->user_site_directory = 0;
        config->safe_path = 1;
    }
    if (config->use_environment) {
        status = read_env_flags(config);
    }
    if (!PyStatus_Exception(status) && config->parse_argv) {
        status = run_filename_abspath(config);
    }
    if (!PyStatus_Exception(status)) {
        status = compute_sys_path_0(config);
    }
    if (!PyStatus_Exception(status)) {
        status = read_hash_seed(config);
    }
    if (!PyStatus_Exception(status)) {
        status = read_int_max_str_digits(config);
    }
    if (!PyStatus_Exception(status)) {
        read_perf_profiling(config);
        status = read_executable(config);
    }
    if (!PyStatus_Exception(status)) {
        status = read_stdlib(config);
    }
    if (!PyStatus_Exception(status)) {
        status = finish_warnoptions(config);
    }
    if (!PyStatus_Exception(status)) {
        status = read_strings(config);
    }
    if (config->thread_inherit_context < 0) {
        config->thread_inherit_context = 1;            /* the free-threaded default */
    }
    if (config->context_aware_warnings < 0) {
        config->context_aware_warnings = 1;
    }
    return status;
}

/* The preconfig's UTF-8 mode: -X utf8[=0|1], PYTHONUTF8, else on when the
   LC_CTYPE locale is "C" or "POSIX" (PEP 540). */
int
lu_utf8_mode(const PyConfig *config)
{
    const wchar_t *x = xoption(config, L"utf8");
    if (x != NULL) {
        return wcscmp(x, L"0") != 0;
    }
    if (config->use_environment && env_set("PYTHONUTF8")) {
        return strcmp(getenv("PYTHONUTF8"), "0") != 0;
    }
    char *saved = setlocale(LC_CTYPE, NULL);
    saved = saved != NULL ? strdup(saved) : NULL;
    const char *name = setlocale(LC_CTYPE, "");
    int c_locale = name == NULL || strcmp(name, "C") == 0 || strcmp(name, "POSIX") == 0;
    if (saved != NULL) {
        setlocale(LC_CTYPE, saved);
        free(saved);
    }
    return c_locale;
}

/* The runtime's own -X options */

static PyStatus
stress_error(const char *what, const char *item)
{
    return exit_with(2, "lucifer: -X lucifer_stress: %s '%s'\n"
                     "usage: -X lucifer_stress=KIND[:N][,KIND[:N]...][,seed:S] with KIND gc or deopt\n",
                     what, item);
}

static int
parse_count(const char *text, unsigned long *out)
{
    char *end;
    errno = 0;
    unsigned long v = strtoul(text, &end, 10);
    if (*text == '\0' || *text == '-' || *end != '\0' || errno != 0) {
        return -1;
    }
    *out = v;
    return 0;
}

/* lucifer_stress=KIND[:N][,...][,seed:S]; N defaults to 1 and must be above 0. */
static PyStatus
parse_stress(const char *value, struct lu_stress *out)
{
    char *copy = strdup(value);
    PyStatus status = *value != '\0' ? PyStatus_Ok() : stress_error("needs a kind, got", "");
    for (char *save, *item = strtok_r(copy, ",", &save); item != NULL && !PyStatus_Exception(status);
         item = strtok_r(NULL, ",", &save)) {
        char *colon = strchr(item, ':');
        unsigned long n = 1;
        if (colon != NULL) {
            *colon = '\0';
        }
        int is_seed = strcmp(item, "seed") == 0;
        if (colon != NULL && parse_count(colon + 1, &n) < 0) {
            status = stress_error("bad number in", colon + 1);
        }
        else if (is_seed && colon == NULL) {
            status = stress_error("seed needs a number:", item);
        }
        else if (is_seed) {
            out->seed = n;
            out->seeded = 1;
        }
        else if (n == 0) {
            status = stress_error("count must be above 0 in", item);
        }
        else if (strcmp(item, "gc") == 0) {
            out->gc = n;
        }
        else if (strcmp(item, "deopt") == 0) {
            out->deopt = n;
        }
        else if (strcmp(item, "sre") == 0 && colon == NULL) {
            /* lib/modules/_sre.py reads it from sys._xoptions */
        }
        else {
            status = stress_error("unknown kind", item);
        }
    }
    free(copy);
    return status;
}

/* lucifer_handoff[=SEED] */
static PyStatus
parse_handoff(const char *value, struct lu_handoff *out)
{
    out->on = 1;
    if (*value == '\0') {
        return PyStatus_Ok();
    }
    if (parse_count(value, &out->seed) < 0) {
        return exit_with(2, "lucifer: -X lucifer_handoff: bad seed '%s'\n%s", value,
                         "usage: -X lucifer_handoff[=SEED] with a non-negative number\n");
    }
    out->seeded = 1;
    return PyStatus_Ok();
}

/* lucifer_jit=off|on|always; on when the option is absent. */
static PyStatus
parse_jit(const char *value, struct lu_jit_options *out)
{
    int mode = strcmp(value, "off") == 0 ? LU_JIT_OFF : strcmp(value, "on") == 0 ? LU_JIT_ON
        : strcmp(value, "always") == 0 ? LU_JIT_ALWAYS : -1;
    if (mode >= 0) {
        out->mode = mode;
        return PyStatus_Ok();
    }
    return exit_with(2, "lucifer: -X lucifer_jit: bad mode '%s'\n%s", value,
                     "usage: -X lucifer_jit=off|on|always\n");
}

/* Every lucifer_* -X option in one place; an unknown one is an error, so
   that a typo cannot silently turn a check off. */
PyStatus
lu_runtime_options(const PyConfig *config)
{
    struct lu_stress stress = { 0 };
    struct lu_handoff handoff = { 0 };
    struct lu_jit_options jit = { .mode = 1 };
    PyStatus status = PyStatus_Ok();
    for (Py_ssize_t i = 0; i < config->xoptions.length && !PyStatus_Exception(status); i++) {
        char *text = Py_EncodeLocale(config->xoptions.items[i], NULL);
        if (text == NULL) {
            return PyStatus_NoMemory();
        }
        if (strncmp(text, "lucifer_", 8) == 0) {
            char *eq = strchr(text, '=');
            const char *value = eq != NULL ? eq + 1 : "";
            if (eq != NULL) {
                *eq = '\0';
            }
            if (strcmp(text, "lucifer_stress") == 0) {
                status = parse_stress(value, &stress);
            }
            else if (strcmp(text, "lucifer_handoff") == 0) {
                status = parse_handoff(value, &handoff);
            }
            else if (strcmp(text, "lucifer_jit") == 0) {
                status = parse_jit(value, &jit);
            }
            else if (strcmp(text, "lucifer_jit_stats") == 0 && eq == NULL) {
                jit.stats = 1;
            }
            else {
                status = exit_with(2, "lucifer: unknown option -X %s\n", text, "");
            }
        }
        PyMem_Free(text);
    }
    /* Without a backend (FOUNDATIONS 2.11) everything runs interpreted:
       said once when compiling everything was asked for. */
    if (!PyStatus_Exception(status) && jit.mode != LU_JIT_OFF && lu_jit_backend() == NULL) {
        if (jit.mode == LU_JIT_ALWAYS) {
            fprintf(stderr, "lucifer: -X lucifer_jit=always: no backend; running interpreted\n");
        }
        jit.mode = LU_JIT_OFF;
    }
    if (!PyStatus_Exception(status)) {
        _PyRuntime.stress = stress;
        _PyRuntime.handoff = handoff;
        jit.debug = getenv("LUCIFER_JIT_DEBUG") != NULL;
        jit.dump = getenv("LUCIFER_JIT_DUMP") != NULL;
        jit.only = getenv("LUCIFER_JIT_ONLY");
        _PyRuntime.jit = jit;
    }
    return status;
}

/* Py_InitializeFromConfig's copy, read: the status is OK, an error or an exit. */
PyStatus
lu_config_read_copy(PyConfig *dst, const PyConfig *src)
{
    PyStatus status = config_copy(dst, src);
    if (!PyStatus_Exception(status)) {
        status = PyConfig_Read(dst);
    }
    if (PyStatus_Exception(status)) {
        PyConfig_Clear(dst);
    }
    return status;
}
