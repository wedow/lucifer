/* perf support (FOUNDATIONS 2.12; Python/perf_trampoline.c and
   Python/perf_jit_trampoline.c, CPython 3.14).

   Two backends name machine code for a native profiler: "perf" appends
   `ADDR SIZE NAME` lines to /tmp/perf-PID.map, and "perf_jit" writes the
   jitdump format to /tmp/jit-PID.dump, with each region's unwinding rules,
   for `perf record --call-graph=dwarf` and `perf inject --jit`. The
   optimizing tier names its units through the active backend, and the
   PyUnstable_*PerfMap* API writes the map.

   The stack trampolines (-X perf, -X perf_jit, sys.activate_stack_trampoline)
   are a PEP 523 hook that runs each Python frame through its code object's
   own copy of the target's few-instruction trampoline, so a native stack
   holds one py::QUALNAME:FILENAME frame per Python frame the interpreter
   runs. Compiled code stays enabled under the hook: a unit is named after
   its code, and the frames it inlines have no trampolines.

   A copy belongs to an epoch, the process id and a count of activations,
   written beside its code: a forked child, or a new backend, starts a new
   map and new copies, so a map names only what its process ran. */

#include "interp/perf.h"
#include "interp/frame.h"
#include "jit/target.h"

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

typedef PyObject *(*evaluator)(PyThreadState *, struct _PyInterpreterFrame *, int);
typedef PyObject *(*trampoline)(PyThreadState *, struct _PyInterpreterFrame *, int, evaluator);

/* A trampoline's chunk: its code, room for the unwinding data perf_jit
   describes it with, and its epoch in the last word. */
#define CHUNK 128
#define ARENA (CHUNK * 512)

static struct {
    struct lu_lock lock;        /* registered, so a fork resets it */
    int registered;
    int backend;                /* enum lu_perf_backend of the trampolines; NONE when off */
    uint32_t activations;       /* backend changes: each starts new copies */
    int persist;                /* PyUnstable_PerfTrampoline_SetPersistAfterFork */
    uint64_t current;           /* epoch(), kept current at its changes: getpid is a system call */
    FILE *out[2];               /* the map and the dump, by backend - 1 */
    pid_t out_pid[2];           /* the process each was opened in */
    void *dump_mark;            /* the dump's first page, mapped executable: perf records the mapping */
    uint64_t code_id;
    char *arena;                /* the current arena's next free chunk */
    size_t arena_left;
    uint64_t arena_epoch;
    Py_ssize_t extra_index;     /* the code-extra slot that holds each code's copy */
} perf = { .lock = { PTHREAD_MUTEX_INITIALIZER }, .extra_index = -1 };

/* A forked child keeps its parent's copies only when they persist. */
static uint64_t
epoch(void)
{
    return (perf.persist ? 0 : (uint64_t)getpid() << 32) | perf.activations;
}

static void
child_after_fork(void)
{
    perf.current = epoch();
}

/* Output */

/* Closes the output of `kind` (a backend), if this process opened it. */
static void
close_out(int kind)
{
    int i = kind - 1;
    if (perf.out[i] != NULL && perf.out_pid[i] == getpid()) {
        fclose(perf.out[i]);
        if (kind == LU_PERF_JIT && perf.dump_mark != NULL) {
            munmap(perf.dump_mark, (size_t)sysconf(_SC_PAGESIZE));
        }
    }
    perf.out[i] = NULL;
    if (kind == LU_PERF_JIT) {
        perf.dump_mark = NULL;
    }
}

static uint64_t
now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

/* The jitdump header: magic "JiTD", version 1. */
struct dump_header {
    uint32_t magic, version, size, elf_mach, reserved, pid;
    uint64_t timestamp, flags;
};

