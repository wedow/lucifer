CC ?= cc
CFLAGS ?= -O2 -g
LU_CFLAGS = -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden \
	-pthread -Iinclude
LU_LDFLAGS = -rdynamic -pthread

.PHONY: all abi-audit check-harness
all: bin/lucifer

abi-audit: bin/lucifer
	python3 tools/abi/audit.py results/abi/cp314t-exports.tsv bin/lucifer

check-harness:
	python3 tools/oracle/test_harness.py
	python3 tools/libtest/check.py
	python3 tools/numpytest/check.py
	python3 tools/bench/check
	python3 tools/difftest/check.py
	python3 tools/ftcorpus/check.py
	python3 tools/pypi/check.py
