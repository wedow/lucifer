CC = cc
CFLAGS = -O2 -g
# Hidden by default: only what the headers declare PyAPI_FUNC/PyAPI_DATA, or
# LU_EXPORT marks, reaches the dynamic symbol table that -rdynamic fills.
LU_CFLAGS = -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden \
	-fno-omit-frame-pointer -pthread -Iinclude -Iruntime -Ithird_party/mimalloc/include
LU_LDFLAGS = -rdynamic -pthread

SRCS = $(wildcard runtime/*.c runtime/*/*.c)
HEADERS = $(wildcard runtime/*.h runtime/*/*.h)
MAIN = build/runtime/main.o
LIB_OBJS = $(filter-out $(MAIN),$(SRCS:%.c=build/%.o)) build/frozen.o
HOST_PYTHON ?= python3

# The kernel, in boot order.
KERNEL = lib/kernel/_boot.py lib/kernel/object.py lib/kernel/type.py lib/kernel/descriptors.py \
	lib/kernel/builtins.py
EXPORTS = results/abi/cp314t-exports.tsv

.PHONY: all abi-audit check-globals check-capi check-kernel regen-abi check-harness check-compiler regen
all: bin/lucifer

# The runtime is an archive so that an embedder can link it too. Every member
# is kept: extensions bind to symbols that nothing in main.o refers to.
build/liblucifer.a: $(LIB_OBJS)
	@mkdir -p $(@D)
	rm -f $@
	ar rcs $@ $^

bin/lucifer: $(MAIN) build/liblucifer.a
	@mkdir -p $(@D)
	$(CC) $(LU_LDFLAGS) -o $@ $(MAIN) -Wl,--whole-archive build/liblucifer.a -Wl,--no-whole-archive

build/runtime/object/alloc.o: LU_CFLAGS += -Ithird_party/mimalloc/src

build/%.o: %.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LU_CFLAGS) -c -o $@ $<

build/frozen.c: tools/freeze $(KERNEL)
	$(HOST_PYTHON) tools/freeze $@ $(KERNEL)

build/frozen.o: build/frozen.c $(HEADERS)
	$(CC) $(CFLAGS) $(LU_CFLAGS) -c -o $@ $<

abi-audit: bin/lucifer
	python3 tools/abi/audit.py $(EXPORTS) bin/lucifer

# Every writable symbol of the executable is accounted for.
check-globals: bin/lucifer
	python3 tools/abi/globals.py $(EXPORTS) tools/abi/globals.tsv bin/lucifer

# Extensions built only against include/, preloaded with LD_BIND_NOW=1 so that
# ld.so must bind every import to bin/lucifer before their checks run. Each
# test runs 15 times: the thread tests fail only on some schedules.
CAPI_TESTS = $(patsubst tests/capi/%.c,build/tests/capi/%.so,$(wildcard tests/capi/*.c))

build/tests/capi/%.so: tests/capi/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -std=gnu11 -Wall -fPIC -shared -pthread -Iinclude -o $@ $<

check-capi: bin/lucifer $(CAPI_TESTS)
	@for so in $(CAPI_TESTS); do \
	  name=$$(basename $$so .so); \
	  for run in $$(seq 15); do \
	    out=$$(LD_BIND_NOW=1 LD_PRELOAD=./$$so bin/lucifer 2>&1); \
	    [ "$$out" = "capi $$name ok" ] || { echo "FAIL $$name (run $$run)"; exit 1; }; \
	  done; echo "PASS $$name"; \
	done

# Rerun when the headers or the export list change.
regen-abi:
	python3 tools/abi/stubs.py $(EXPORTS) include > runtime/capi/stubs.c

check-harness:
	python3 tools/oracle/test_harness.py
	python3 tools/libtest/check.py
	python3 tools/numpytest/check.py
	python3 tools/bench/check
	python3 tools/difftest/check.py
	python3 tools/ftcorpus/check.py
	python3 tools/pypi/check.py

# tests/kernel programs, host-compiled to .pyc, against the free-threaded
# oracle under a 512 KiB stack. PROGRAMS="a b" selects; LANE= picks a policy.
ORACLE_T ?= python3.14t

check-kernel: bin/lucifer
	$(HOST_PYTHON) tools/kernelcheck --oracle $(ORACLE_T) --lucifer bin/lucifer \
	  --programs "$(PROGRAMS)" $(if $(LANE),--lane $(LANE))

check-compiler:
	$(HOST_PYTHON) tools/compilercheck --corpus closure,difftest,kernel,lib --require closure,difftest,kernel

PEGEN = third_party/cpython/Tools/peg_generator

regen:
	PYTHONPATH=$(PEGEN) $(HOST_PYTHON) -m pegen -q python lib/compiler/python.gram -o lib/compiler/parser.py