static FILE *
open_dump(void)
{
    char path[64];
    snprintf(path, sizeof path, "/tmp/jit-%d.dump", (int)getpid());
    FILE *out = fopen(path, "w+");
    if (out == NULL) {
        return NULL;
    }
    void *mark = mmap(NULL, (size_t)sysconf(_SC_PAGESIZE), PROT_READ | PROT_EXEC, MAP_PRIVATE, fileno(out), 0);
    if (mark == MAP_FAILED) {
        fclose(out);
        return NULL;
    }
    perf.dump_mark = mark;
    struct dump_header h = { .magic = 0x4A695444, .version = 1, .size = sizeof h,
                             .elf_mach = lu_target.elf_machine, .pid = (uint32_t)getpid(),
                             .timestamp = now_ns() };
    fwrite(&h, sizeof h, 1, out);
    return out;
}

/* Appends a file's lines to the open map: a child's copy of its parent's. */
static int
copy_into(FILE *out, const char *path)
{
    FILE *in = fopen(path, "r");
    if (in == NULL) {
        return -1;
    }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        fwrite(buf, 1, n, out);
    }
    fclose(in);
    fflush(out);
    return 0;
}

/* The output of `kind` for this process, opened on first use: a forked
   child starts its own, from a copy of its parent's map when copies
   persist. Under the lock. */
static FILE *
output(int kind)
{
    int i = kind - 1;
    pid_t pid = getpid();
    if (perf.out[i] != NULL && perf.out_pid[i] == pid) {
        return perf.out[i];
    }
    int forked = perf.out[i] != NULL;
    close_out(kind);
    if (kind == LU_PERF_MAP) {
        char path[64];
        snprintf(path, sizeof path, "/tmp/perf-%d.map", (int)pid);
        perf.out[i] = fopen(path, "a");
        if (perf.out[i] != NULL && forked && perf.persist) {
            snprintf(path, sizeof path, "/tmp/perf-%d.map", (int)getppid());
            copy_into(perf.out[i], path);
        }
    }
    else {
        perf.out[i] = open_dump();
    }
    perf.out_pid[i] = pid;
    return perf.out[i];
}

static void
lock(void)
{
    if (!__atomic_load_n(&perf.registered, __ATOMIC_ACQUIRE)) {
        pthread_mutex_lock(&perf.lock.mutex);
        if (!perf.registered) {
            lu_lock_register(&perf.lock);
            pthread_atfork(NULL, NULL, child_after_fork);
            __atomic_store_n(&perf.registered, 1, __ATOMIC_RELEASE);
        }
        return;
    }
    pthread_mutex_lock(&perf.lock.mutex);
}

static void
unlock(void)
{
    pthread_mutex_unlock(&perf.lock.mutex);
}

/* DWARF call frame information for perf_jit: one CIE and one FDE per
   region, which perf inject places after the region's code in the ELF
   file it makes, followed by an .eh_frame_hdr. */

/* Appends `n` bytes of `body` as one length-prefixed record, padded with
   DW_CFA_nop to 8 bytes. */
static uint8_t *
record(uint8_t *p, const uint8_t *body, size_t n)
{
    uint8_t *start = p;
    p += 4;
    memcpy(p, body, n);
    p += n;
    while ((p - start) % 8) {
        *p++ = 0;
    }
    uint32_t len = (uint32_t)(p - start - 4);
    memcpy(start, &len, 4);
    return p;
}

struct eh_frame_hdr {
    uint8_t version, eh_frame_ptr_enc, fde_count_enc, table_enc;
    int32_t eh_frame_ptr, fde_count, from, to;
};

static size_t
round8(size_t n)
{
    return (n + 7) & ~(size_t)7;
}

/* Writes the unwinding record of a region of `size` bytes at `addr` whose
   rules are `cfi`, then its load record. */
