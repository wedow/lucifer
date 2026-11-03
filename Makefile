.PHONY: check-harness check-compiler regen
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
