CC = cc
CFLAGS = -O2 -g
# Hidden by default: only what the headers declare PyAPI_FUNC/PyAPI_DATA, or
# LU_EXPORT marks, reaches the dynamic symbol table that -rdynamic fills.
LU_CFLAGS = -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden \
	-fno-omit-frame-pointer -pthread -Iinclude -Iruntime -Ithird_party/mimalloc/include
LU_LDFLAGS = -rdynamic -pthread

# The optimizing tier's backend (runtime/jit/target.h): x86_64, or "stub"
# where there is none, which runs everything interpreted.
JIT_TARGET ?= $(if $(filter x86_64,$(shell uname -m)),x86_64,stub)
SRCS = $(wildcard runtime/*.c runtime/*/*.c runtime/compiler/*/*.c runtime/jit/$(JIT_TARGET)/*.c)
HEADERS = $(wildcard runtime/*.h runtime/*/*.h runtime/compiler/*/*.h runtime/jit/$(JIT_TARGET)/*.h)
MAIN = build/runtime/main.o
LIB_OBJS = $(filter-out $(MAIN),$(SRCS:%.c=build/%.o)) build/frozen.o
HOST_PYTHON ?= python3
ORACLE_T ?= python3.14t

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
	re._parser:re._parser:third_party/cpython/Lib/re/_parser.py \
	zipimport:zipimport:third_party/cpython/Lib/zipimport.py
EXPORTS = results/abi/cp314t-exports.tsv

# Every gate step runs under timeout: on expiry the whole process group is
# killed (so no daemon-thread child survives) and the step fails with 124/137.
# TMO bounds one program, STEP_TMO a whole tool run; override on the command line.
TMO = 120
STEP_TMO = 3600
RUN = timeout -k 10 $(TMO)
STEP = timeout -k 10 $(STEP_TMO)

.PHONY: all oracle-exts abi-audit check check-jit check-kernel-jit check-blocking check-globals check-capi check-capi-ext check-kernel check-embed regen-abi check-harness check-compiler check-timing
all: bin/lucifer build/lib/stamp

# The runtime is an archive so that an embedder can link it too. Every member
# is kept: extensions bind to symbols that nothing in main.o refers to.
build/liblucifer.a: $(LIB_OBJS)
	@mkdir -p $(@D)
	rm -f $@
	ar rcs $@ $^

bin/lucifer: $(MAIN) build/liblucifer.a
	@mkdir -p $(@D)
	$(CC) $(LU_LDFLAGS) -o $@ $(MAIN) -Wl,--whole-archive build/liblucifer.a -Wl,--no-whole-archive -lm -lz -lffi

build/runtime/object/alloc.o: LU_CFLAGS += -Ithird_party/mimalloc/src

# CPython's compiler, as CPython's own files with the internal headers beside them.
build/runtime/compiler/%.o: LU_CFLAGS += -DPy_BUILD_CORE -DPy_BUILD_CORE_MODULE -Iruntime/compiler \
	-Iruntime/compiler/include -Iruntime/compiler/lexer -Iruntime/compiler/tokenizer -Wno-unused-function \
	-Wno-sign-compare -Wno-missing-field-initializers -Wno-type-limits -Wno-implicit-fallthrough -Wno-unused-variable

build/%.o: %.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LU_CFLAGS) -c -o $@ $<

build/frozen.c: Makefile tools/freeze $(KERNEL) $(MODULES) $(foreach e,$(FROZEN),$(lastword $(subst :, ,$(e))))
	$(HOST_PYTHON) tools/freeze $@ --kernel $(KERNEL) --builtin $(MODULES) --frozen $(FROZEN)

build/frozen.o: build/frozen.c $(HEADERS)
	$(CC) $(CFLAGS) $(LU_CFLAGS) -c -o $@ $<

# The installed standard library next to the executable: Lib/ plus its pycs.
STDLIB = build/lib/python3.14t
SYSCONFIGDATA = _sysconfigdata_t_linux_x86_64-linux-gnu.py
build/lib/stamp: tools/freeze lib/sysconfigdata.py $(shell find third_party/cpython/Lib -type f -not -name '*.pyc')
	$(HOST_PYTHON) tools/freeze --pyc $(STDLIB) third_party/cpython/Lib $(SYSCONFIGDATA)=lib/sysconfigdata.py
	mkdir -p build/include && ln -sfn ../../include build/include/python3.14t   # sysconfig's INCLUDEPY
	touch $@

# Standard-library extension modules: the tag's own C (third_party/cpython/Modules),
# built as any extension is, against include/ and the tag's internal headers, and
# linked to the system's libraries, into lib-dynload as CPython installs them.
DYNLOAD = $(STDLIB)/lib-dynload
EXT_SUFFIX = .cpython-314t-x86_64-linux-gnu.so
CPY_MODULES = third_party/cpython/Modules
CJK_CODECS = _codecs_cn _codecs_hk _codecs_iso2022 _codecs_jp _codecs_kr _codecs_tw
STDLIB_EXTS = _ssl _sqlite3 _bz2 pyexpat _elementtree _pickle cmath _multiprocessing _posixshmem _multibytecodec \
	$(CJK_CODECS)
STDLIB_EXT_FILES = $(STDLIB_EXTS:%=$(DYNLOAD)/%$(EXT_SUFFIX))
all: $(STDLIB_EXT_FILES)
$(DYNLOAD)/_ssl$(EXT_SUFFIX): EXT_SRC = $(CPY_MODULES)/_ssl.c
$(DYNLOAD)/_ssl$(EXT_SUFFIX): EXT_LIBS = -lssl -lcrypto
$(DYNLOAD)/_sqlite3$(EXT_SUFFIX): EXT_SRC = $(wildcard $(CPY_MODULES)/_sqlite/*.c)
$(DYNLOAD)/_sqlite3$(EXT_SUFFIX): EXT_LIBS = -lsqlite3
$(DYNLOAD)/_bz2$(EXT_SUFFIX): EXT_SRC = $(CPY_MODULES)/_bz2module.c
$(DYNLOAD)/_bz2$(EXT_SUFFIX): EXT_LIBS = -lbz2
$(DYNLOAD)/pyexpat$(EXT_SUFFIX): EXT_SRC = $(CPY_MODULES)/pyexpat.c
$(DYNLOAD)/pyexpat$(EXT_SUFFIX): EXT_LIBS = -lexpat
$(DYNLOAD)/_elementtree$(EXT_SUFFIX): EXT_SRC = $(CPY_MODULES)/_elementtree.c
$(DYNLOAD)/_pickle$(EXT_SUFFIX): EXT_SRC = $(CPY_MODULES)/_pickle.c
$(DYNLOAD)/cmath$(EXT_SUFFIX): EXT_SRC = $(CPY_MODULES)/cmathmodule.c
$(DYNLOAD)/cmath$(EXT_SUFFIX): EXT_LIBS = -lm
$(DYNLOAD)/_multiprocessing$(EXT_SUFFIX): EXT_SRC = $(CPY_MODULES)/_multiprocessing/multiprocessing.c \
	$(CPY_MODULES)/_multiprocessing/semaphore.c
$(DYNLOAD)/_posixshmem$(EXT_SUFFIX): EXT_SRC = $(CPY_MODULES)/_multiprocessing/posixshmem.c
$(DYNLOAD)/_multibytecodec$(EXT_SUFFIX): EXT_SRC = $(CPY_MODULES)/cjkcodecs/multibytecodec.c
$(CJK_CODECS:%=$(DYNLOAD)/%$(EXT_SUFFIX)): EXT_SRC = $(CPY_MODULES)/cjkcodecs/$*.c

$(DYNLOAD)/%$(EXT_SUFFIX): $(shell find $(CPY_MODULES) third_party/cpython/Include -type f) $(wildcard include/*.h include/cpython/*.h)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -std=c11 -fno-strict-overflow -DNDEBUG -fvisibility=hidden -fPIC -shared -pthread \
	  -Iinclude -Ithird_party/cpython/Include/internal -I$(CPY_MODULES)/expat -o $@ $(EXT_SRC) $(EXT_LIBS)

abi-audit: bin/lucifer
	python3 tools/abi/audit.py $(EXPORTS) bin/lucifer

check: check-blocking check-globals check-capi check-kernel check-kernel-jit check-embed

# Blocking calls only in runtime/prim/os.c, inside the syscall bracket.
check-blocking: $(MAIN) $(LIB_OBJS)
	$(STEP) $(HOST_PYTHON) tools/blockcheck runtime/prim/os.c $(MAIN) $(LIB_OBJS)

# Every writable symbol of the executable is accounted for.
check-globals: bin/lucifer
	$(STEP) python3 tools/abi/globals.py $(EXPORTS) tools/abi/globals.tsv bin/lucifer

# Extensions built only against include/, preloaded with LD_BIND_NOW=1 so that
# ld.so must bind every import to bin/lucifer before their checks run. Each
# test runs 15 times: the thread tests fail only on some schedules.
CAPI_TESTS = $(patsubst tests/capi/%.c,build/tests/capi/%.so,$(filter-out $(EXT_SOURCES),$(wildcard tests/capi/*.c)))

build/tests/capi/%.so: tests/capi/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -std=gnu11 -Wall -fPIC -shared -pthread -Iinclude -o $@ $<

# Extension modules, imported unmodified: the oracle's own _bisect, _heapq and
# _statistics, found beside the oracle, and tests/capi's modules, built here.
# Each EXT_PROGRAMS program prints the same stdout, stderr and exit status
# under the oracle and bin/lucifer.
EXT_DIR = build/tests/capi/ext
EXT_SOURCES = tests/capi/exc_cross.c tests/capi/slots.c tests/capi/capi_monitoring.c tests/capi/capi_conv.c tests/capi/ready.c
EXT_PROGRAMS = accel accel_use exc_cross_run slots_run capi_monitoring_run capi_conv_run testcapi_import container_refs ready_run list_capi_run
SLOTS_MODULES = plain noshare shared_gil create two_exec unknown negative two_create two_interp \
	two_gil not_module not_module_exec create_fails create_raises exec_fails exec_raises

$(EXT_DIR)/%$(EXT_SUFFIX): tests/capi/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -std=gnu11 -Wall -fPIC -shared -pthread -Iinclude -o $@ $<

# slots.c holds one PyInit_ per module; a file of each name loads it.
$(EXT_DIR)/slots_%$(EXT_SUFFIX): $(EXT_DIR)/slots$(EXT_SUFFIX)
	ln -sf slots$(EXT_SUFFIX) $@

# The oracle's own C test modules, beside the installed standard library, for
# the libtest modules and the capi gate programs that import them.
LIBTEST_EXTS = _testcapi _testlimitedcapi _testinternalcapi _testmultiphase
oracle-exts: build/lib/stamp
	dir=$$($(ORACLE_T) -c 'import _testcapi, os; print(os.path.dirname(_testcapi.__file__))') && \
	for m in $(LIBTEST_EXTS); do ln -sf $$dir/$$m$(EXT_SUFFIX) $(STDLIB)/; done

$(EXT_DIR)/accelerators:
	@mkdir -p $(@D)
	dir=$$($(ORACLE_T) -c 'import _bisect, os; print(os.path.dirname(_bisect.__file__))') && \
	for m in _bisect _heapq _statistics; do ln -sf $$dir/$$m$(EXT_SUFFIX) $(EXT_DIR)/; done
	touch $@

check-capi-ext: bin/lucifer oracle-exts $(EXT_DIR)/accelerators $(EXT_DIR)/exc_cross$(EXT_SUFFIX) \
	  $(EXT_DIR)/capi_monitoring$(EXT_SUFFIX) $(EXT_DIR)/capi_conv$(EXT_SUFFIX) $(EXT_DIR)/ready$(EXT_SUFFIX) \
	  $(foreach m,$(SLOTS_MODULES),$(EXT_DIR)/slots_$(m)$(EXT_SUFFIX))
	@for p in $(EXT_PROGRAMS); do \
	  { PYTHONPATH=$(EXT_DIR) $(RUN) $(ORACLE_T) tests/capi/$$p.py 2>&1; echo "exit $$?"; } > $(EXT_DIR)/$$p.expected; \
	  { PYTHONPATH=$(EXT_DIR) $(RUN) bin/lucifer tests/capi/$$p.py 2>&1; echo "exit $$?"; } > $(EXT_DIR)/$$p.actual; \
	  diff $(EXT_DIR)/$$p.expected $(EXT_DIR)/$$p.actual > $(EXT_DIR)/$$p.diff || { cat $(EXT_DIR)/$$p.diff; echo "FAIL $$p"; exit 1; }; \
	  echo "PASS $$p"; \
	done

check-capi: bin/lucifer $(CAPI_TESTS) check-capi-ext
	@for so in $(CAPI_TESTS); do \
	  name=$$(basename $$so .so); \
	  for run in $$(seq 15); do \
	    out=$$($(RUN) env LD_BIND_NOW=1 LD_PRELOAD=./$$so bin/lucifer 2>&1); \
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
	$(CC) $(CFLAGS) -std=gnu11 -Wall -Iinclude -pthread -o $@ $< -Wl,--whole-archive build/liblucifer.a -Wl,--no-whole-archive -lm -lz -lffi

check-embed: build/tests/embed/run bin/lucifer
	@$(RUN) build/tests/embed/run $(EMBED_PROGRAMS) > build/tests/embed/actual 2>&1; \
	st=$$?; [ $$st -lt 124 ] || { echo "FAIL embed (exit $$st)"; exit 1; }; \
	{ for p in $(EMBED_PROGRAMS); do $(RUN) bin/lucifer -c "$$p" 2>&1; done; } > build/tests/embed/expected; \
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
	python3 tools/readme/check.py

# tests/kernel programs, host-compiled to .pyc, against the free-threaded
# oracle under a 512 KiB stack. PROGRAMS="a b" selects; LANE= picks a policy.

check-kernel: bin/lucifer build/lib/stamp $(STDLIB_EXT_FILES)
	$(STEP) $(HOST_PYTHON) tools/kernelcheck --opcodes
	$(STEP) $(HOST_PYTHON) tools/kernelcheck --oracle $(ORACLE_T) --lucifer bin/lucifer \
	  --programs "$(PROGRAMS)" $(if $(LANE),--lane $(LANE))

# tests/kernel with every function compiled at its first call, then with every
# 7th guard failing as well: the lanes every landing runs.
check-kernel-jit: bin/lucifer build/lib/stamp $(STDLIB_EXT_FILES)
	for lane in jit jit-deopt; do \
	  $(STEP) $(HOST_PYTHON) tools/kernelcheck --oracle $(ORACLE_T) --lucifer bin/lucifer --lane $$lane || exit 1; \
	done

# The optimizing tier on AWFY in every JIT mode, then the difftest corpus with
# every function compiled at its first call, without and with failing guards.
DIFFTEST_JIT = always:'-X lucifer_jit=always' deopt:'-X lucifer_jit=always -X lucifer_stress=deopt:7'
check-jit: bin/lucifer build/lib/stamp $(STDLIB_EXT_FILES)
	$(STEP) python3 tools/jitcheck --lucifer bin/lucifer --oracle $(ORACLE_T)
	@for m in $(DIFFTEST_JIT); do \
	  name=$${m%%:*}; flags=$${m#*:}; \
	  $(STEP) python3 tools/difftest run --oracle $(ORACLE_T) --python "bin/lucifer $$flags" --runtime lucifer \
	    --results build/difftest/$$name --work build/difftest/$$name/work || { echo "FAIL difftest $$name"; exit 1; }; \
	  echo "PASS difftest $$name"; \
	done

# Wall-clock bounds: an uncaught exception, a 100,000-line function.
check-timing: bin/lucifer build/lib/stamp
	$(STEP) python3 tools/timecheck --lucifer bin/lucifer --oracle $(ORACLE_T)

check-compiler: bin/lucifer build/lib/stamp
	$(STEP) $(HOST_PYTHON) tools/compilercheck --lucifer bin/lucifer --oracle $(ORACLE_T) \
	  --corpus closure,difftest,kernel,lib --require closure,difftest,kernel,lib