static void
write_dump(FILE *out, const void *addr, size_t size, const char *name, const uint8_t *cfi, size_t ncfi)
{
    uint8_t frame[256], fde[128];
    uint8_t *p = record(frame, lu_target.cie, (size_t)lu_target.ncie);
    size_t cie_size = (size_t)(p - frame);
    /* The FDE: the CIE pointer, the code's start (PC-relative, from where
       perf inject puts it), its length, no augmentation, the rules. */
    uint8_t *q = fde;
    uint32_t cie_ptr = (uint32_t)(cie_size + 4);
    memcpy(q, &cie_ptr, 4);
    int32_t pc = -(int32_t)(round8(size) + cie_size + 8);
    memcpy(q + 4, &pc, 4);
    uint32_t len = (uint32_t)size;
    memcpy(q + 8, &len, 4);
    q += 12;
    *q++ = 0;
    memcpy(q, cfi, ncfi);
    q += ncfi;
    p = record(p, fde, (size_t)(q - fde));
    size_t eh_size = (size_t)(p - frame);
    struct eh_frame_hdr hdr = {
        .version = 1, .eh_frame_ptr_enc = 0x1b, .fde_count_enc = 0x03, .table_enc = 0x3b,
        .eh_frame_ptr = -(int32_t)(eh_size + 4), .fde_count = 1,
        .from = -(int32_t)(round8(size) + eh_size), .to = -(int32_t)(eh_size - cie_size),
    };
    struct {
        uint32_t event, size;
        uint64_t timestamp, unwind_size, hdr_size, mapped_size;
    } unwind = { .event = 4, .timestamp = now_ns(), .unwind_size = sizeof hdr + eh_size,
                 .hdr_size = sizeof hdr, .mapped_size = (sizeof hdr + eh_size + 15) & ~(size_t)15 };
    size_t content = sizeof unwind + sizeof hdr + eh_size;
    unwind.size = (uint32_t)round8(content);
    static const uint8_t zeros[8];
    fwrite(&unwind, sizeof unwind, 1, out);
    fwrite(frame, eh_size, 1, out);
    fwrite(&hdr, sizeof hdr, 1, out);
    fwrite(zeros, round8(content) - content, 1, out);
    struct {
        uint32_t event, size;
        uint64_t timestamp;
        uint32_t pid, tid;
        uint64_t vma, code_addr, code_size, code_id;
    } load = { .event = 0, .timestamp = now_ns(), .pid = (uint32_t)getpid(),
               .tid = (uint32_t)gettid(), .vma = (uint64_t)(uintptr_t)addr,
               .code_addr = (uint64_t)(uintptr_t)addr, .code_size = size, .code_id = ++perf.code_id };
    load.size = (uint32_t)(sizeof load + strlen(name) + 1 + size);
    fwrite(&load, sizeof load, 1, out);
    fwrite(name, strlen(name) + 1, 1, out);
    fwrite(addr, size, 1, out);
    fflush(out);
}

/* Names a region in the backend `kind`. Under the lock. */
static void
write_entry(int kind, const void *addr, size_t size, const char *name, const uint8_t *cfi, size_t ncfi)
{
    FILE *out = kind != LU_PERF_NONE ? output(kind) : NULL;
    if (out == NULL) {
        return;
    }
    if (kind == LU_PERF_MAP) {
        fprintf(out, "%" PRIxPTR " %zx %s\n", (uintptr_t)addr, size, name);
        fflush(out);
    }
    else {
        write_dump(out, addr, size, name, cfi, ncfi);
    }
}

/* "py::QUALNAME:FILENAME", and "@PC" when pc >= 0, in malloc'd memory;
   NULL when a name has no UTF-8 form. Made before the lock is taken: an
   exception runs Python, whose frames may need trampolines. The exception
   state is left as it was (a frame may be entered to throw). */
static char *
code_name(PyCodeObject *co, int pc)
{
    PyObject *exc = PyErr_GetRaisedException();
    const char *qualname = PyUnicode_AsUTF8(co->co_qualname);
    const char *filename = qualname != NULL ? PyUnicode_AsUTF8(co->co_filename) : NULL;
    char *name = NULL;
    if (filename != NULL) {
        size_t n = strlen(qualname) + strlen(filename) + 32;
        name = malloc(n);
        if (name != NULL) {
            snprintf(name, n, pc >= 0 ? "py::%s:%s@%d" : "py::%s:%s", qualname, filename, pc);
        }
    }
    PyErr_SetRaisedException(exc);
    return name;
}

