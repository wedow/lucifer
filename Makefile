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
	lib/kernel/exceptions.py lib/kernel/operators.py lib/kernel/_format.py lib/kernel/int.py \
	lib/kernel/float.py lib/kernel/complex.py lib/kernel/slice.py lib/kernel/_args.py lib/kernel/iter.py lib/kernel/tuple.py lib/kernel/_listsort.py \
	lib/kernel/list.py lib/kernel/range.py lib/kernel/str.py lib/kernel/bytes.py lib/kernel/memoryview.py \
	lib/kernel/dict.py lib/kernel/set.py lib/kernel/mappingproxy.py \
	lib/kernel/generators.py lib/kernel/frame.py lib/kernel/code.py \
	lib/kernel/iterators.py lib/kernel/builtins.py lib/kernel/weakref.py lib/kernel/module.py lib/kernel/structseq.py lib/kernel/sys.py lib/kernel/genericalias.py \
	lib/kernel/template.py \
	lib/kernel/cell.py \
	lib/kernel/match.py
# The builtin modules, as Python over the _lucifer primitives, and the modules
# importlib finds as frozen (NAME:ORIGNAME:FILE).
MODULES = $(wildcard lib/modules/*.py)
FROZEN = _frozen_importlib:importlib._bootstrap:third_party/cpython/Lib/importlib/_bootstrap.py \
	_frozen_importlib_external:importlib._bootstrap_external:third_party/cpython/Lib/importlib/_bootstrap_external.py \
	codecs:codecs:third_party/cpython/Lib/codecs.py \
	encodings:encodings:third_party/cpython/Lib/encodings/__init__.py \
	encodings.aliases:encodings.aliases:third_party/cpython/Lib/encodings/aliases.py \
	encodings.utf_8:encodings.utf_8:third_party/cpython/Lib/encodings/utf_8.py \
	encodings.ascii:encodings.ascii:third_party/cpython/Lib/encodings/ascii.py \
	encodings.latin_1:encodings.latin_1:third_party/cpython/Lib/encodings/latin_1.py \
	encodings.utf_16:encodings.utf_16:third_party/cpython/Lib/encodings/utf_16.py \
	encodings.utf_32:encodings.utf_32:third_party/cpython/Lib/encodings/utf_32.py \
	encodings.utf_16_le:encodings.utf_16_le:third_party/cpython/Lib/encodings/utf_16_le.py \
	encodings.utf_16_be:encodings.utf_16_be:third_party/cpython/Lib/encodings/utf_16_be.py \
	encodings.utf_32_le:encodings.utf_32_le:third_party/cpython/Lib/encodings/utf_32_le.py \
	encodings.utf_32_be:encodings.utf_32_be:third_party/cpython/Lib/encodings/utf_32_be.py \
	encodings.utf_8_sig:encodings.utf_8_sig:third_party/cpython/Lib/encodings/utf_8_sig.py \
	encodings.unicode_escape:encodings.unicode_escape:third_party/cpython/Lib/encodings/unicode_escape.py \
	encodings.raw_unicode_escape:encodings.raw_unicode_escape:third_party/cpython/Lib/encodings/raw_unicode_escape.py \
	abc:abc:third_party/cpython/Lib/abc.py \
	_py_abc:_py_abc:third_party/cpython/Lib/_py_abc.py \
	_weakrefset:_weakrefset:third_party/cpython/Lib/_weakrefset.py \
	_collections_abc:_collections_abc:third_party/cpython/Lib/_collections_abc.py \
	io:io:third_party/cpython/Lib/io.py \
	types:types:third_party/cpython/Lib/types.py \
	keyword:keyword:third_party/cpython/Lib/keyword.py \
	reprlib:reprlib:third_party/cpython/Lib/reprlib.py \
	copyreg:copyreg:third_party/cpython/Lib/copyreg.py \
	operator:operator:third_party/cpython/Lib/operator.py \
	functools:functools:third_party/cpython/Lib/functools.py \
	collections:collections:third_party/cpython/Lib/collections/__init__.py \
	typing:typing:third_party/cpython/Lib/typing.py \
	token:token:third_party/cpython/Lib/token.py \
	__future__:__future__:third_party/cpython/Lib/__future__.py \
	ast:ast:third_party/cpython/Lib/ast.py \
	_ast_unparse:_ast_unparse:third_party/cpython/Lib/_ast_unparse.py \
	enum:enum:third_party/cpython/Lib/enum.py \
	contextlib:contextlib:third_party/cpython/Lib/contextlib.py \
	os:os:third_party/cpython/Lib/os.py \
	stat:stat:third_party/cpython/Lib/stat.py \
	posixpath:posixpath:third_party/cpython/Lib/posixpath.py \
	genericpath:genericpath:third_party/cpython/Lib/genericpath.py \
	runpy:runpy:third_party/cpython/Lib/runpy.py \
	re:re:third_party/cpython/Lib/re/__init__.py \
	re._casefix:re._casefix:third_party/cpython/Lib/re/_casefix.py \
	re._compiler:re._compiler:third_party/cpython/Lib/re/_compiler.py \
	re._constants:re._constants:third_party/cpython/Lib/re/_constants.py \
	re._parser:re._parser:third_party/cpython/Lib/re/_parser.py
# The compiler runs as a C module would: its frames are not in tracebacks.
HIDDEN = compiler:compiler:lib/compiler/__init__.py \
	$(foreach f,$(filter-out %/__init__.py,$(wildcard lib/compiler/*.py)),compiler.$(basename $(notdir $f)):compiler.$(basename $(notdir $f)):$f)
EXPORTS = results/abi/cp314t-exports.tsv

.PHONY: all abi-audit check check-blocking check-globals check-capi check-kernel check-embed regen-abi check-harness check-compiler regen
all: bin/lucifer build/lib/stamp

# The runtime is an archive so that an embedder can link it too. Every member
# is kept: extensions bind to symbols that nothing in main.o refers to.
build/liblucifer.a: $(LIB_OBJS)
	@mkdir -p $(@D)
	rm -f $@
	ar rcs $@ $^

bin/lucifer: $(MAIN) build/liblucifer.a
	@mkdir -p $(@D)
	$(CC) $(LU_LDFLAGS) -o $@ $(MAIN) -Wl,--whole-archive build/liblucifer.a -Wl,--no-whole-archive -lm

build/runtime/object/alloc.o: LU_CFLAGS += -Ithird_party/mimalloc/src

build/%.o: %.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LU_CFLAGS) -c -o $@ $<

build/frozen.c: Makefile tools/freeze $(KERNEL) $(MODULES) $(foreach e,$(FROZEN) $(HIDDEN),$(lastword $(subst :, ,$(e))))
	$(HOST_PYTHON) tools/freeze $@ --kernel $(KERNEL) --builtin $(MODULES) --frozen $(FROZEN) --hidden $(HIDDEN)

build/frozen.o: build/frozen.c $(HEADERS)
	$(CC) $(CFLAGS) $(LU_CFLAGS) -c -o $@ $<

# The installed standard library next to the executable: Lib/ plus its pycs.
STDLIB = build/lib/python3.14t
SYSCONFIGDATA = _sysconfigdata_t_linux_x86_64-linux-gnu.py
build/lib/stamp: tools/freeze lib/sysconfigdata.py $(shell find third_party/cpython/Lib -type f -not -name '*.pyc')
	$(HOST_PYTHON) tools/freeze --pyc $(STDLIB) third_party/cpython/Lib $(SYSCONFIGDATA)=lib/sysconfigdata.py
	touch $@

abi-audit: bin/lucifer
	python3 tools/abi/audit.py $(EXPORTS) bin/lucifer

check: check-blocking check-globals check-capi check-kernel check-embed

# Blocking calls only in runtime/prim/os.c, inside the syscall bracket.
check-blocking: $(MAIN) $(LIB_OBJS)
	$(HOST_PYTHON) tools/blockcheck runtime/prim/os.c $(MAIN) $(LIB_OBJS)

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

# An embedder links the archive: its programs, each in its own initialize and
# finalize cycle, print what the executable prints for the same -c programs.
EMBED_PROGRAMS = 'import os; x = 1; print("first", x)' \
	'print("second", globals().get("x"))' \
	'import atexit; atexit.register(print, "exit"); d = type("D", (), {"__del__": lambda s: print("del")})()' \
	'1 / 0' \
	'print("after error")'

build/tests/embed/run: tests/embed/run.c build/liblucifer.a
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -std=gnu11 -Wall -Iinclude -pthread -o $@ $< -Wl,--whole-archive build/liblucifer.a -Wl,--no-whole-archive -lm

check-embed: build/tests/embed/run bin/lucifer
	@build/tests/embed/run $(EMBED_PROGRAMS) > build/tests/embed/actual 2>&1; \
	{ for p in $(EMBED_PROGRAMS); do bin/lucifer -c "$$p" 2>&1; done; } > build/tests/embed/expected; \
	diff build/tests/embed/expected build/tests/embed/actual && echo "PASS embed"

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
	$(HOST_PYTHON) tools/genast
