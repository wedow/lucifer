# Validation

## CPython oracles: `tools/oracle`

The validation baseline is **CPython v3.14.10**, built from the official
[python/cpython repository](https://github.com/python/cpython/tree/v3.14.10)
at commit [`b703b2c5e704341d61e367b9b940b77ed11dded0`](https://github.com/python/cpython/commit/b703b2c5e704341d61e367b9b940b77ed11dded0).
Use separate default and free-threaded builds of that same commit. This pin
supersedes the earlier 3.14.0 proposal in
[the research plan](research/05-oracles-and-benchmarks.md); it is a deliberate
baseline selection, independent of the host Python version.

### Build and provenance

Run from the checkout, choosing an absolute destination outside it:

```sh
oracle_root=/absolute/path/to/oracles
tools/oracle/build-cpython v3.14.10 "$oracle_root/cpython-3.14.10"
tools/oracle/build-cpython v3.14.10 "$oracle_root/cpython-3.14.10t" --disable-gil
```

`build-cpython TAG PREFIX [--disable-gil]` is a POSIX shell script. It uses
Git, a C compiler, make and the available system development libraries; it
does not install system packages. Invoke it through any build serialization
required by the environment. It never builds the repository's reference
`vendor/` tree.

The script shallow-clones the official release tag, compares the checkout's
HEAD and peeled tag to the official remote commit, and rejects a changed
v3.14.10 commit against the hard-coded pin. This verifies commit identity;
release signing-key verification is **unverified**. Other release
tags are checked against the official remote and recorded, but do not have
a reviewed hard-coded commit pin in this script.

Both variants use the following configure arguments, with `CFLAGS=-O2`:

```text
--prefix=PREFIX --enable-optimizations=no
--prefix=PREFIX --enable-optimizations=no --disable-gil
```

These are release builds without PGO. CPython's default `OPT` includes an
earlier `-O3`; the explicit `CFLAGS=-O2` comes later in compiler invocations
and selects `-O2`. The installed `PREFIX/ORACLE-INFO` records the tag,
commit, official source URL, exact shell-quoted configure command (including
`CC`, `CFLAGS`, `CPPFLAGS`, and `LDFLAGS`), and full compiler version.
Provenance containing machine-specific paths stays beside the installed
oracle, outside the public checkout.

`CC` selects a single compiler executable; `JOBS` defaults to 4. Optional
`CPPFLAGS` and `LDFLAGS` are passed through and recorded. `ORACLE_WORKDIR`
can select the work directory; its default is
`PREFIX_PARENT/.oracle-build/PREFIX_BASENAME`. Source and out-of-tree build
directories are `source/` and `build/` there. The latter retains
`configure.log`, `build.log`, and `install.log`. Sources remain available
for harnesses that need CPython's upstream tests or tools.

Installation uses a staging tree under `PREFIX_PARENT/.oracle-stage/`,
checks the exact interpreter version, ABI build flag and default GIL
state, then publishes the complete prefix by rename. Waiting consumers
can use the appearance of `bin/python3.14` or `bin/python3.14t` as the
readiness signal. A repeated invocation checks the recorded provenance
and installed interpreter and returns without rebuilding. An interrupted
build resumes in its existing work directory. A different configuration,
modified source, or unrelated nonempty prefix is rejected rather than
overwritten; choose a fresh prefix/work directory for a different build.
Locks protect both the work directory and prefix. After an uncatchable
termination, remove a stale lock directory only after verifying its build
has stopped.

### Installation checks and scoreboard

Run the driver using the host's Python 3, passing the oracle as a command:

```sh
python3 tools/oracle/check --python "$oracle_root/cpython-3.14.10/bin/python3.14" \
    --runtime cpython-3.14.10
python3 tools/oracle/check --python "$oracle_root/cpython-3.14.10t/bin/python3.14t" \
    --runtime cpython-3.14.10t --disable-gil
python3 tools/oracle/check --python bin/lucifer --runtime lucifer --disable-gil
tools/scoreboard
```

`--python` supports a shell-quoted executable and arguments, parsed without
a shell. Use `--disable-gil` for a `cp314t` target. The driver checks Python
3.14 compatibility, ABI flags and runtime GIL state, plus imports of `ssl`,
`sqlite3`, `ctypes`, `zlib`, `lzma`, `bz2`, `readline`, `decimal` with the
`_decimal` backend, and optional `_tkinter`. Every successful module import
also checks that the requested GIL state is preserved. Importing `_tkinter`
does not require opening a GUI window. This is an installation health
panel, not a claim that a language or C-API compatibility suite has passed.

Each process has a timeout (`--timeout`, default 30 seconds). Missing,
failing, crashing, timing-out or nonresponsive startup produces all 11
expected rows as `NOTRUN` and exits 1. After successful startup, assertion
or required-import failures, signals, timeouts, missing response markers
and optional-module absence remain distinct (`FAIL`, `CRASH`, `TIMEOUT`,
`ERROR`, `SKIP`). A missing `_tkinter` is an optional skip, not a pass.

Manifests live in `results/oracle/expected.tsv` and
`results/oracle/RUNTIME.tsv`, or below `--results DIR`. They have LF
endings, sorted IDs and the header `id\tstatus\tseconds`. The expected
inventory uses `NOTRUN` and zero seconds as placeholders. JSON details
beside each result include the command and diagnostics and are ignored by
Git because they can contain local paths. The scoreboard counts every
expected ID, fills missing result rows with `NOTRUN`, reports all status
counts, and divides passes by the complete expected inventory. Empty
inventories, unexpected IDs and malformed rows are invalid. It also
accepts an expected inventory with a single `id` column.

The initial recorded runs have **10 PASS and 1 SKIP out of 11** for each
CPython build. Both have every required module above, including `_decimal`.
Only `_tkinter` is missing: configure could not find `tcl.h` or the Tcl/Tk
pkg-config dependencies. No system dependencies were installed. The
free-threaded executable's `sys._is_gil_enabled()` prints `False`, including
after the available module imports. Lucifer's absent executable records
**11 NOTRUN**, a 0% starting score. These results describe this initial
installation, not guarantees about other build environments.

### Environments and harness checks

`tools/oracle/venv PREFIX DIR` creates a venv from the selected installed
oracle, preferring `python3.14t` when present. It requires `ORACLE-INFO`
and refuses an existing destination. Package installation belongs to the
consuming suite, which must pin its own NumPy or pyperformance versions,
dependencies and artifacts; the helper performs no network installation.

```sh
tools/oracle/venv "$oracle_root/cpython-3.14.10t" /absolute/path/to/venv
python3 tools/oracle/test_harness.py
```

The offline binary checks use fake toolchain commands and interpreters.
They cover both ABI build paths, safe quoting, commit mismatches,
idempotence, retry after build failure, preservation of unrelated prefixes,
configuration mismatch, venv interpreter selection, unavailable startup,
distinct per-test outcomes, complete manifests, and scoreboard denominators.
Run them through the environment's test serialization when required.

## libtest: CPython regression suite

`tools/libtest/run.py` is a host-Python, standard-library-only harness for the
unmodified [CPython v3.14.10 regression suite](https://github.com/python/cpython/tree/v3.14.10/Lib/test).
`third_party/cpython/VERSION` identifies the source commit. The complete `Lib/`
and license are vendored; modifications must be recorded in
`third_party/cpython/CHANGES.md`. This pin supersedes the research plan's earlier
3.14.0 proposal for this lane.

Collection uses each oracle's installed suite, with regrtest `--list-tests`
followed by `--list-cases` separately for every module. Collect the free-threaded
build first, then the GIL build. `inventory.json` preserves the module-to-case
mapping, per-build collection, empty/skipped modules, and collection errors.
`expected.tsv` is their sorted union, with NOTRUN placeholders; it never depends
on Lucifer starting. Cases available under only one build stay in the denominator.
Modules skipped on import on both builds have no collectible case IDs and are
visible in the inventory rather than invented test cases. This is a host-specific
collection baseline, not a claim to cover every platform or optional dependency.
Native `module.Class.method` IDs, parameterized class names and loader-generated
skip placeholders are preserved. Doctests sometimes have shorter IDs such as
`decimal`; these upstream IDs are retained too. Repeated identical IDs (for
example the Python and C decimal doctests) form one row with the worst outcome.

```sh
python3 tools/libtest/run.py collect --python "$ORACLE_T" --source installed --runtime cpython-3.14.10t
python3 tools/libtest/run.py collect --python "$ORACLE_GIL" --source installed --runtime cpython-3.14.10
python3 tools/libtest/run.py collect --profile --python "$ORACLE_T" --source installed --runtime cpython-3.14.10t
python3 tools/libtest/run.py collect --profile --python "$ORACLE_GIL" --source installed --runtime cpython-3.14.10
python3 tools/libtest/run.py run --python "$ORACLE_T" --source installed --runtime cpython-3.14.10t
python3 tools/libtest/run.py run --python "$ORACLE_GIL" --source installed --runtime cpython-3.14.10
python3 tools/libtest/run.py run --python bin/lucifer --runtime lucifer
python3 tools/scoreboard --suite libtest
python3 tools/libtest/check.py
```

`--python` takes a shell-quoted command but never executes a shell. Oracle paths
are supplied by the caller. The default `--source vendored` adds the vendored
`Lib/` to the target's `PYTHONPATH`; `--source installed` uses the interpreter's
own installed library and test extensions. The host never imports vendored code.
A bounded startup probe checks JSON output after importing `test`; an absent,
failing, or hanging target produces every expected case as NOTRUN and exits
successfully. Outcomes describe coverage; callers use the scoreboard to enforce
their acceptance policy.

Execution uses two concurrent module controllers by default (`--jobs 2`), each
with one upstream regrtest worker (`-j 1`), a 180-second module timeout, and an
additional 30-second emergency bound on its controller. Each controller has a
private temporary working directory. A controller invokes, with actual paths
substituted:

```text
{python} tools/libtest/bridge.py -j 1 --python "{python} tools/libtest/bridge.py --worker" --timeout 180 --randseed 0 -u all,-network,-urlfetch,-gui,-largefile,-cpu,-walltime --junit-xml {scratch}/junit.xml --tempdir {scratch} {module}
```

The observer calls regrtest's own entry point and worker module. It records
module states and unittest events without changing test assertions, selection,
decorators, or the vendored library. It supplements JUnit XML for class/module
fixtures and subtests: upstream XML otherwise omits some names and outcomes.
Local workers use regrtest's inherited file descriptor for result JSON, keeping
terminal-control bytes from curses out of the status channel.
The worst observed outcome wins when a case has multiple subtests. Whole-module
crashes and timeouts mark **all** its expected cases CRASH/TIMEOUT, including
earlier cases; partial progress cannot turn a crashed module into a pass.
Regrtest's faulthandler timeout exits are recognized as TIMEOUT even though its
parent labels them `WORKER_FAILED`.
Unreported cases remain NOTRUN, or ERROR when module setup fails. For pinned
oracle runs only, cases absent from that build's recorded collection become
SKIP; this adapter does not apply to Lucifer.

Regrtest's normal `--list-cases` branch does not initialize resource flags.
`collect --profile` additionally lists cases with those flags initialized by
the observer, and saves `resource-profile.json` without changing `expected.tsv`.
Cases omitted from both builds' resource-aware inventories are explicitly SKIP
when not observed during execution, including conditional timezone expansion.
This resource policy applies to every runtime; it never removes expected rows.

Resource policy enables ordinary subprocess, decimal, audio, console and curses
resources while disabling external network/URL access, GUI, large-file, CPU-heavy
and long-walltime resources. The latter two follow regrtest's distinction between
routine correctness and expensive stress. `extralargefile`, `tzdata`, `xpickle`
and `wantobjects` are already outside regrtest's `all` set. No large-memory budget
is supplied. Local loopback tests remain enabled. Skips remain SKIP, never PASS.
Use `--resources` and `--timeout` for separately labelled expanded runs.

The environment clears inherited `PYTHON*` options and fixes `PYTHONHASHSEED=0`,
`PYTHON_JIT=0`, `LC_ALL=C.UTF-8`, and `TZ=UTC`. Bytecode writing keeps its normal
enabled behavior, which import and compileall tests require; generated caches
are ignored by Git.
Regrtest's RNG seed is 0. The startup metadata records free-threaded capability
and GIL state after imports; the harness does not force the GIL on or off.
Per-case seconds are diagnostic elapsed times from upstream XML/the observer,
not benchmark measurements. Module failures, import skips, CPython-only
decorators and unavailable extensions are retained as baseline limitations;
this lane alone does not audit the promised full public C-API.

`results/libtest/<runtime>.tsv` and `expected.tsv` use the shared
`id\tstatus\tseconds` format, LF endings, and sorted IDs. Metadata beside each
runtime records portable commands, environment, timeout, expected-manifest hash,
per-module counts, and unexpected IDs. Raw XML, event logs, module-state JSON and
stdout/stderr remain in ignored `results/libtest/raw/<runtime>/` (or `--raw-dir`)
because upstream diagnostics can include local paths and account details.

Use `--shard INDEX/TOTAL --resume` to run bounded, disjoint stretches; shard
indices are zero-based stripes over sorted modules. Each invocation probes the
target, reruns the selected modules, and preserves other results. Without
`--resume`, other modules start at NOTRUN. Use enough shards to keep each
resource reservation below 20 minutes, and execute each invocation through the
host's required reservation mechanism. Do not overlap writers for one runtime.
`--modules` selects exact modules or package prefixes for diagnosis. Every
manifest still includes the complete expected denominator.

The standalone self-check script exercises startup exit 1, startup hang, missing
executables, module crashes/timeouts, expected failure, subtest failure, and
fixture-error accounting through the runner's command-line boundary. It also
checks parameterized/doctest collection, malformed XML, repeated IDs, and resource
omissions.

The initial collection contains 53,973 unique IDs across 538 modules per build:
53,799 listed by the free-threaded oracle and 53,771 by the GIL oracle. There
are 202 free-threaded-only and 174 GIL-only IDs; `inventory.json` preserves their
identities. Both collections completed without errors. The resource profile
omits 5,595 of those IDs from conditional generation, retaining them as explicit
skips in the full denominator. Empty/import-skipped module counts are 30 for the
free-threaded build and 77 for the GIL build.

### Recorded baseline

All 538 modules were attempted under each oracle with the settings above,
divided into module batches with resource reservations bounded to about 18
minutes. The manifests contain all 53,973 expected IDs:

| Runtime | PASS | FAIL | SKIP | XFAIL | NOTRUN | Pass rate |
|---|---:|---:|---:|---:|---:|---:|
| cpython-3.14.10 | 45,915 | 0 | 8,048 | 10 | 0 | 85.07% |
| cpython-3.14.10t | 45,900 | 1 | 8,059 | 13 | 0 | 85.04% |
| lucifer | 0 | 0 | 0 | 0 | 53,973 | 0.00% |

ERROR, XPASS, TIMEOUT and CRASH are zero in all three manifests. Lucifer's
executable is absent; every row is NOTRUN with zero seconds. The complete
outcomes are in [the manifests](../results/libtest/), including every skipped
and expected-failure ID. No skip or expected failure contributes to the pass
rate. Skips include resource-disabled cases, build-specific guards, unavailable
platform features/extensions, source-build-only checks, and upstream decorators.

The sole failure is
`test.test_build_details.CPythonBuildDetailsTests.test_base_interpreter` in the
free-threaded build. Its installed `build-details.json` names `python3.14`,
while the exercised binary is `python3.14t`. This failure was independently
reproduced without the observer; see [the upstream assertion](../third_party/cpython/Lib/test/test_build_details.py):

```sh
"$ORACLE_T" -m test -j 1 --timeout 180 --match test_base_interpreter test_build_details
```

The free-threaded `test_warnings` module also records an ENV_CHANGED diagnostic
for warning filters, with all 201 cases passing. Regrtest accepts this under its
default policy; the module state remains visible in the runtime metadata.

Verification included 16 harness self-checks, byte-for-byte comparison of all
2,435 vendored library files and the license against the fresh clone, and fresh
collection with normal bytecode settings matching both inventories and the
expected manifest exactly. Manifest checks cover headers, LF endings, sorted
unique IDs, complete coverage, finite nonnegative durations, interpreter build
and GIL state, and the absence of worker/protocol errors. Raw diagnostics remain
local; the committed metadata and manifests contain portable paths and IDs.

## NumPy: immutable full-suite acceptance

`tools/numpytest/PIN` pins NumPy 2.5.4, the latest stable PyPI release with
a `cp314t` manylinux x86-64 wheel when queried on 2026-11-02. It records both
the free-threaded artifact and the ordinary `cp314` comparison artifact,
including filenames, download URLs and SHA-256 from
[PyPI JSON](https://pypi.org/pypi/numpy/2.5.4/json).
The Python version for both oracles is CPython v3.14.10.

`requirements.txt` locks the release's complete
[test requirements](https://github.com/numpy/numpy/blob/v2.5.4/requirements/test_requirements.txt)
and transitive dependencies with PyPI wheel hashes. It includes pytest 9.1.0,
Hypothesis 6.152.1, Cython, pytest-cov, Meson, Ninja, pytest-xdist,
pytest-timeout, charset-normalizer and tzdata. The Darwin-only quaddtype
requirement retains its upstream marker. Additional pinned packages provide
pip, setuptools and meson-python for bootstrap and extension-building tests.

All commands below use the host's Python 3 for the driver. Supply external
directories for downloads, virtual environments and raw logs. Download the
two wheel URLs in `PIN`, then use pip's `download --require-hashes
--only-binary=:all: -r tools/numpytest/requirements.txt` for each oracle ABI.
The wheel cache must contain compatible dependencies for both `cp314` and
`cp314t`. Preparation recreates the supplied venv directory and installs only
hashed wheels from that cache:

```sh
python3 tools/numpytest/run.py prepare --oracle "$ORACLE_T/bin/python3.14t" \
  --venv "$VENV_T" --cache "$WHEEL_CACHE" --abi cp314t
python3 tools/numpytest/run.py prepare --oracle "$ORACLE/bin/python3.14" \
  --venv "$VENV" --cache "$WHEEL_CACHE" --abi cp314
```

Set `SITE_T` and `SITE` to the respective venv's `lib/python3.14t/site-packages`
and `lib/python3.14/site-packages`. Collection unions both oracle collections
into the expected denominator. Repeat collection with `--parallel` to establish
the parallel lane's denominator:

```sh
python3 tools/numpytest/run.py run --collect --python "$VENV_T/bin/python" \
  --runtime cpython-3.14.10t --site "$SITE_T" --cache "$WHEEL_CACHE" --work "$RAW"
python3 tools/numpytest/run.py run --collect --python "$VENV/bin/python" \
  --runtime cpython-3.14.10 --site "$SITE" --abi cp314 \
  --cache "$WHEEL_CACHE" --work "$RAW"
```

Run without `--collect` to execute the complete suite using `pytest --pyargs
numpy`, equivalent to NumPy's `full` label with slow tests included.
Use `--shard numpy/linalg` (or an installed test filename or `file.py::TestClass`)
to run one submodule
at a time; repeat `--shard` to group installed files. Results merge by node ID
into the runtime's manifest. Every shard
gets a fresh process and working directory. No tests or markers are filtered
out for the acceptance lane. The default whole-shard deadline is 540 seconds;
pytest's individual-test deadline is 600 seconds, matching upstream CI. Split
large submodules into test-file shards to stay within the whole-shard deadline.

The separate `--parallel` lane uses four shared-process threads with
`pytest-run-parallel==0.8.2`, mirroring
[the release's macOS free-threaded CI](https://github.com/numpy/numpy/blob/v2.5.4/.github/workflows/macos.yml)
and [the spin option translation](https://github.com/numpy/numpy/blob/v2.5.4/.spin/cmds.py).
It retains the plugin's default single iteration and serial fallback for
`thread_unsafe` cases; NumPy's explicit internally threaded tests are included.
Unlike that CI job's default fast selection, this lane includes slow tests.
Both lanes constrain OpenBLAS and OpenMP to one thread to avoid oversubscription.

The driver hashes the wheel archive and verifies every installed wheel payload
file except pip's rewritten `RECORD`, before and after each run. JSON evidence
alongside the manifests includes native-library hashes and the observed GIL
state after NumPy import and after all test imports. No setting forces the GIL
off. Raw JUnit XML and stdout/stderr stay in the external `--work` directory.
JUnit properties carry the original pytest node ID encoded as base64, preserving
parametrization, punctuation and Unicode. Only the known installation-root
prefix is removed, including in installed typing-data parameters, so IDs stay
portable. Public JSON records skip/failure reasons with absolute paths removed.

A future Lucifer uses the **same `SITE_T`**, through `PYTHONPATH`, and loads the
same extension binaries and bundled libraries. It must supply CPython 3.14t's
non-limited ABI; rebuilding NumPy against Lucifer headers would change this bar.
An absent or failing command produces every expected row as `NOTRUN`:

```sh
python3 tools/numpytest/run.py run --python bin/lucifer --runtime lucifer \
  --site "$SITE_T" --cache "$WHEEL_CACHE" --work "$RAW"
python3 tools/numpytest/run.py run --python bin/lucifer --runtime lucifer \
  --site "$SITE_T" --cache "$WHEEL_CACHE" --work "$RAW" --parallel
tools/scoreboard --suite numpytest --suite numpytest-parallel
make check-harness
```

Manifests are sorted LF TSV with `id`, `status`, `seconds`; `expected.tsv` uses
`NOTRUN` as its placeholder status. The scoreboard divides PASS by the complete
expected count, reports missing rows as NOTRUN and flags unexpected IDs. SKIP,
XFAIL and XPASS never count as PASS. Startup/import failures cannot establish
an expected denominator. A killed shard records its active test as TIMEOUT or
CRASH. An incremental report journal retains completed tests if JUnit cannot
be closed; unreported tests remain NOTRUN. Oracle failures and unavailable optional
resources are evidence to investigate; they do not weaken Lucifer's denominator.

`make check-harness` runs binary-level checks for missing and failing runtimes,
JUnit ID preservation, xfail, artifact tampering, empty collection, timeout
accounting and the scoreboard denominator. Run it through the execution policy
of the machine performing validation.

### Recorded baseline, 2026-11-02

Both oracle collections and the parallel collection contain the same **52,273**
IDs. Every runtime manifest contains exactly that set, with no duplicate,
unexpected or missing IDs. The oracle runs used five bounded shards each:
`test_multiarray.py`, three groups covering the other core test files, and
the remaining installed submodules. The exact selections are recorded in each
shard's JSON `shards` list. All shards completed within their 540-second limit;
the longest pytest session took 195.16 seconds. These are test-run durations,
not throughput benchmarks.

| Suite | Runtime | Expected | PASS | SKIP | XFAIL | XPASS | NOTRUN | PASS / expected |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| numpytest | cpython-3.14.10 | 52,273 | 50,135 | 2,106 | 30 | 2 | 0 | 95.91% |
| numpytest | cpython-3.14.10t | 52,273 | 50,049 | 2,192 | 30 | 2 | 0 | 95.75% |
| numpytest | lucifer | 52,273 | 0 | 0 | 0 | 0 | 52,273 | 0.00% |
| numpytest-parallel | cpython-3.14.10t | 52,273 | 50,049 | 2,192 | 30 | 2 | 0 | 95.75% |
| numpytest-parallel | lucifer | 52,273 | 0 | 0 | 0 | 0 | 52,273 | 0.00% |

**FAIL, ERROR, TIMEOUT and CRASH are zero in all cells.** Both free-threaded
lanes retained a disabled GIL after every shard. The parallel plugin classified
51,068 cases for parallel wrapping and 1,205 for serial fallback; these counts
include ordinary skipped cases. The parallel outcome map exactly matches the
serial free-threaded outcome map. No `thread_unsafe` tests were excluded.

Every artifact check verified **925 installed payload files**, including
**22 native binaries**, against the pinned wheel. All before/after payload
checks matched. Downloads, environments, JUnit XML and full logs remain outside
the repository; the committed JSON retains hashes, observed GIL states,
collection fingerprints, concurrency classifications and per-node reasons.
Shard JSON `counts` are cumulative manifest snapshots, not counts to add.

The largest skip categories are upstream CPU-dispatch requirements (704
`X86_V4` cases), unavailable Fortran compilers (609 general, 146 Fortran 77,
121 Fortran 90 cases) and optional mypy coverage (178 cases). The free-threaded
oracles additionally skip 84 ABI3 cases, as upstream specifies that
`Py_GIL_DISABLED` builds do not support ABI3. Other skips include unsupported
operation/dtype combinations, unavailable locales, optional packages and
upstream resource/platform exclusions. These remain coverage gaps rather than
passes; no exclusions were added to obtain the baseline.

The two upstream non-strict XPASS cases in every oracle cell are:

- `numpy/lib/tests/test_arraypad.py::TestLinearRamp::test_object_array`
- `numpy/tests/test_scripts.py::test_f2py[f2py]` (upstream reason: "Test is unreliable")

The harness returns nonzero for XPASS, FAIL, ERROR, TIMEOUT, CRASH or incomplete
collection, even when pytest itself accepts a non-strict XPASS. These baseline
commands therefore returned nonzero solely for the two recorded XPASS cases;
pytest returned zero. Neither case is relabelled PASS or removed.

All **ten binary-level harness checks passed**, including malformed startup
output and shard merging. The final content audit verified all seven TSV files:
exact expected coverage, sorted unique IDs, LF endings, finite nonnegative
durations, valid statuses, no unrun oracle cases and all-zero Lucifer scores.
Actual Lucifer interoperability remains unverified until its executable exists.

## Benchmarks

The benchmark panels are fixed before Lucifer exists. `results/bench/awfy/expected.tsv`
declares all 14 canonical AWFY rows; `results/bench/pyperformance/expected.tsv`
declares nine Python breadth rows. Each runtime has a sorted LF TSV with the same
`id`, `status`, `seconds` columns. Missing or failing interpreter commands produce
every expected row as NOTRUN. Missing dependencies also remain NOTRUN with a reason
in the status JSON. Only PASS contributes to `tools/scoreboard` or a timing factor.
These benchmark outcomes are separate from language and C-API conformance.

All drivers run with the host's Python 3 and use only its standard library. The
interpreter under test is a command supplied with `--python`; shell words are
parsed without executing a shell. The timed processes use a fixed hash seed of
zero, UTC, C.UTF-8, `PYTHON_JIT=0`, and single-threaded BLAS/OpenMP settings. Each
child has a bounded process group, including pyperf's worker subprocesses. Acquire
the host's prescribed isolation externally: build isolation for installation,
test isolation for self-checks/capability checks, timing isolation for benchmarks.
Split AWFY by benchmark so each timing hold remains short. Do not nest isolation.
Both drivers also cap an invocation's benchmark work at 15 minutes; unfinished
rows remain visible rather than extending a timing hold indefinitely.

### AWFY

`third_party/awfy/VERSION` pins
[are-we-fast-yet](https://github.com/smarr/are-we-fast-yet/tree/74306fec151070fd07157cefeacf19e7e0bcdc89)
at `74306fec151070fd07157cefeacf19e7e0bcdc89`. Its 25 Python files, MIT notices,
upstream `LICENSE.md` (including inherited-license information), and `AUTORS.md`
are unchanged. The copy comes from the official commit archive, independently of
the ignored reference checkout. No reference checkout is executed.

`tools/bench/awfy` launches one fresh process for each runtime/benchmark. Its
`awfy_worker.py` adapter calls upstream `Run.run_benchmark` and **unchanged
`Run.measure`**, preserving the timed body, answer verification and integer
microsecond samples. Only the outer iteration loop changes, stopping cleanly
between verified iterations. All runtimes receive the donor's canonical inner
work: DeltaBlue 12000, Richards 100, Json 100, CD 250, Havlak 1500, Bounce 1500,
List 1500, Mandelbrot 500, NBody 250000, Permute 1000, Queens 1000, Sieve 3000,
Storage 1000, Towers 600.

The methodology follows the donor Smalltalk VM's
`tools/perf/all-runtimes/{sample,analyze}.py` at `origin/master`
(`a71c8c05f32d7b01191e60a9ab8c773959613ef6`) and its
`vm/results/all-runtimes-2026-11-01/README.md`:

- At most 1000 iterations; stop after at least 20 iterations and 10 seconds of
  summed iteration time. A separate wall timeout defaults to 180 seconds per row.
- Consider suffixes with at least 12 samples. Use median ±max(1% of median,
  3 × median absolute deviation). The first suffix sample and every rolling
  five-sample median must remain inside that band.
- Reject suffixes with both a significant two-sided 95% Kendall monotonic trend
  (normal approximation, conservative ties) and a greater-than-1% half-median
  shift. Choose the earliest suffix that remains.
- Never-steady rows use the second-half median, including the middle sample for
  odd lengths, explicitly flagged `fallback`. They retain valid checked work.
- Report the median and distribution-free 95% order-statistic interval. A blank
  interval bound means unbounded with fewer than six retained samples. Also keep
  first iteration, first/median ratio, warm-up elapsed time and excess cost, and
  process wall time minus summed iterations as startup/harness/teardown overhead.

The CSV contains all expected rows; `.data.json` retains every iteration,
stdout/stderr, source fingerprint and executable hash/capabilities. Single-process
intervals describe within-launch samples; they do **not** estimate between-launch
variation. This initial scoreboard is not a noise-calibrated regression gate or
a new measurement of the Smalltalk runtime.

Example commands, inside external timing isolation (`ORACLE_GIL` and `ORACLE_T`
are the pinned oracle executables):

```sh
python3 tools/bench/awfy --python "$ORACLE_GIL" --runtime cpython-3.14.10 --benchmarks Bounce
python3 tools/bench/awfy --python "$ORACLE_T" --runtime cpython-3.14.10t --benchmarks Bounce
# Repeat for the other 13 predeclared names; shards retain other completed rows.
python3 tools/bench/awfy --python bin/lucifer --runtime lucifer
python3 tools/bench/compare --suite awfy
```

Shard updates reject a changed source or runtime fingerprint. Run a full panel
or use a fresh output directory to change either. A missing-to-working target
starts a fresh panel automatically, so Lucifer can climb from zero. Omitting
`--benchmarks` runs the whole panel. Running one named shard does not mark the
other expected rows as passed.

### pyperformance

[pyperformance 1.14.0](https://pypi.org/project/pyperformance/1.14.0/) is installed
from its published wheel into a separate venv for each oracle. Every selected
dependency is pinned with SHA-256 hashes in `tools/bench/requirements.lock`;
installation uses `--require-hashes --only-binary=:all: --no-deps`. The closure is
pyperformance 1.14.0, pyperf 2.10.0, packaging 25.0, psutil 7.2.2 and Docutils
0.18.1. psutil deliberately advances the reference tree's 7.0.0 pin to obtain
published cp314t wheels rather than introduce unpinned native build dependencies.
Docutils keeps the benchmark's own requirements pin.

| Fixed selector / emitted ID | Coverage and correctness preflight |
|---|---|
| `comprehensions` | Builtin lists/dicts, sorting, any, enum/dataclass objects; assert the retained widget IDs and upstream size check |
| `regex_dna` | Strings and regular expressions; upstream exact full-input count and length tuple |
| `float` | Math, slots and numeric loops; unit-vector norms, coordinate maxima, full-size oracle result with 1e-12 libm tolerance |
| `richards` | Object dispatch and scheduling; require its full checked packet/hold counts via `run(1) is True` |
| `generators` | Recursive yield-from; compare all 100000 emitted integers with the input range |
| `async_generators` | Recursive async iteration and event loop; compare all 100000 emitted integers |
| `json_loads` | Standard-library serialization; round-trip the benchmark's dictionaries, tuple-as-list and fixed-seed data |
| `python_startup` | Fresh-process startup; additionally verify a child result, exit code and empty stderr |
| `docutils` | Application workload with real reStructuredText documentation; all 49 documents must render and match the pinned oracle's combined output checksum |

These nine selectors each emit exactly one result, avoiding an ambiguous selector
versus measurement denominator. AWFY does not cover these Python mechanisms.
The full-size float answer and Docutils output checksum were frozen against the
pinned CPython v3.14.10 oracle and locked dependencies. Docutils hashes each
relative UTF-8 filename, NUL, rendered HTML, NUL in sorted filename order; all 49
documents render successfully. This preflight rejects rendering errors that the
upstream timed workload would otherwise suppress.
Docutils supplies application breadth without an old Django dependency stack;
legacy `2to3`, xDSL's conflicting pins and the wider dependency-heavy panel are
outside this **declared initial subset**, not silently omitted measurements.
Preflights run under the target before timing; embedded upstream checks remain
enabled (`-O` is not used). Preflights sample workload correctness; benchmarks
whose timed body ignores return values are not continuous differential tests.
The timed source is unchanged, and each selected source directory is hashed.

The driver invokes the installed release's `bm_*/run_benchmark.py` entry points
with upstream pyperf, rather than pyperformance's environment manager, which
would perform additional unhashed installs. Each row uses three independent
worker processes, two discarded warmups and three retained values per worker,
with a 0.05-second minimum calibrated value. The default per-row timeout is 180
seconds. This is a bounded foundation run, not pyperf's rigorous setting;
instability warnings stay in `.status.json`. JSON retains pyperf's calibration,
raw values, worker metadata and warmups, with identifying names/paths removed.
Actual single-thread/free-threaded capability is recorded; importing the locked
environment or preflight dependencies must not re-enable the free-threaded GIL.

```sh
# Inside external build isolation:
python3 tools/bench/pyperformance setup --python "$ORACLE_GIL" --venv "$VENV_GIL"
python3 tools/bench/pyperformance setup --python "$ORACLE_T" --venv "$VENV_T"
# Inside external timing isolation:
python3 tools/bench/pyperformance run --python "$VENV_GIL/bin/python" --runtime cpython-3.14.10
python3 tools/bench/pyperformance run --python "$VENV_T/bin/python" --runtime cpython-3.14.10t
python3 tools/bench/pyperformance run --python bin/lucifer --runtime lucifer
# Read-only comparison, no timing:
python3 tools/bench/compare --suite pyperformance
"$VENV_GIL/bin/python" -m pyperf compare_to results/bench/pyperformance/cpython-3.14.10.json results/bench/pyperformance/cpython-3.14.10t.json --table
```

`<runtime>.json` is standard pyperf JSON for measured rows. A missing interpreter
has an empty `benchmarks` array plus all nine NOTRUN manifest rows; pyperf itself
cannot load an empty suite, so use the stdlib compare driver for that case.
`<runtime>.status.json` retains reasons, preflight outcomes, warnings, source and
lock hashes, runtime capabilities and the exact measurement plan.

### Comparisons, runtime availability and checks

`tools/bench/compare` writes `factors.csv` (all expected runtime/row pairs) and
`summary.csv` (coverage and complete-panel geometric mean). Every ratio is
**runtime elapsed time / GIL CPython elapsed time**, lower is faster. pyperformance
ratios use medians of retained pyperf values. A partial runtime has per-row
diagnostics and a blank aggregate, never a fabricated 1.0 score. AWFY and
pyperformance stay separate.

The required runtimes are the two separately built official CPython v3.14.10
oracles and `bin/lucifer`. The initial host has no preinstalled PyPy executable
on PATH or in the usual system install directories; none is installed for this
task. Lucifer is absent and therefore starts at 0/14 and 0/9. PyPy's optional
comparison remains unverified until an existing executable is supplied.

`tools/bench/runtime-info` records executable hashes, ABI/build flags, GIL state,
and `sys._jit` availability/enabled state with `PYTHON_JIT=0` and `1`. CPython's
[v3.14.10 configure documentation](https://github.com/python/cpython/blob/v3.14.10/Doc/using/configure.rst)
states that the default build excludes the experimental JIT. `PYTHON_JIT=1` can
enable it only when compiled with `--enable-experimental-jit=yes` or `yes-off`.
Both pinned oracle binaries reported JIT unavailable and disabled with either
environment setting; the probes are retained in `results/bench/runtimes.json`.
A separate JIT-capable build and paired on/off measurements are follow-up work;
setting the environment alone must never be labelled a JIT result.

`make check-harness` runs binary-level driver checks: missing and failing target
commands emit the full NOTRUN denominator; wrong answers, signals, timeouts and
empty successful output stay visible; missing comparisons and scoreboards stay
at zero. It also checks constant, warming and drifting sample series, fallback
selection and median intervals. Run it inside the prescribed external test
isolation. Identifying local paths and hostnames are omitted from public artifacts.

### Initial recorded results (2026-11-02)

| Runtime | AWFY PASS / expected | pyperformance PASS / expected | AWFY time / CPython | pyperformance time / CPython |
|---|---:|---:|---:|---:|
| cpython-3.14.10 | 14/14 | 9/9 | 1.000 | 1.000 |
| cpython-3.14.10t | 14/14 | 9/9 | 1.189 | 1.108 |
| lucifer | 0/14 | 0/9 | missing | missing |

AWFY retained 560 verified iterations across 28 fresh processes. GIL CPython's
Storage row is a never-steady second-half fallback; the other 27 rows found a
steady suffix. pyperformance retained 162 values across 54 measurement workers.
Its short-run instability warnings affect seven GIL rows and six free-threaded
rows and remain in the status JSON; these factors are not a calibrated regression
gate. All 23 Lucifer rows are NOTRUN, with no imputed timing factors.

The four harness self-tests passed. Final artifact checks verified manifest
denominators, recorded medians against raw samples, worker/value counts, missing
cells, dependency/source fingerprints and omitted local paths. Upstream pyperf
successfully loaded and compared both exported oracle JSON suites.

## Production package corpus: `tools/pypi`

The corpus runs upstream tests from pinned release sources against installed
release wheels. The host-Python driver uses only the standard library; pytest
and test dependencies run inside the target environment. All archives and wheel
dependencies have SHA-256 pins in `tools/pypi/corpus.json` and
`tools/pypi/requirements.lock`. Downloads, extracted sources, environments,
pytest logs and incremental outcome journals belong in external directories.
No reference checkout under `vendor/` is executed.

This is a **24-package** revision of [the proposed 20](research/05-oracles-and-benchmarks.md#4-a-first-corpus-of-20-popular-pypi-package-suites).
It retains packaging, urllib3, requests, cryptography, PyYAML, MarkupSafe,
Jinja2, Pillow, lxml, SQLAlchemy, NetworkX, SymPy and simplejson. NumPy already
has its dedicated acceptance lane. pandas and SciPy's large scientific suites,
Django's framework/database matrix, setuptools' build-system suite, pytest's
self-hosting suite and cffi's compiler-driven suite are outside this initial
corpus; they remain future coverage, without an implied passing result.
The required attrs, more-itertools, sortedcontainers, pyparsing, pydantic-core,
msgpack, regex, python-dateutil, pytz, click and rich add distinct collection,
parser, validation, binary-format, Unicode, timezone and terminal mechanisms.
Pydantic-core supplies the requested Pydantic/native validation coverage;
Pydantic's separate Python wrapper suite is unverified.

| Proposed entry moved out of this corpus | Recorded reason |
|---|---|
| NumPy | Its full wheel acceptance suite already has a dedicated scoreboard above. |
| pandas, SciPy | Large scientific dependency matrices overlap that NumPy lane; the required additional production packages take priority in this bounded first corpus. Their own suites remain unverified here. |
| Django | SQLAlchemy and Jinja2 provide the initial ORM/template coverage; Django's additional framework and optional database matrix is deferred. |
| setuptools | Its subprocess build/install tests need a separately controlled build and download environment beyond this immutable-wheel lane. |
| pytest | Self-hosting runner/plugin tests are deferred in favor of the required application-library mechanisms. |
| cffi | Its compiler-driven foreign-call suite merits a separate build-enabled ABI lane; cryptography uses the pinned cffi wheel as a dependency here. |

Rich and sortedcontainers omit their tests from the published sdists. Their
sources therefore use the matching upstream release-tag archives, with hashes
recorded alongside the PyPI wheel hashes. All other test sources are PyPI
sdists. Sources and test selectors are explicit in the corpus lock.

Choose absolute external directories and create each oracle environment under
the execution policy of the machine doing validation. Use a fresh environment
directory when changing the dependency lock or package profile:

```sh
python3 tools/pypi/run.py setup --python "$ORACLE_GIL" --env "$ENV_GIL" \
  --cache "$CACHE" --work "$WORK"
python3 tools/pypi/run.py setup --python "$ORACLE_T" --env "$ENV_T" \
  --cache "$CACHE" --work "$WORK"
# Repeat collection and execution for each package in corpus.json.
python3 tools/pypi/run.py collect --python "$ENV_GIL/bin/python" \
  --package attrs --runtime cpython-3.14.10 --cache "$CACHE" --work "$WORK"
python3 tools/pypi/run.py run --python "$ENV_GIL/bin/python" \
  --package attrs --runtime cpython-3.14.10 --cache "$CACHE" --work "$WORK"
python3 tools/pypi/run.py run --python "$ENV_T/bin/python" \
  --package attrs --runtime cpython-3.14.10t --cache "$CACHE" --work "$WORK"
python3 tools/pypi/run.py run --python bin/lucifer \
  --package attrs --runtime lucifer --cache "$CACHE" --work "$WORK"
tools/scoreboard --results results/pypi
make check-harness
```

Each collection must succeed, contain unique IDs and agree with its frozen
`results/pypi/<package>/expected.tsv`. Runs preserve the entire inventory in
sorted LF TSV; missing or failing target commands produce all NOTRUN. PASS is
the only numerator. SKIP, XFAIL and XPASS remain separate. JSON evidence records
source/lock and harness hashes, collection fingerprints, return codes, GIL state,
loaded native-module hashes and reasons.
A partial journal preserves completed outcomes after interruption; the active
case becomes TIMEOUT or CRASH and unreported cases remain NOTRUN. The default
whole-package deadline is 900 seconds, with a 60-second individual pytest
limit. Run each package in a separate externally serialized test invocation.
Suite elapsed times are diagnostics, never application speed measurements.

The environment fixes hash seed zero, UTC, C.UTF-8 and one BLAS/OpenMP thread.
Matplotlib uses a private configuration cache. Rich unsets COLUMNS/LINES so its
descriptor-fallback tests retain control of terminal dimensions.
It does not force the free-threaded GIL off: the final GIL state is observed.
An enabled GIL in a free-threaded build makes the driver return nonzero.
External-network markers are skipped. Requests' HTTP-service fixture tests stay
in the denominator as SKIP; its offline object/protocol tests run. Loopback
fixtures used by upstream suites are permitted. SQLAlchemy uses SQLite and
excludes asynchronous database drivers. SymPy uses its core, logic and matrices
suites; sortedcontainers runs its deterministic coverage suite, leaving the
separate randomized stress programs outside this declared lane. Upstream
ordinary marker defaults remain in effect. The two unmarked urllib3 HTTP/2
TARPIT probes also remain SKIP because they use an external reserved address.
Its pytest timeout markers have a five-second minimum to keep the upstream
0.1-second deadline from interrupting instrumented failure reporting; socket
timeouts and assertions are unchanged.

The unittest adapters call upstream loaders: more-itertools' `load_tests`
includes doctests, simplejson's `all_tests_suite` wraps the same cases in native
and Python modes, lxml's `get_test_files`/`get_test_cases` applies the release's
default level and includes its doctests, pytz calls its `test_suite`, and regex
uses its unittest cases. The other packages use pytest. Adapters and import-path
setup leave upstream assertions and production code unchanged. Native extension
imports are required for the ten C package entries; the final loaded-module
hashes are retained. No system packages were installed for this corpus.

For a future working Lucifer, `--site` can point at the free-threaded oracle's
installed `site-packages`, reusing the exact pinned cp314t extension binaries.
That interoperability remains unverified until Lucifer exists.


### Corpus environment details

The artifact lock covers 116 distributions and 138 published wheels for Linux
x86-64, plus the test-source archives. Installations use only those exact hashes,
without dependency resolution or source builds. Rich uses Pygments 2.19.2 from
its release `poetry.lock`. The pydantic-core sdist lacks a dependency lock and
its release-tag lock was unavailable; its documentation tooling uses the
available [upstream main lock](https://github.com/pydantic/pydantic-core/blob/main/uv.lock)
versions: pytest-examples 0.0.15, Black
24.10.0 and Ruff 0.9.2. Matching that tooling snapshot to the release tag remains
**unverified**. Those tools execute the unchanged examples and assertions.

urllib3's own `pyproject.toml` and `uv.lock` select the
[urllib3 Hypercorn fork](https://github.com/urllib3/hypercorn/tree/d1719f8c1570cbd8e6a3719ffdb14a4d72880abb)
at `d1719f8c1570cbd8e6a3719ffdb14a4d72880abb`. Its SHA-256-pinned source is
loaded only as a test-server dependency, providing the TLS and proxy scope
extensions absent from published Hypercorn. The production urllib3 wheel is
unchanged. The lock and JSON evidence record this source dependency explicitly.

NetworkX gets separate environments matching its default dependency selection:
NumPy, SciPy, pandas and Matplotlib are present; optional lxml and lxml-html-clean
are absent. This prevents an optional XML extension from changing the GIL state
of the graph run. fontTools uses its published pure Python wheel because its
compiled optional accelerator enables the GIL. NetworkX still includes doctests
and image comparisons. Create its environments with:

```sh
python3 tools/pypi/run.py setup --package networkx --python "$ORACLE_GIL" \
  --env "$ENV_NX_GIL" --cache "$CACHE" --work "$WORK"
python3 tools/pypi/run.py setup --package networkx --python "$ORACLE_T" \
  --env "$ENV_NX_T" --cache "$CACHE" --work "$WORK"
# Use these commands for NetworkX's run steps above.
python3 tools/pypi/run.py run --package networkx --python "$ENV_NX_T/bin/python" \
  --runtime cpython-3.14.10t --cache "$CACHE" --work "$WORK"
```

The full expected inventory retains five GraphML doctests that upstream omits
when optional lxml is absent. An exact allowlist maps those to SKIP. SQLAlchemy
similarly omits nine tutorial methods on free-threaded builds because its
`DocTest.__requires__` includes `gil_enabled`; an exact allowlist preserves them
as SKIP in the common inventory. All other unreported cases remain NOTRUN.
The original collections and journals stay in the external work directory.

pydantic-core's custom parameter labels include addresses, random UUIDs and
unordered Rust debug output. Its stable IDs therefore use pytest's parameter
names and indices. Runtime JSON retains the original IDs separately. PyYAML's
legacy collector gives three no-data cases the same node ID; the adapter appends
their original function names. Known source and installation-root prefixes are
normalized inside all IDs, preserving portability without comparing addresses.
Four packaging file-URI fixture labels also use parameter indices; their original
labels remain only in external journals to meet the public-path rule.

`results/pypi/decisions.json` records environment repairs, upstream failures and
confirmed flakiness. The published lxml cp314t extension **enables the GIL**.
Its row is a functional free-threaded-ABI run with that observed fallback;
no GIL-off lxml compatibility result is claimed. The driver reports this
capability gap with a nonzero exit even when its tests pass. No setting forces
an extension to run without the GIL and no extension is rebuilt for this lane.

### Recorded package baseline (2026-11-02)

Kind describes the principal package implementation; pure packages can have
native dependencies. Rates count PASS alone against every expected case.
Every Lucifer row is NOTRUN: **0 / 139,582 (0.00%)**.

| Package | Version | Kind | Expected tests | CPython PASS / expected | CPython t PASS / expected | Scope and exclusions |
|---|---|---|---:|---:|---:|---|
| [attrs](https://pypi.org/project/attrs/26.1.0/) | 26.1.0 | pure | 1,387 | 1,380 (99.50%) | 1,380 (99.50%) | Platform/type-checker skips; 2 upstream XFAIL. |
| [click](https://pypi.org/project/click/8.5.0/) | 8.5.0 | pure | 2,016 | 1,991 (98.76%) | 1,991 (98.76%) | Upstream ordinary unit selection; stress marker excluded; platform/resource skips and 1 XFAIL. |
| [cryptography](https://pypi.org/project/cryptography/50.0.2/) | 50.0.2 | C | 4,681 | 4,486 (95.83%) | 4,486 (95.83%) | Full Python suite; optional vectors, backend algorithms and resource cases SKIP. |
| [jinja2](https://pypi.org/project/jinja2/3.1.6/) | 3.1.6 | pure | 909 | 909 (100.00%) | 909 (100.00%) | Full unit suite using upstream prepend import mode. |
| [lxml](https://pypi.org/project/lxml/6.1.3/) | 6.1.3 | C | 2,051 | 2,000 (97.51%) | 2,000 (97.51%) | Upstream default-level unittest/doctests; **t enables GIL**. |
| [markupsafe](https://pypi.org/project/markupsafe/3.0.4/) | 3.0.4 | C | 80 | 79 (98.75%) | 79 (98.75%) | Python/native equivalence cases; 1 upstream SKIP. |
| [more-itertools](https://pypi.org/project/more-itertools/11.2.1/) | 11.2.1 | pure | 930 | 925 (99.46%) | 925 (99.46%) | Upstream unittest loader includes doctests; 5 all-skipped doctest groups. |
| [msgpack](https://pypi.org/project/msgpack/1.2.4/) | 1.2.4 | C | 161 | 160 (99.38%) | 159 (98.76%) | Native/fallback tests; upstream build/GIL skips retained. |
| [networkx](https://pypi.org/project/networkx/3.7/) | 3.7 | pure | 10,914 | 10,110 (92.63%) | 10,110 (92.63%) | Default dependency environment, doctests/image checks; optional XML cases SKIP; 741 upstream XFAIL. |
| [packaging](https://pypi.org/project/packaging/26.3/) | 26.3 | pure | 62,423 | 62,423 (100.00%) | 62,423 (100.00%) | Upstream default non-property selection; full 62,423-case inventory. |
| [pillow](https://pypi.org/project/pillow/13.0.0/) | 13.0.0 | C | 5,738 | 5,608 (97.73%) | 5,608 (97.73%) | Full pytest suite including fuzz-regression inputs; feature/resource skips, 3 XFAIL. |
| [pydantic-core](https://pypi.org/project/pydantic-core/2.50.2/) | 2.50.2 | C | 6,171 | 6,043 (97.93%) | 6,043 (97.93%) | Full Python suite, documentation examples and upstream fast Hypothesis profile; 8 XFAIL. |
| [pyparsing](https://pypi.org/project/pyparsing/3.3.3/) | 3.3.3 | pure | 2,094 | 2,094 (100.00%) | 2,094 (100.00%) | Full unit suite and tiny-language examples, including Matplotlib cases. |
| [python-dateutil](https://pypi.org/project/python-dateutil/4.0.0/) | 4.0.0 | pure | 5,343 | 5,063 (94.76%) | 5,063 (94.76%) | Full suite including properties; 1 upstream UTC FAIL and 17 XFAIL; timing flakiness recorded. |
| [pytz](https://pypi.org/project/pytz/2026.6/) | 2026.6 | pure | 208 | 208 (100.00%) | 208 (100.00%) | Own unittest loader and doctests. |
| [pyyaml](https://pypi.org/project/pyyaml/6.0.3/) | 6.0.3 | C | 2,608 | 2,608 (100.00%) | 2,608 (100.00%) | Upstream legacy collection, Python and LibYAML modes. |
| [regex](https://pypi.org/project/regex/2026.9.29/) | 2026.9.29 | C | 101 | 101 (100.00%) | 101 (100.00%) | Own unittest cases, preserving internal subcases. |
| [requests](https://pypi.org/project/requests/2.34.2/) | 2.34.2 | pure | 633 | 410 (64.77%) | 410 (64.77%) | Offline HTTP/object subset; service fixtures and external-address probes SKIP. |
| [rich](https://pypi.org/project/rich/15.0.0/) | 15.0.0 | pure | 981 | 956 (97.45%) | 956 (97.45%) | Full unit suite; optional Jupyter/terminal cases SKIP; release Pygments pin. |
| [simplejson](https://pypi.org/project/simplejson/4.2.0/) | 4.2.0 | C | 492 | 430 (87.40%) | 430 (87.40%) | Native and Python modes; debug-build, Python 2/3.15 and native-only cases SKIP. |
| [sortedcontainers](https://pypi.org/project/sortedcontainers/2.4.0/) | 2.4.0 | pure | 291 | 291 (100.00%) | 291 (100.00%) | All deterministic coverage modules; separate randomized stress programs excluded. |
| [sqlalchemy](https://pypi.org/project/sqlalchemy/2.1.6/) | 2.1.6 | C | 23,740 | 23,125 (97.41%) | 23,055 (97.11%) | Synchronous SQLite; other server dialects/async drivers excluded; 9 GIL-only tutorials SKIP on t. |
| [sympy](https://pypi.org/project/sympy/1.14.0/) | 1.14.0 | pure | 2,921 | 2,816 (96.41%) | 2,816 (96.41%) | Core, logic and matrices; other modules excluded; upstream slow/tooslow defaults, 31 XFAIL. |
| [urllib3](https://pypi.org/project/urllib3/2.8.0/) | 2.8.0 | pure | 2,709 | 2,111 (77.93%) | 2,111 (77.93%) | Loopback servers and offline cases; 593 SKIP, 2 XFAIL, 3 upstream FAIL. |

The common inventory contains **139,582 tests**. The remaining
oracle failures are retained as FAIL, without new xfails or weakened assertions:
one python-dateutil UTC-abbreviation case and three urllib3 cases (two gzip
trailing-garbage flushes and closed-socket SSL validation order). Both oracles
reproduce them. The final urllib3 evidence records zlib 1.3.2 and OpenSSL 3.6.5.
The three initial free-threaded dateutil Hypothesis deadline failures disappeared
on an unchanged repeat and are recorded as timing flakiness.

Except for lxml's explicit GIL fallback, every final t run observed a disabled
GIL after testing. FAIL, SKIP and XFAIL contribute zero to the numerator.
All expected rows are present; no oracle case remains NOTRUN, ERROR, TIMEOUT or
CRASH. The initial NetworkX crash and environment failures are superseded only
by the completed repaired-environment runs recorded in the decision log.

Runtime JSON identifies the dependency lock actually used. Exact hash-addressed
lock snapshots under `results/pypi/locks/` retain earlier completed baselines
from the environment-repair sequence. To reconstruct an earlier environment,
download that snapshot's wheels into the external cache using pip
`--require-hashes --only-binary=:all: --no-deps`, then pass `--lock SNAPSHOT`
to setup and run. The current lock remains the default. Each future invocation preserves
its lock snapshot automatically.

`make check-harness` passed, including all **12 package-corpus binary checks**:
missing/failing/malformed startup, real process interruption, retained completed
outcomes, symlink-preserved environments, empty collection, explicit upstream
collection skips, hash tampering, scoreboard denominators and offline networking.
The final content audit verifies all 96 TSVs, 72 runtime records, dependency/source
hashes, sorted unique inventories, LF endings, statuses, finite nonnegative
durations, full coverage and zero Lucifer passes. No system packages were
installed. Actual Lucifer execution remains unverified until its binary exists.