void
lu_perf_unit(const void *addr, size_t size, PyCodeObject *code, int pc)
{
    int kind = __atomic_load_n(&perf.backend, __ATOMIC_ACQUIRE);
    if (kind == LU_PERF_NONE) {
        return;
    }
    char *name = code_name(code, pc);
    if (name == NULL) {
        return;
    }
    lock();
    write_entry(kind, addr, size, name, lu_target.fp_cfi, (size_t)lu_target.nfp_cfi);
    unlock();
    free(name);
}

/* The trampolines */

/* A chunk of an arena of the current epoch. An arena is filled with
   copies and their epoch, then made executable. Under the lock. */
static char *
new_chunk(void)
{
    uint64_t e = perf.current;
    if (perf.arena_left < CHUNK || perf.arena_epoch != e) {
        char *mem = mmap(NULL, ARENA, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem == MAP_FAILED) {
            return NULL;
        }
        for (size_t i = 0; i < ARENA; i += CHUNK) {
            memcpy(mem + i, lu_target.trampoline, (size_t)lu_target.ntrampoline);
            memcpy(mem + i + CHUNK - 8, &e, 8);
        }
        mprotect(mem, ARENA, PROT_READ | PROT_EXEC);
        perf.arena = mem;
        perf.arena_left = ARENA;
        perf.arena_epoch = e;
    }
    char *chunk = perf.arena;
    perf.arena += CHUNK;
    perf.arena_left -= CHUNK;
    return chunk;
}

/* The code object's copy for this epoch, made and named on first use. */
static trampoline
trampoline_of(PyCodeObject *co)
{
    void *found;
    PyUnstable_Code_GetExtra((PyObject *)co, perf.extra_index, &found);
    char *t = found;
    if (t != NULL && *(uint64_t *)(t + CHUNK - 8) == __atomic_load_n(&perf.current, __ATOMIC_RELAXED)) {
        return (trampoline)(void *)t;
    }
    char *name = code_name(co, -1);
    lock();
    PyUnstable_Code_GetExtra((PyObject *)co, perf.extra_index, &found);
    t = found;
    if (t == NULL || *(uint64_t *)(t + CHUNK - 8) != perf.current) {
        t = new_chunk();
        if (t != NULL) {
            if (name != NULL) {
                write_entry(perf.backend, t, (size_t)lu_target.ntrampoline, name,
                            lu_target.fp_cfi, (size_t)lu_target.nfp_cfi);
            }
            if (PyUnstable_Code_SetExtra((PyObject *)co, perf.extra_index, t) < 0) {
                PyErr_Clear();      /* not kept: the next call makes another */
            }
        }
    }
    unlock();
    free(name);
    return (trampoline)(void *)t;
}

PyObject *
lu_perf_eval(PyThreadState *ts, struct _PyInterpreterFrame *f, int throwflag)
{
    trampoline t = trampoline_of(((struct lu_frame *)f)->code);
    return t != NULL ? t(ts, f, throwflag, _PyEval_EvalFrameDefault)
                     : _PyEval_EvalFrameDefault(ts, f, throwflag);
}

/* Activation */

static int
activate(PyInterpreterState *interp, int backend)
{
    if (lu_target.trampoline == NULL) {
        PyErr_SetString(PyExc_ValueError, "perf trampoline not available");
        return -1;
    }
    if (interp->eval_frame != NULL && interp->eval_frame != lu_perf_eval) {
        PyErr_SetString(PyExc_RuntimeError,
                        "Trampoline cannot be initialized as a custom eval frame is already present");
        return -1;
    }
    lock();
    if (perf.extra_index < 0) {
        /* Each code's copy lives in a code-extra slot (perf_trampoline.c). */
        perf.extra_index = PyUnstable_Eval_RequestCodeExtraIndex(NULL);
        if (perf.extra_index < 0) {
            unlock();
            PyErr_SetString(PyExc_RuntimeError, "no code-extra slot left for the perf trampolines");
            return -1;
        }
    }
    if (perf.backend != backend) {
        perf.activations++;
        __atomic_store_n(&perf.current, epoch(), __ATOMIC_RELAXED);
        __atomic_store_n(&perf.backend, backend, __ATOMIC_RELEASE);
    }
    unlock();
    _PyInterpreterState_SetEvalFrameFunc(interp, lu_perf_eval);
    return 0;
}

