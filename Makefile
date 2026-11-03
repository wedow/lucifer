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

.PHONY: all abi-audit regen-abi check-harness
all: bin/lucifer

bin/lucifer: $(OBJS)
	@mkdir -p $(@D)
	$(CC) $(LU_LDFLAGS) -o $@ $(OBJS)

build/%.o: %.c $(wildcard runtime/*.h)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LU_CFLAGS) -c -o $@ $<

abi-audit: bin/lucifer
	python3 tools/abi/audit.py $(EXPORTS) bin/lucifer

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
