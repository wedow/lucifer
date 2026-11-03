CC = cc
CFLAGS = -O2 -g
# Hidden by default: only what the headers declare PyAPI_FUNC/PyAPI_DATA, or
# LU_EXPORT marks, reaches the dynamic symbol table that -rdynamic fills.
LU_CFLAGS = -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden \
	-pthread -Iinclude -Iruntime
LU_LDFLAGS = -rdynamic -pthread

SRCS = $(wildcard runtime/*.c runtime/*/*.c)
OBJS = $(SRCS:%.c=build/%.o)
EXPORTS = results/abi/cp314t-exports.tsv

.PHONY: all abi-audit check-capi regen-abi check-harness check-compiler regen
all: bin/lucifer

bin/lucifer: $(OBJS)
	@mkdir -p $(@D)
	$(CC) $(LU_LDFLAGS) -o $@ $(OBJS)

build/runtime/object/alloc.o: LU_CFLAGS += -Ithird_party/mimalloc/include -Ithird_party/mimalloc/src

build/%.o: %.c $(wildcard runtime/*.h)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LU_CFLAGS) -c -o $@ $<

abi-audit: bin/lucifer
	python3 tools/abi/audit.py $(EXPORTS) bin/lucifer

# Extensions built only against include/, preloaded with LD_BIND_NOW=1 so that
# ld.so must bind every import to bin/lucifer before their checks run.
CAPI_TESTS = $(patsubst tests/capi/%.c,build/tests/capi/%.so,$(wildcard tests/capi/*.c))

build/tests/capi/%.so: tests/capi/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -std=gnu11 -Wall -fPIC -shared -pthread -Iinclude -o $@ $<

check-capi: bin/lucifer $(CAPI_TESTS)
	@for so in $(CAPI_TESTS); do \
	  name=$$(basename $$so .so); \
	  out=$$(LD_BIND_NOW=1 LD_PRELOAD=./$$so bin/lucifer); \
	  if [ "$$out" = "capi $$name ok" ]; then echo "PASS $$name"; \
	  else echo "FAIL $$name"; exit 1; fi; \
	done

# Rerun when the headers or the export list change.
regen-abi:
	python3 tools/abi/stubs.py $(EXPORTS) include > runtime/capi/stubs.c

HOST_PYTHON ?= python3

check-harness:
	python3 tools/oracle/test_harness.py
	python3 tools/libtest/check.py
	python3 tools/numpytest/check.py
	python3 tools/bench/check
	python3 tools/difftest/check.py
	python3 tools/ftcorpus/check.py
	python3 tools/pypi/check.py

check-compiler:
	$(HOST_PYTHON) tools/compilercheck --corpus closure,difftest,kernel,lib --require closure,difftest,kernel

PEGEN = third_party/cpython/Tools/peg_generator

regen:
	PYTHONPATH=$(PEGEN) $(HOST_PYTHON) -m pegen -q python lib/compiler/python.gram -o lib/compiler/parser.py