int
lu_perf_start(PyInterpreterState *interp)
{
    int mode = interp->config.perf_profiling;
    return mode == 1 ? activate(interp, LU_PERF_MAP) : mode == 2 ? activate(interp, LU_PERF_JIT) : 0;
}

static PyObject *
sys_activate_stack_trampoline(PyObject *m, PyObject *arg)
{
    const char *name = PyUnicode_Check(arg) ? PyUnicode_AsUTF8(arg) : NULL;
    if (name == NULL) {
        if (!PyErr_Occurred()) {
            PyErr_Format(PyExc_TypeError, "activate_stack_trampoline() argument must be str, not %.50s",
                         Py_TYPE(arg)->tp_name);
        }
        return NULL;
    }
    int backend = !strcmp(name, "perf") ? LU_PERF_MAP : !strcmp(name, "perf_jit") ? LU_PERF_JIT : 0;
    if (backend == 0) {
        PyErr_Format(PyExc_ValueError, "invalid backend: %s", name);
        return NULL;
    }
    if (activate(PyInterpreterState_Get(), backend) < 0) {
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
sys_deactivate_stack_trampoline(PyObject *m, PyObject *unused)
{
    PyInterpreterState *interp = PyInterpreterState_Get();
    if (interp->eval_frame == lu_perf_eval) {
        _PyInterpreterState_SetEvalFrameFunc(interp, NULL);
        __atomic_store_n(&perf.backend, LU_PERF_NONE, __ATOMIC_RELEASE);
    }
    Py_RETURN_NONE;
}

static PyObject *
sys_is_stack_trampoline_active(PyObject *m, PyObject *unused)
{
    return PyBool_FromLong(PyInterpreterState_Get()->eval_frame == lu_perf_eval);
}

PyMethodDef lu_sys_perf_methods[] = {
    { "activate_stack_trampoline", sys_activate_stack_trampoline, METH_O, NULL },
    { "deactivate_stack_trampoline", sys_deactivate_stack_trampoline, METH_NOARGS, NULL },
    { "is_stack_trampoline_active", sys_is_stack_trampoline_active, METH_NOARGS, NULL },
    { NULL, NULL, 0, NULL },
};

/* The C API */

int
PyUnstable_PerfMapState_Init(void)
{
    lock();
    int ok = output(LU_PERF_MAP) != NULL;
    unlock();
    return ok ? 0 : -1;
}

void
PyUnstable_PerfMapState_Fini(void)
{
    lock();
    close_out(LU_PERF_MAP);
    unlock();
}

int
PyUnstable_WritePerfMapEntry(const void *code_addr, unsigned int code_size, const char *entry_name)
{
    lock();
    int ok = output(LU_PERF_MAP) != NULL;
    if (ok) {
        write_entry(LU_PERF_MAP, code_addr, code_size, entry_name, NULL, 0);
    }
    unlock();
    return ok ? 0 : -1;
}

int
PyUnstable_CopyPerfMapFile(const char *parent_filename)
{
    lock();
    FILE *out = output(LU_PERF_MAP);
    int r = out != NULL ? copy_into(out, parent_filename) : -1;
    unlock();
    return r;
}

int
PyUnstable_PerfTrampoline_CompileCode(PyCodeObject *co)
{
    if (__atomic_load_n(&perf.backend, __ATOMIC_ACQUIRE) != LU_PERF_NONE) {
        trampoline_of(co);
    }
    return 0;
}

int
PyUnstable_PerfTrampoline_SetPersistAfterFork(int enable)
{
    lock();
    perf.persist = enable;
    __atomic_store_n(&perf.current, epoch(), __ATOMIC_RELAXED);
    unlock();
    return enable;
}
