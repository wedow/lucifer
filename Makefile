.PHONY: check-harness
check-harness:
	python3 tools/numpytest/check.py

	python3 tools/bench/check
