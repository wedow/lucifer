/* Executable memory for compiled units (runtime/jit/target.h).

   Code is written through one mapping and run through another of the
   same shared pages (an alias mremap makes), so that no page is writable
   and executable at once. Units take ranges from chunks that stay mapped
   for the life of the process: a freed range goes back to the free list
   and a later unit reuses it, so freeing makes no system call (a
   collection frees dead units in bulk), and units share pages instead of
   rounding each up to one. Each new chunk is as large as all before it,
   so a process maps about twice the code it ever held at once.

   A range is freed only once no activation can be in its unit
   (runtime/jit/jit.c), and reused by a unit published after that: a
   release store its runners read through the code's list of units, so
   they see the new code (x86 keeps instruction fetch coherent with
   stores).

   A perf map (-X perf) cannot say that a range was freed, so while one is
   written freed ranges are kept, not reused: each range keeps the one
   name the map gives it. A jitdump's records are timestamped, and reuse
   is safe under it.

   A child of os.fork shares the parent's pages, so it copies each
   chunk's taken ranges into pages of its own at the same addresses
   before it runs (runtime/thread/fork.c). A child that only execs (the
   subprocess module's) runs no compiled code and copies nothing. */
#define _GNU_SOURCE
#include "jit/target.h"
#include "interp/perf.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>

#define ALIGN 64                /* a cache line */
#define FIRST_CHUNK (1u << 20)

struct chunk {
    char *rx, *rw;              /* the pages as run, and as written */
    size_t size;
    struct chunk *next;
};

/* A free range, in address order on the free list. */
struct range {
    struct chunk *chunk;
    char *at;                   /* in chunk->rx */
    size_t size;
    struct range *next;
};

static struct {
    pthread_mutex_t lock;
    struct chunk *chunks;
    struct range *free;
    size_t mapped;
} code_pool = { .lock = PTHREAD_MUTEX_INITIALIZER };

/* Maps both views of c->size bytes of shared memory: the writable one,
   and an alias of its pages made executable. With `copy`, the new pages
   take c's place, holding what c holds but its free ranges (left
   untouched). 0, or -1 (a failed copy may have replaced one view). */
static int
map_views(struct chunk *c, int copy)
{
    size_t size = c->size;
    char *w = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (w == MAP_FAILED) {
        return -1;
    }
    if (copy) {
        size_t at = 0;
        for (struct range *r = code_pool.free; r != NULL; r = r->next) {
            if (r->chunk == c) {
                memcpy(w + at, c->rw + at, (size_t)(r->at - c->rx) - at);
                at = (size_t)(r->at - c->rx) + r->size;
            }
        }
        memcpy(w + at, c->rw + at, size - at);
    }
    char *x = mremap(w, 0, size, MREMAP_MAYMOVE | (copy ? MREMAP_FIXED : 0), copy ? c->rx : NULL);
    if (x == MAP_FAILED || mprotect(x, size, PROT_READ | PROT_EXEC) != 0) {
        munmap(w, size);
        if (x != MAP_FAILED && !copy) {
            munmap(x, size);
        }
        return -1;
    }
    if (copy && mremap(w, size, size, MREMAP_MAYMOVE | MREMAP_FIXED, c->rw) == MAP_FAILED) {
        munmap(w, size);
        return -1;
    }
    c->rw = copy ? c->rw : w;
    c->rx = x;
    return 0;
}

/* Puts r on the free list in address order, merged with its neighbours
   in the same chunk. The lock is held. */
static void
put_free(struct range *r)
{
    struct range *prev = NULL, *next = code_pool.free;
    while (next != NULL && next->at < r->at) {
        prev = next;
        next = next->next;
    }
    r->next = next;
    if (prev != NULL) {
        prev->next = r;
    }
    else {
        code_pool.free = r;
    }
    if (next != NULL && next->chunk == r->chunk && r->at + r->size == next->at) {
        r->size += next->size;
        r->next = next->next;
        free(next);
    }
    if (prev != NULL && prev->chunk == r->chunk && prev->at + prev->size == r->at) {
        prev->size += r->size;
        prev->next = r->next;
        free(r);
    }
}

void
lu_jit_code_after_fork(void)
{
    pthread_mutex_init(&code_pool.lock, NULL);
    for (struct chunk *c = code_pool.chunks; c != NULL; c = c->next) {
        if (map_views(c, 1) < 0) {
            /* Shared with the parent, which may write its freed ranges over
               units this process still runs. */
            fputs("lucifer: no memory to copy compiled code after fork\n", stderr);
            abort();
        }
    }
}

void *
lu_code_alloc(size_t n, void **rw)
{
    n = (n + ALIGN - 1) & ~(size_t)(ALIGN - 1);
    pthread_mutex_lock(&code_pool.lock);
    struct range **link = &code_pool.free;
    while (*link != NULL && (*link)->size < n) {
        link = &(*link)->next;
    }
    if (*link == NULL) {
        size_t page = (size_t)sysconf(_SC_PAGESIZE);
        size_t size = code_pool.mapped > FIRST_CHUNK ? code_pool.mapped : FIRST_CHUNK;
        size = ((size > n ? size : n) + page - 1) & ~(page - 1);
        struct chunk *c = malloc(sizeof *c);
        struct range *r = malloc(sizeof *r);
        if (c != NULL) {
            c->size = size;
        }
        if (c == NULL || r == NULL || map_views(c, 0) < 0) {
            free(c);
            free(r);
            pthread_mutex_unlock(&code_pool.lock);
            return NULL;
        }
        c->next = code_pool.chunks;
        code_pool.chunks = c;
        code_pool.mapped += size;
        *r = (struct range){ .chunk = c, .at = c->rx, .size = size };
        put_free(r);
        link = &code_pool.free;
        while ((*link)->chunk != c) {
            link = &(*link)->next;
        }
    }
    struct range *r = *link;
    char *at = r->at;
    struct chunk *c = r->chunk;
    if (r->size == n) {
        *link = r->next;
        free(r);
    }
    else {
        r->at += n;
        r->size -= n;
    }
    pthread_mutex_unlock(&code_pool.lock);
    *rw = c->rw + (at - c->rx);
    return at;
}

void
lu_code_free(void *code, size_t n)
{
    if (code == NULL || lu_perf_names_ranges()) {
        return;
    }
    n = (n + ALIGN - 1) & ~(size_t)(ALIGN - 1);
    struct range *r = malloc(sizeof *r);
    if (r == NULL) {
        return;                 /* the range stays taken */
    }
    pthread_mutex_lock(&code_pool.lock);
    struct chunk *c = code_pool.chunks;
    while ((char *)code < c->rx || (char *)code >= c->rx + c->size) {
        c = c->next;
    }
    /* Freed code traps if anything still runs it. */
    memset(c->rw + ((char *)code - c->rx), 0xCC, n);
    *r = (struct range){ .chunk = c, .at = code, .size = n };
    put_free(r);
    pthread_mutex_unlock(&code_pool.lock);
}

