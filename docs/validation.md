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

## Reference coverage audit and experimental JIT (2026-11-02)

This section supersedes the initial dependency/resource limitations above.
The original manifests were audited before changing any outcomes; the complete
grouped reasons are preserved in [skip-audit-before.json](../results/deps/skip-audit-before.json).
The original libtest JSON recorded module states but omitted individual skip
reasons. Its original observer logs supplied those reasons; current libtest JSON
now preserves sanitized per-case reasons and whole-module skip reasons.
`tools/validation/skip-audit` reads the manifests and JSON, counts only SKIP
rows, and groups every distinct reason. `--legacy-raw` recovers the initial
observer evidence when auditing an older snapshot. Module skips with no
collectible IDs are reported separately and do not inflate the case denominator.

### Original skip groups

Counts are cases, in ordinary/free-threaded order. “Environment and OS features”
includes missing locales, terminal devices, privileges, memory budgets and kernel
protocols. Build-configuration and upstream applicability guards remain explicit.

| Group | libtest GIL | libtest t | NumPy GIL | NumPy t |
|---|---:|---:|---:|---:|
| CPU features | 0 | 0 | 705 | 705 |
| Environment and OS features | 406 | 404 | 24 | 24 |
| Missing dependencies | 62 | 62 | 1073 | 1073 |
| Other platforms | 760 | 761 | 7 | 7 |
| Resource policy | 5835 | 5834 | 0 | 0 |
| Upstream declarations and build guards | 985 | 998 | 297 | 383 |

The **5,595 conditional-generation omissions** are not all caused by our
explicit `-cpu,-largefile,-walltime,-gui` flags. They consist of 2,508 datetime
timezone cases, 3,016 cross-version pickle cases, 27 socketserver cases, 16
urllib2net cases, 12 urllibnet cases, eight smtpnet cases, four PEG generator
cases and four ZIP64 cases. Upstream `all` itself excludes `tzdata`, `xpickle`,
`extralargefile` and `wantobjects`; see
[the pinned resource definitions](../third_party/cpython/Lib/test/libregrtest/utils.py).
The widened default is `all,-network,-urlfetch,tzdata,xpickle`.

### Installed dependencies and execution policy

The following Arch packages, including transaction dependencies, were installed
with the system package manager. Versions describe this audit, not a moving
installation requirement. No system configuration was changed.

| Package | Version |
|---|---|
| clang19 | 19.1.7-2 |
| compiler-rt19 | 19.1.7-2 |
| gcc-fortran | 16.2.1+r23+gd564253eb6c8-1 |
| glibc-locales | 2.44+r50+g1848099f063e-1 |
| libfontenc | 1.1.9-1 |
| libxfont2 | 2.0.9-1 |
| libxkbfile | 1.2.0-1 |
| libxss | 1.2.5-1 |
| llvm19 | 19.1.7-4 |
| llvm19-libs | 19.1.7-4 |
| net-tools | 2.10-3 |
| tcl | 8.6.16-1 |
| tcsh | 6.24.16-1 |
| tk | 8.6.16-1 |
| xdg-utils | 1.2.1-2 |
| xorg-fonts-encodings | 1.1.0-2 |
| xorg-server-common | 21.1.25-2 |
| xorg-server-xvfb | 21.1.25-2 |
| xorg-setxkbmap | 1.3.5-1 |
| xorg-xauth | 1.1.5-1 |
| xorg-xkbcomp | 1.5.0-1 |

The following Python packages were installed into the external environments.
The locks retain wheel/source hashes as well as these versions.

| Environment | Package | Version |
|---|---|---|
| JIT benchmark venv | docutils | 0.18.1 |
| JIT benchmark venv | packaging | 25.0 |
| JIT benchmark venv | psutil | 7.2.2 |
| JIT benchmark venv | pyperf | 2.10.0 |
| JIT benchmark venv | pyperformance | 1.14.0 |
| both test venvs | PyInstaller | 6.22.3 |
| both test venvs | altgraph | 0.17.5 |
| both test venvs | ast-serialize | 0.13.1 |
| both test venvs | big-O | 0.11.0 |
| both test venvs | cffi | 2.1.1 |
| both test venvs | librt | 0.16.0 |
| both test venvs | llvmlite | 0.50.0 |
| both test venvs | mypy | 2.4.1 |
| both test venvs | mypy_extensions | 1.1.0 |
| both test venvs | numba | 0.68.1 |
| both test venvs | numpy-quaddtype | 1.0.0 |
| both test venvs | pathspec | 1.1.1 |
| both test venvs | pycparser | 3.12 |
| both test venvs | pyfakefs | 6.3.0 |
| both test venvs | pyinstaller-hooks-contrib | 2026.9 |
| both test venvs | typing_extensions | 4.16.0 |

The additional PyPI test dependencies are hash pinned in
[optional-requirements.txt](../tools/numpytest/optional-requirements.txt):
mypy 2.4.1, CFFI 2.1.1, PyInstaller 6.22.3, Numba 0.68.1, llvmlite 0.50.0,
numpy-quaddtype 1.0.0 and pyfakefs 6.3.0, plus their recorded dependency closure.
They were installed with `--require-hashes --no-deps --only-binary=:all:` into
both existing test venvs; `pip check` checked the closure. The source-only
big-O 0.11.0 dependency is separately
[hash pinned](../tools/libtest/optional-requirements.txt) and installed with
`--no-deps --no-build-isolation` using the already pinned build helpers.
The NumPy wheel and its 925 payload files remain immutable. Libtest uses the
same oracle executables through those dependency-complete venvs. NumPy now
explicitly sets `NPY_RUN_MYPY_IN_TESTSUITE=1`, so installing mypy enables the
typing checks rather than replacing one skip with another.

Both test drivers preserve executable symlinks when making commands absolute;
resolving a venv executable would silently discard its environment. The libtest
binary checks exercise this with a real venv. NumPy preparation now includes
the optional dependency lock, so its wheel cache must include that lock's
artifacts and the NumPy source archive in `PIN`. Preparation verifies that
archive's hash and copies only its f2py documentation examples to the sibling
`site-packages/doc/` directory that upstream's tests already search. It neither
rebuilds NumPy nor changes wheel payload files. Their portable file hashes are
recorded and checked before/after each shard. A failed startup clears all resumed
shard results to NOTRUN.
Shard diagnostics carry an explicit timestamp; the skip audit selects the latest
overlapping reason independently of filesystem modification times.

CPU, walltime, GUI and large-file resources are enabled. GUI collection and
execution use a private Xvfb display. Free disk space was checked first
(71 GiB available); large-file tests use at most two 2.5 GB files, below the
10 GB working-data limit, and are run separately from other disk-heavy work.
No large-memory `-M` budget is supplied. ZIP64’s `extralargefile` lane remains
excluded: upstream documents a greater-than-30-minute run and roughly 6 GiB
archives, exceeding the bounded test-hold policy. `wantobjects` selects an
alternate Tcl representation rather than an absent dependency.

External network and URL fetching remain disabled to avoid remote service,
DNS, certificate and internet availability determining the correctness baseline.
Local loopback tests remain enabled where upstream does not guard the entire
module with its `network` resource. Arch supplies pre-generated locales through
`glibc-locales`; installing that package closes locale gaps without editing
system configuration. Unsupported CPU instructions,
other-platform tests, unavailable legacy Python versions and upstream-declared
skips remain visible rather than being counted as passes.

Each build/test/timing reservation stays below about 20 minutes. Reruns are
incremental, preserving unaffected module/shard outcomes and their original
commands. Collection is refreshed for both ABIs and the parallel NumPy lane;
new IDs extend the expected inventory and Lucifer remains entirely NOTRUN.

### Rebuilding and measuring the JIT oracle

`tools/oracle/build-cpython` now accepts `--rebuild` to rerun configure after
installing system libraries. It checks the existing pin/configuration, keeps
the previous installation until the staged replacement verifies, and restores
it if publication fails. A normal repeated invocation remains idempotent.
Both original oracles were rebuilt with their original flags and now include
Tk. The third build uses the same source commit and `CFLAGS=-O2` with
`--enable-experimental-jit=yes`; JIT and free threading are rejected together.

The pinned [Tools/jit/_llvm.py](https://github.com/python/cpython/blob/v3.14.10/Tools/jit/_llvm.py)
requires **LLVM major 19**. Packaged LLVM/Clang 19.1.7 were installed and their
tools selected on PATH for stencil generation; the interpreter C compiler
remains the original compiler. This is a separate build, without PGO, rather
than a change to the ordinary GIL baseline.

```sh
tools/oracle/build-cpython v3.14.10 "$ORACLE_GIL_PREFIX" --rebuild
tools/oracle/build-cpython v3.14.10 "$ORACLE_T_PREFIX" --disable-gil --rebuild
tools/oracle/build-cpython v3.14.10 "$ORACLE_JIT_PREFIX" --enable-experimental-jit
python3 tools/bench/awfy --python "$ORACLE_JIT" --runtime cpython-3.14.10-jit --jit 1
python3 tools/bench/pyperformance run --python "$VENV_JIT/bin/python" \
    --runtime cpython-3.14.10-jit --jit 1
```

Both benchmark drivers accept `--jit 0|1` (default 0). The setting reaches
the capability probe, correctness preflights, timed process and pyperf workers.
A requested JIT run requires both `sys._jit.is_available()` and
`sys._jit.is_enabled()`; an unavailable JIT leaves the whole panel NOTRUN.
JIT availability/enabled state and executable/configuration fingerprints are
recorded in the benchmark metadata. The normal missing/failing-runtime behavior
and full expected denominator are preserved.

### Every original case skip reason

The primary rows partition final SKIP cases. A case with skipped subtests can
emit several reasons; the supplementary table preserves every other distinct
reason for that case. Supplementary counts are reason incidences and overlap
the primary totals. Exact text and counts are retained in the audit JSON.

| Group | Primary reason | libtest GIL | libtest t | NumPy GIL | NumPy t |
|---|---|---:|---:|---:|---:|
| CPU features | Only one non-baseline feature detected. | 0 | 0 | 1 | 1 |
| CPU features | target 'X86_V4' isn't supported by current machine | 0 | 0 | 704 | 704 |
| Environment and OS features | AF_ALG required | 7 | 7 | 0 | 0 |
| Environment and OS features | Australia/Lord_Howe timezone is not supported on this platform | 4 | 4 | 0 | 0 |
| Environment and OS features | Bluetooth sockets required for this test. | 14 | 14 | 0 | 0 |
| Environment and OS features | CAN ISOTP required for this test. | 5 | 5 | 0 | 0 |
| Environment and OS features | CAN J1939 required for this test. | 3 | 3 | 0 | 0 |
| Environment and OS features | Can't set symlink permissions | 2 | 2 | 0 | 0 |
| Environment and OS features | Cannot block permissions | 1 | 1 | 0 | 0 |
| Environment and OS features | Hyper-V sockets required for this test. | 7 | 7 | 0 | 0 |
| Environment and OS features | No French locale available | 0 | 0 | 24 | 24 |
| Environment and OS features | QIPCRTR sockets required for this test. | 6 | 6 | 0 | 0 |
| Environment and OS features | RDS sockets required for this test. | 8 | 8 | 0 | 0 |
| Environment and OS features | SocketCan required for this test. | 12 | 12 | 0 | 0 |
| Environment and OS features | TIPC module is not loaded, please 'sudo modprobe tipc' | 2 | 2 | 0 | 0 |
| Environment and OS features | UDPLITE sockets required for this test. | 113 | 113 | 0 | 0 |
| Environment and OS features | VSOCK sockets required for this test. | 5 | 5 | 0 | 0 |
| Environment and OS features | [Errno 1] Operation not permitted | 1 | 1 | 0 | 0 |
| Environment and OS features | can't create socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP): [Errno 93] Protocol not supported | 42 | 42 | 0 | 0 |
| Environment and OS features | can't use pyrepl in this terminal | 21 | 21 | 0 | 0 |
| Environment and OS features | cannot set the en_US locale | 2 | 2 | 0 | 0 |
| Environment and OS features | coerced LC_CTYPE locale: C.UTF-8 | 1 | 1 | 0 | 0 |
| Environment and OS features | failed to query terminal size | 1 | 1 | 0 | 0 |
| Environment and OS features | local time needs to be different from UTC | 1 | 1 | 0 | 0 |
| Environment and OS features | locale-specific month name needs to be different from C locale | 1 | 1 | 0 | 0 |
| Environment and OS features | need non-UTC/GMT timezone | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'bg_BG' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'crh_UA' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'fr_FR' | 6 | 6 | 0 | 0 |
| Environment and OS features | no locale 'he_IL' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'ja_JP' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'ja_JP.UTF-8' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'lzh_TW' | 3 | 3 | 0 | 0 |
| Environment and OS features | no locale 'shn_MM' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locales ('tr_TR',) | 1 | 1 | 0 | 0 |
| Environment and OS features | no read-only type to test | 12 | 12 | 0 | 0 |
| Environment and OS features | no suitable locales | 6 | 6 | 0 | 0 |
| Environment and OS features | not enough memory: 1.0G minimum needed | 2 | 2 | 0 | 0 |
| Environment and OS features | not enough memory: 1.2G minimum needed | 1 | 1 | 0 | 0 |
| Environment and OS features | not enough memory: 1.4G minimum needed | 1 | 1 | 0 | 0 |
| Environment and OS features | not enough memory: 1.8G minimum needed | 2 | 2 | 0 | 0 |
| Environment and OS features | not enough memory: 10.0G minimum needed | 1 | 1 | 0 | 0 |
| Environment and OS features | not enough memory: 12.0G minimum needed | 1 | 1 | 0 | 0 |
| Environment and OS features | not enough memory: 16.0G minimum needed | 1 | 1 | 0 | 0 |
| Environment and OS features | not enough memory: 18.0G minimum needed | 2 | 2 | 0 | 0 |
| Environment and OS features | not enough memory: 2.0G minimum needed | 3 | 3 | 0 | 0 |
| Environment and OS features | not enough memory: 2.2G minimum needed | 2 | 2 | 0 | 0 |
| Environment and OS features | not enough memory: 2.7G minimum needed | 1 | 1 | 0 | 0 |
| Environment and OS features | not enough memory: 256.0G minimum needed | 2 | 0 | 0 | 0 |
| Environment and OS features | not enough memory: 288.0G minimum needed | 0 | 2 | 0 | 0 |
| Environment and OS features | not enough memory: 3.7G minimum needed | 3 | 3 | 0 | 0 |
| Environment and OS features | not enough memory: 32.0G minimum needed | 1 | 1 | 0 | 0 |
| Environment and OS features | not enough memory: 4.0G minimum needed | 6 | 6 | 0 | 0 |
| Environment and OS features | not enough memory: 4.7G minimum needed | 4 | 4 | 0 | 0 |
| Environment and OS features | not enough memory: 4.9G minimum needed | 6 | 6 | 0 | 0 |
| Environment and OS features | not enough memory: 5.0G minimum needed | 1 | 1 | 0 | 0 |
| Environment and OS features | not enough memory: 6.0G minimum needed | 5 | 5 | 0 | 0 |
| Environment and OS features | not enough memory: 6.2G minimum needed | 1 | 0 | 0 | 0 |
| Environment and OS features | not enough memory: 6.6G minimum needed | 1 | 0 | 0 | 0 |
| Environment and OS features | not enough memory: 7.2G minimum needed | 1 | 1 | 0 | 0 |
| Environment and OS features | not enough memory: 8.0G minimum needed | 6 | 6 | 0 | 0 |
| Environment and OS features | not enough memory: 8589934592.0G minimum needed | 13 | 13 | 0 | 0 |
| Environment and OS features | not enough privileges | 2 | 2 | 0 | 0 |
| Environment and OS features | not on tty | 1 | 1 | 0 | 0 |
| Environment and OS features | requires FS encoding to match locale | 1 | 1 | 0 | 0 |
| Environment and OS features | requires a case-insensitive filesystem | 8 | 8 | 0 | 0 |
| Environment and OS features | requires terminal | 3 | 3 | 0 | 0 |
| Environment and OS features | setgroup() EPERM; this test may require root. | 2 | 2 | 0 | 0 |
| Environment and OS features | stdin and stdout must be ttys | 6 | 6 | 0 | 0 |
| Environment and OS features | stdin is not a tty | 1 | 1 | 0 | 0 |
| Environment and OS features | stdin must be a TTY in this test | 2 | 2 | 0 | 0 |
| Environment and OS features | stdout tty check failed: fileno | 1 | 1 | 0 | 0 |
| Environment and OS features | stty invocation failed | 1 | 1 | 0 | 0 |
| Environment and OS features | sys.stdout is not a TTY | 1 | 1 | 0 | 0 |
| Environment and OS features | test needs Turkish locale | 1 | 1 | 0 | 0 |
| Environment and OS features | test needs de_DE.UTF8 locale | 1 | 1 | 0 | 0 |
| Environment and OS features | test needs en_US.iso88591 locale | 2 | 2 | 0 | 0 |
| Environment and OS features | test needs root privilege | 9 | 9 | 0 | 0 |
| Environment and OS features | test needs root privilege and more than one user | 1 | 1 | 0 | 0 |
| Environment and OS features | test should be run as root | 1 | 1 | 0 | 0 |
| Missing dependencies | CLI command needs a Fortran compiler | 0 | 0 | 5 | 5 |
| Missing dependencies | Could not find f2py documentation sources(&lt;path&gt; does not exist) | 0 | 0 | 3 | 3 |
| Missing dependencies | Hypothesis required for property test with no specified examples | 18 | 18 | 0 | 0 |
| Missing dependencies | Mypy is not installed | 0 | 0 | 178 | 178 |
| Missing dependencies | No Fortran 77 compiler available | 0 | 0 | 146 | 146 |
| Missing dependencies | No Fortran 90 compiler available | 0 | 0 | 121 | 121 |
| Missing dependencies | No Fortran compiler available | 0 | 0 | 609 | 609 |
| Missing dependencies | No Fortran compiler found | 0 | 0 | 1 | 1 |
| Missing dependencies | Skipping tzdata-specific tests: tzdata not installed | 30 | 30 | 0 | 0 |
| Missing dependencies | Test needs Numpy | 6 | 6 | 0 | 0 |
| Missing dependencies | Unable to import big_o | 1 | 1 | 0 | 0 |
| Missing dependencies | cffi not available | 0 | 0 | 5 | 5 |
| Missing dependencies | could not import 'PyInstaller.__main__': No module named 'PyInstaller' | 0 | 0 | 2 | 2 |
| Missing dependencies | csh required for this test | 1 | 1 | 0 | 0 |
| Missing dependencies | lib_gle not available | 1 | 1 | 0 | 0 |
| Missing dependencies | numpy_quaddtype not installed | 0 | 0 | 1 | 1 |
| Missing dependencies | pyfakefs not available | 1 | 1 | 0 | 0 |
| Missing dependencies | requires cffi | 0 | 0 | 1 | 1 |
| Missing dependencies | requires ifconfig | 2 | 2 | 0 | 0 |
| Missing dependencies | requires numba and cffi | 0 | 0 | 1 | 1 |
| Other platforms | Android only | 1 | 1 | 0 | 0 |
| Other platforms | Android-specific test | 1 | 1 | 0 | 0 |
| Other platforms | Bad file descriptor in &lt;path&gt; affects only macOS | 3 | 3 | 0 | 0 |
| Other platforms | Can only test junctions with creation on win32. | 3 | 3 | 0 | 0 |
| Other platforms | Can only test on win32. | 2 | 2 | 0 | 0 |
| Other platforms | Dev Drives only exist on Win32 | 1 | 1 | 0 | 0 |
| Other platforms | Emscripten-only test | 1 | 1 | 0 | 0 |
| Other platforms | FILE_ATTRIBUTE_* constants are Win32 specific | 2 | 2 | 0 | 0 |
| Other platforms | F_GETPATH is only available on macos | 1 | 1 | 0 | 0 |
| Other platforms | Fast paths are only for win32 | 1 | 1 | 0 | 0 |
| Other platforms | No Windows event queue on Unix | 13 | 13 | 0 | 0 |
| Other platforms | No module named '_winapi' | 1 | 1 | 0 | 0 |
| Other platforms | No module named '_wmi' | 1 | 1 | 0 | 0 |
| Other platforms | No module named 'winreg' | 2 | 2 | 0 | 0 |
| Other platforms | OS X-specific test | 1 | 1 | 0 | 0 |
| Other platforms | OS helpers require 'nt' module | 1 | 1 | 0 | 0 |
| Other platforms | OSX only test | 1 | 1 | 0 | 0 |
| Other platforms | OSX-specific test | 3 | 3 | 0 | 0 |
| Other platforms | Only for Linux and ARM | 0 | 0 | 1 | 1 |
| Other platforms | Only for Linux and IBM Z | 0 | 0 | 1 | 1 |
| Other platforms | Only for Linux and LoongArch | 0 | 0 | 1 | 1 |
| Other platforms | Only for Linux and Power | 0 | 0 | 1 | 1 |
| Other platforms | Only for Linux and RISC-V | 0 | 0 | 1 | 1 |
| Other platforms | Only on Windows. | 3 | 3 | 0 | 0 |
| Other platforms | PYTHONEXECUTABLE only works on macOS | 1 | 1 | 0 | 0 |
| Other platforms | Proactor is supported on Windows only | 4 | 4 | 0 | 0 |
| Other platforms | Requires \\ as path separator. | 1 | 1 | 0 | 0 |
| Other platforms | Test only applicable on Windows | 3 | 3 | 0 | 0 |
| Other platforms | Test only applicable to iOS | 4 | 4 | 0 | 0 |
| Other platforms | Test only valid for Android | 1 | 1 | 0 | 0 |
| Other platforms | Test only valid for Emscripten | 5 | 5 | 0 | 0 |
| Other platforms | Test specific to Windows | 1 | 1 | 0 | 0 |
| Other platforms | This test only makes sense on Windows | 1 | 1 | 0 | 0 |
| Other platforms | WASI only test | 1 | 1 | 0 | 0 |
| Other platforms | Win32 specific test | 3 | 3 | 0 | 0 |
| Other platforms | Win32 specific tests | 35 | 35 | 0 | 0 |
| Other platforms | Win32-specific test | 1 | 1 | 0 | 0 |
| Other platforms | Windows only | 21 | 22 | 0 | 0 |
| Other platforms | Windows only issue | 4 | 4 | 0 | 0 |
| Other platforms | Windows specific | 13 | 13 | 0 | 0 |
| Other platforms | Windows specific test | 3 | 3 | 0 | 0 |
| Other platforms | Windows specific tests | 20 | 20 | 0 | 0 |
| Other platforms | Windows-only process limit | 3 | 3 | 0 | 0 |
| Other platforms | Windows-specific | 2 | 2 | 0 | 0 |
| Other platforms | Windows-specific test | 16 | 16 | 0 | 0 |
| Other platforms | Windows-specific tests | 4 | 4 | 0 | 0 |
| Other platforms | abspath requires 'nt' module | 1 | 1 | 0 | 0 |
| Other platforms | bpo-32457 only applies on Windows | 1 | 1 | 0 | 0 |
| Other platforms | code pages are specific to Windows | 13 | 13 | 0 | 0 |
| Other platforms | ctypes.WINFUNCTYPE is required | 2 | 2 | 0 | 0 |
| Other platforms | ctypes.oledll is required | 1 | 1 | 0 | 0 |
| Other platforms | drive letters are a windows concept | 1 | 1 | 0 | 0 |
| Other platforms | macOS only | 28 | 28 | 0 | 0 |
| Other platforms | macOS specific test | 9 | 9 | 0 | 0 |
| Other platforms | macOS system check | 2 | 2 | 0 | 0 |
| Other platforms | need subprocess._winapi | 2 | 2 | 0 | 0 |
| Other platforms | needs sys._enablelegacywindowsfsencoding() | 1 | 1 | 0 | 0 |
| Other platforms | object &lt;module 'sys' (built-in)&gt; has no attribute 'getwindowsversion' | 2 | 2 | 0 | 0 |
| Other platforms | only applies to 32-bit platforms | 5 | 5 | 0 | 0 |
| Other platforms | only applies to ppc double-double values | 0 | 0 | 1 | 1 |
| Other platforms | only available on Emscripten | 1 | 1 | 0 | 0 |
| Other platforms | only need to run on musllinux_x86_64 | 0 | 0 | 1 | 1 |
| Other platforms | only relevant for OSX | 1 | 1 | 0 | 0 |
| Other platforms | only relevant for Windows | 1 | 1 | 0 | 0 |
| Other platforms | only relevant on Windows | 9 | 9 | 0 | 0 |
| Other platforms | only relevant on macOS | 1 | 1 | 0 | 0 |
| Other platforms | pathext check is Windows-only | 2 | 2 | 0 | 0 |
| Other platforms | plutil utility is for Mac os | 2 | 2 | 0 | 0 |
| Other platforms | requires 32-bit system | 1 | 1 | 0 | 0 |
| Other platforms | requires NTFS | 1 | 1 | 0 | 0 |
| Other platforms | requires OS X | 16 | 16 | 0 | 0 |
| Other platforms | requires Windows | 13 | 13 | 0 | 0 |
| Other platforms | requires Windows-flavoured path class | 129 | 129 | 0 | 0 |
| Other platforms | requires macOS | 1 | 1 | 0 | 0 |
| Other platforms | skipped on non-Windows platforms | 3 | 3 | 0 | 0 |
| Other platforms | specific to Windows | 5 | 5 | 0 | 0 |
| Other platforms | st_file_attributes is Win32 specific | 1 | 1 | 0 | 0 |
| Other platforms | symlinks on Windows | 1 | 1 | 0 | 0 |
| Other platforms | test can only run in a 32-bit address space | 2 | 2 | 0 | 0 |
| Other platforms | test is for win32 | 4 | 4 | 0 | 0 |
| Other platforms | test is only meaningful on 32-bit builds | 22 | 22 | 0 | 0 |
| Other platforms | test needs Windows | 1 | 1 | 0 | 0 |
| Other platforms | test only relevant on MacOSX | 1 | 1 | 0 | 0 |
| Other platforms | test only relevant on win32 | 1 | 1 | 0 | 0 |
| Other platforms | test requires a Windows-compatible system | 223 | 223 | 0 | 0 |
| Other platforms | test requires a tty and either Windows or nl_langinfo(CODESET) | 1 | 1 | 0 | 0 |
| Other platforms | test specific to Mac OS X and Android | 1 | 1 | 0 | 0 |
| Other platforms | test specific to Windows | 27 | 27 | 0 | 0 |
| Other platforms | test specific to Windows pathnames. | 2 | 2 | 0 | 0 |
| Other platforms | test specific to the Windows console | 1 | 1 | 0 | 0 |
| Other platforms | test weak linking on macOS | 18 | 18 | 0 | 0 |
| Other platforms | trackfd only fails on Windows | 1 | 1 | 0 | 0 |
| Other platforms | win32-specific test | 3 | 3 | 0 | 0 |
| Other platforms | win32evtlog/win32evtlogutil/pywintypes required for this test. | 1 | 1 | 0 | 0 |
| Other platforms | windows console only | 2 | 2 | 0 | 0 |
| Other platforms | windows only | 2 | 2 | 0 | 0 |
| Other platforms | windows only test | 2 | 2 | 0 | 0 |
| Resource policy | Use of the 'network' resource not enabled | 21 | 21 | 0 | 0 |
| Resource policy | Use of the 'urlfetch' resource not enabled | 51 | 51 | 0 | 0 |
| Resource policy | network is not enabled | 1 | 1 | 0 | 0 |
| Resource policy | resource 'cpu' is not enabled | 117 | 116 | 0 | 0 |
| Resource policy | resource 'network' is not enabled | 6 | 6 | 0 | 0 |
| Resource policy | resource 'walltime' is not enabled | 44 | 44 | 0 | 0 |
| Resource policy | resource-profile omission (conditional generation) | 5595 | 5595 | 0 | 0 |
| Upstream declarations and build guards | '&lt;path&gt;' doesn't exist | 75 | 74 | 0 | 0 |
| Upstream declarations and build guards | 'utf-8' decoder doesn't support surrogatepass error handler | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | 'utf-8' encoder doesn't support surrogatepass error handler | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | (empty upstream reason) | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | -X presite requires a Python debug build | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | -X showrefcount requires a Python debug build | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | Advance is not supported by MT19937 | 0 | 0 | 1 | 1 |
| Upstream declarations and build guards | Advance is not supported by SFC64 | 0 | 0 | 1 | 1 |
| Upstream declarations and build guards | BUG #2376 | 0 | 0 | 2 | 2 |
| Upstream declarations and build guards | Bad memory reports lead to OOM in ci testing | 0 | 0 | 2 | 2 |
| Upstream declarations and build guards | Bytes dtype does not support non-ascii input | 0 | 0 | 47 | 47 |
| Upstream declarations and build guards | BytesWarning is needed for this test: use -bb option | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | Cannot get total reference count. | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | Cannot safely use fork in tests on the free-threaded build | 0 | 0 | 0 | 1 |
| Upstream declarations and build guards | Complex arithmetic with signed zero fails on most platforms | 0 | 0 | 1 | 1 |
| Upstream declarations and build guards | Condition does not expose _recursion_count() | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | Consistently fails on CI; noisy so skip not xfail. | 0 | 0 | 2 | 2 |
| Upstream declarations and build guards | Desired but not supported. | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | Exhausted deque iterator doesn't free a deque | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | FIXME: bpo-32706 | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | Failed to import generate-build-details | 6 | 6 | 0 | 0 |
| Upstream declarations and build guards | Fails on FreeBSD | 4 | 1 | 0 | 0 |
| Upstream declarations and build guards | For generic signatures only | 0 | 0 | 60 | 60 |
| Upstream declarations and build guards | Free threaded builds don't have an 'active thread' | 0 | 1 | 0 | 0 |
| Upstream declarations and build guards | Functions are immortalized if a thread is launched, making this test flaky | 0 | 0 | 0 | 2 |
| Upstream declarations and build guards | Incompatible with the fork start method. | 8 | 7 | 0 | 0 |
| Upstream declarations and build guards | Jump is not supported by SFC64 | 0 | 0 | 1 | 1 |
| Upstream declarations and build guards | Linux behavior is tested by TestLinuxAbstractNamespace | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | Locals proxies for different frames never compare as equal | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | Maybe test trigger a leak on Ubuntu | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | NT specific tests | 3 | 3 | 0 | 0 |
| Upstream declarations and build guards | Needs -OO | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | No dynamic libpython | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | Not relevant in pure Python | 25 | 25 | 0 | 0 |
| Upstream declarations and build guards | Numeric scope id does not work or undocumented | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | Numpy xerbla not linked in. | 0 | 0 | 1 | 0 |
| Upstream declarations and build guards | Only run for Fast C implementation | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | Only run for Pure Python implementation | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | Out of range of int32 | 0 | 0 | 1 | 1 |
| Upstream declarations and build guards | PYTHON_TLBC and -X tlbc only supported in Py_GIL_DISABLED builds | 3 | 0 | 0 | 0 |
| Upstream declarations and build guards | Py_GIL_DISABLED builds do not support abi3 | 0 | 0 | 0 | 84 |
| Upstream declarations and build guards | Redeclared namespace handling is not supported in out_inNsSuperfluous_c14nDefault | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | Requires --with-trace-refs build option | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | Requires HAVE_PERF_TRAMPOLINE support | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | Requires free-threading | 2 | 0 | 0 | 0 |
| Upstream declarations and build guards | Requires warn_default_encoding | 13 | 13 | 0 | 0 |
| Upstream declarations and build guards | SOURCE_DATE_EPOCH is set | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | SSLv3 is not available. | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | See test_init_pybuilddir | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | Skip due to platform/environment differences on *NIX buildbots | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | Skipping test for start_method = 'fork' | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | TLSv1 is not available. | 3 | 3 | 0 | 0 |
| Upstream declarations and build guards | TLSv1_1 is not available. | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | Test disabled for now - see gh-60779/gh-60780 | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | Test library is already loaded | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | Test needs selectors.DevpollSelector | 20 | 20 | 0 | 0 |
| Upstream declarations and build guards | Test needs selectors.KqueueSelector) | 22 | 22 | 0 | 0 |
| Upstream declarations and build guards | Unable to open &lt;path&gt; | 6 | 6 | 0 | 0 |
| Upstream declarations and build guards | Unlike a mapping: del proxy[key] fails | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | Unlike a mapping: no proxy.clear | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | Unlike a mapping: no proxy.fromkeys | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | Unlike a mapping: no proxy.pop | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | Unlike a mapping: no proxy.popitem | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | Unlike a mapping: no proxy.update | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | Unwinding is unreliable with frame pointers | 3 | 3 | 0 | 0 |
| Upstream declarations and build guards | Weights not supported by method. | 0 | 0 | 72 | 72 |
| Upstream declarations and build guards | XMLParser.(Get\|Set)ReparseDeferralEnabled methods not available in C | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | XXX NotImplementedError: tolist() only supports byte views | 3 | 3 | 0 | 0 |
| Upstream declarations and build guards | XXX test should be adapted for non-byte buffers | 3 | 3 | 0 | 0 |
| Upstream declarations and build guards | __doc__ is already present, __slots__ will have no effect | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | `axis != None` not supported for 0d arrays | 0 | 0 | 86 | 86 |
| Upstream declarations and build guards | `huge_int -&gt; string -&gt; longdouble` failed | 0 | 0 | 1 | 1 |
| Upstream declarations and build guards | arp is not used for introspection on this platform | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | array repr problem | 0 | 0 | 1 | 1 |
| Upstream declarations and build guards | array**2 can have incorrect/weird result dtype | 0 | 0 | 1 | 1 |
| Upstream declarations and build guards | as of 3.11 this test no longer works because path calculations do not occur on read | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | blocking forever | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | broken (see BPO-...) | 2 | 0 | 0 | 0 |
| Upstream declarations and build guards | broken on some platforms | 0 | 0 | 7 | 7 |
| Upstream declarations and build guards | build directory could not be found | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | built without C assertions | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | can't fix because of backward compat in email5, will fix in email6 | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | causes undesirable side-effects (#20128) | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | cexp(nan + 0I) is wrong on most platforms | 0 | 0 | 1 | 1 |
| Upstream declarations and build guards | could not find libc | 1 | 1 | 0 | 0 |
| Missing dependencies | could not locate runnable browser | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | decimal_point is not "," | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | dir_fd is supported | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | disabled because of a bug in glibc. Issue #13309 | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | disabled until high-level channels exist | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | does not have an na object | 0 | 0 | 2 | 2 |
| Upstream declarations and build guards | does not intern all constants | 1 | 0 | 0 | 0 |
| Upstream declarations and build guards | enable this test when working on pystate.c | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | encoding contains full ISO 10646 map | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | failing buildbots | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | fork specific test | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | forkserver specific test | 5 | 5 | 0 | 0 |
| Upstream declarations and build guards | forkserver specific test. | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | forkserver start method specific | 4 | 4 | 0 | 0 |
| Upstream declarations and build guards | fortran function returning array segfaults | 0 | 0 | 1 | 1 |
| Upstream declarations and build guards | free-threaded build interns all string constants | 0 | 1 | 0 | 0 |
| Upstream declarations and build guards | freeze directory could not be found | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | generators are not reentrant | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | gh-117344: test is flaky without the GIL | 0 | 5 | 0 | 0 |
| Upstream declarations and build guards | gh-117649: not supported in free-threaded build | 0 | 1 | 0 | 0 |
| Upstream declarations and build guards | gh-117694: free-threaded build does not currently support single-phase init modules in sub-interpreters | 0 | 12 | 0 | 0 |
| Upstream declarations and build guards | gh-118433: Flaky due to a longstanding bug | 0 | 1 | 0 | 0 |
| Upstream declarations and build guards | gh-121065: test is flaky on free-threaded build | 0 | 1 | 0 | 0 |
| Upstream declarations and build guards | half assignment currently uses Python float converter | 0 | 0 | 1 | 1 |
| Upstream declarations and build guards | i18n directory could not be found | 4 | 4 | 0 | 0 |
| Upstream declarations and build guards | if no_value is allowed, ParsingError is not raised | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | inappropriate decimal point separator ('.' not '\\u066b') | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | incompatible format | 4 | 4 | 0 | 0 |
| Upstream declarations and build guards | inner classes are not members | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | iso2022_kr.txt cannot be used to test "chunk coding" | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | lanscan is not used for introspection on this platform | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | library ._pth file not supported on this platform | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | libssl doesn't support ephemeral DH | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | libuuid cannot deduce MAC address | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | lltrace requires Py_DEBUG | 3 | 3 | 0 | 0 |
| Upstream declarations and build guards | mixed types with auto() no longer supported | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | mouse stuff not available | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | need Pickler class | 20 | 20 | 0 | 0 |
| Upstream declarations and build guards | need Py_DEBUG | 9 | 9 | 0 | 0 |
| Upstream declarations and build guards | need Py_GIL_DISABLED | 1 | 0 | 0 | 0 |
| Upstream declarations and build guards | need Py_STATS build | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | need _getfinalpathname | 13 | 13 | 0 | 0 |
| Upstream declarations and build guards | need _testcapi.decref_freed_object() | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | need _testcapi.negative_refcount() | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | need a debug build | 5 | 5 | 0 | 0 |
| Upstream declarations and build guards | need a suitable object | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | need frozendict | 12 | 12 | 0 | 0 |
| Upstream declarations and build guards | need generational GC | 0 | 2 | 0 | 0 |
| Upstream declarations and build guards | need msvcrt.CrtSetReportMode | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | need pymalloc | 0 | 9 | 0 | 0 |
| Upstream declarations and build guards | need sys.getobjects() | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | need uuid._generate_time_safe | 4 | 4 | 0 | 0 |
| Upstream declarations and build guards | needs a built-in function with a really long name | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | needs local target | 4 | 4 | 0 | 0 |
| Upstream declarations and build guards | needs precise allocation counts | 0 | 1 | 0 | 0 |
| Upstream declarations and build guards | needs the GIL enabled | 0 | 10 | 0 | 0 |
| Upstream declarations and build guards | needs too much memory on a 64-bit platform | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | netstat is not used for introspection on this platform | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | no na support | 0 | 0 | 2 | 2 |
| Upstream declarations and build guards | no non-broken extended attribute support | 9 | 9 | 0 | 0 |
| Upstream declarations and build guards | non-ASCII bytes | 3 | 3 | 0 | 0 |
| Upstream declarations and build guards | not applicable | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | not appropriate for subclasses | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | not collected by this ABI build (union inventory) | 202 | 174 | 0 | 0 |
| Upstream declarations and build guards | object &lt;module 'sys' (built-in)&gt; has no attribute 'gettotalrefcount' | 8 | 8 | 0 | 0 |
| Upstream declarations and build guards | only for the Python version | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | only in free-threaded builds | 6 | 0 | 0 | 0 |
| Upstream declarations and build guards | only makes sense with processes | 9 | 9 | 0 | 0 |
| Upstream declarations and build guards | only meaningful if the GIL is disabled | 2 | 0 | 0 | 0 |
| Upstream declarations and build guards | only run in test_multiprocessing_forkserver. | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | only run in test_multiprocessing_spawn. | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | only verified in the C implementation | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | only verified in the Python implementation | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | opaque PyObject requires Python 3.15+ | 0 | 0 | 1 | 1 |
| Upstream declarations and build guards | os.link() is present | 3 | 3 | 0 | 0 |
| Upstream declarations and build guards | os.random() does not use a file descriptor | 3 | 3 | 0 | 0 |
| Upstream declarations and build guards | os.readlink() is present | 3 | 3 | 0 | 0 |
| Upstream declarations and build guards | os.symlink() is present | 3 | 3 | 0 | 0 |
| Upstream declarations and build guards | path parser is supported | 3 | 3 | 0 | 0 |
| Upstream declarations and build guards | printing subinterpreter unraisable exceptions requires DEBUG build | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | requires CAP_DAC_OVERRIDE | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | requires GC generations or increments | 0 | 2 | 0 | 0 |
| Upstream declarations and build guards | requires JIT enabled | 89 | 0 | 0 | 0 |
| Upstream declarations and build guards | requires POSIX-flavoured path class | 8 | 8 | 0 | 0 |
| Upstream declarations and build guards | requires _testinternalcapi.uop_symbols_test | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | requires a debug build for testing assertions in multiprocessing | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | requires a shared libpython build | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | requires colors support | 8 | 8 | 0 | 0 |
| Upstream declarations and build guards | requires context aware warnings | 4 | 0 | 0 | 0 |
| Upstream declarations and build guards | requires debug build | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | requires free-threaded GC | 1 | 0 | 0 | 0 |
| Upstream declarations and build guards | requires headers and trailers support | 5 | 5 | 0 | 0 |
| Upstream declarations and build guards | requires os.chflags | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | requires os.chflags, EOPNOTSUPP & ENOTSUP | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | requires os.lchmod | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | requires os.link to be missing | 4 | 4 | 0 | 0 |
| Upstream declarations and build guards | requires os.path.altsep | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | requires specialization | 0 | 118 | 0 | 0 |
| Upstream declarations and build guards | requires sys.gettotalrefcount() | 4 | 4 | 0 | 0 |
| Upstream declarations and build guards | requires the native C os.spawnv | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | requires thread-safe warnings flags | 2 | 0 | 0 | 0 |
| Upstream declarations and build guards | requires unix | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | requires uuid_generate_time_safe(3) | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | requires verbose mode | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | scripts directory could not be found | 3 | 3 | 0 | 0 |
| Upstream declarations and build guards | segfaults on BOLT instrumented binaries | 4 | 4 | 0 | 0 |
| Upstream declarations and build guards | setregid() available on platform | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | setreuid() available on platform | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | some buildbots are not cooperating (#20128) | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | start_method='fork', not 'forkserver'; avoids redundant testing. | 5 | 5 | 0 | 0 |
| Upstream declarations and build guards | start_method='fork', not 'forkserver'; forkserver specific test. | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | start_method='fork', not 'spawn'; avoids redundant testing. | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | start_method='fork', not 'spawn'; freeze_support is not start method specific | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | start_method='fork', not 'spawn'; spawn specific test. | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | start_method='forkserver', not 'spawn'; avoids redundant testing. | 3 | 3 | 0 | 0 |
| Upstream declarations and build guards | start_method='forkserver', not 'spawn'; freeze_support is not start method specific | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | start_method='forkserver', not 'spawn'; spawn specific test. | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | start_method='spawn', not 'forkserver'; avoids redundant testing. | 5 | 5 | 0 | 0 |
| Upstream declarations and build guards | start_method='spawn', not 'forkserver'; forkserver specific test. | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | temporarily skipped due to refleaks | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | test generates files &gt;4294967295 bytes and takes a long time to run | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | test is only useful if the GIL is disabled | 1 | 0 | 0 | 0 |
| Upstream declarations and build guards | test meaningful only where os.altsep is defined | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | test needs os.RWF_SYNC | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | test needs os.chflags() | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | test needs os.lchflags() | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | test needs os.lchmod() | 4 | 4 | 0 | 0 |
| Upstream declarations and build guards | test needs posix.O_EXLOCK | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | test needs posix.O_SHLOCK | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | test not applicable to manager | 6 | 6 | 0 | 0 |
| Upstream declarations and build guards | test not appropriate for fork | 3 | 3 | 0 | 0 |
| Upstream declarations and build guards | test not appropriate for manager | 33 | 33 | 0 | 0 |
| Upstream declarations and build guards | test not appropriate for spawn | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | test not appropriate for threads | 81 | 81 | 0 | 0 |
| Upstream declarations and build guards | test not implemented | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | test only relevant for 'forkserver' method | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | test requires SIGINT to not be ignored | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | test requires build with -DEXTRA_FUNCTIONALITY | 6 | 6 | 0 | 0 |
| Upstream declarations and build guards | test.bat script is not installed | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | text mode not available | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | these tests can only possibly fail with GIL disabled | 1 | 0 | 0 | 0 |
| Upstream declarations and build guards | these tests take several hours to run | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | this test can only possibly fail with GIL disabled | 3 | 0 | 0 | 0 |
| Upstream declarations and build guards | this tests the error messages when trying toprotocol 5 although it is not available | 0 | 0 | 1 | 1 |
| Upstream declarations and build guards | to-do list | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | too slow, see gh-23975 | 0 | 0 | 1 | 1 |
| Upstream declarations and build guards | unicode directory could not be found | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | unnecessary on this platform | 1 | 1 | 0 | 0 |

### Additional original subtest skip reasons

| Group | Additional reason | libtest GIL | libtest t | NumPy GIL | NumPy t |
|---|---|---:|---:|---:|---:|
| Environment and OS features | no locale 'aa_ET' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'af_ZA' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'am_ET' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'ar_AE' | 4 | 4 | 0 | 0 |
| Environment and OS features | no locale 'ar_AE.UTF-8' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'ar_SA' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'az_AZ' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'az_IR' | 3 | 3 | 0 | 0 |
| Environment and OS features | no locale 'be_BY' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'ber_DZ' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'ber_MA' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'bg_BG' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'bn_IN.UTF-8' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'br_FR' | 4 | 4 | 0 | 0 |
| Environment and OS features | no locale 'brx_IN' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'bs_BA' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'byn_ER' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'ca_ES' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'cs_CZ' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'csb_PL' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'da_DK' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'de_AT' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'de_BE' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'de_DE' | 15 | 15 | 0 | 0 |
| Environment and OS features | no locale 'de_DE.ISO8859-1' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'de_LU' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'el_GR' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'el_GR.ISO8859-7' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'en_DK' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'en_US' | 10 | 10 | 0 | 0 |
| Environment and OS features | no locale 'es_AR' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'es_BO' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'es_CL' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'es_CO' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'es_EC' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'es_ES' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'es_PY' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'es_UY' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'es_VE' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'et_EE' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'eu_ES' | 5 | 5 | 0 | 0 |
| Environment and OS features | no locale 'fa_IR' | 3 | 3 | 0 | 0 |
| Environment and OS features | no locale 'fi_FI' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'fo_FO' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'fr_BE' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'fr_CA' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'fr_CH' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'fr_FR' | 9 | 9 | 0 | 0 |
| Environment and OS features | no locale 'fr_FR.ISO8859-1' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'fr_FR.ISO8859-15@euro' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'fr_FR.UTF-8' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'fr_LU' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'gez_ET' | 3 | 3 | 0 | 0 |
| Environment and OS features | no locale 'gl_ES' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'he_IL' | 3 | 3 | 0 | 0 |
| Environment and OS features | no locale 'hr_HR' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'hu_HU' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'id_ID' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'is_IS' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'it_CH' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'it_IT' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'ja_JP' | 7 | 7 | 0 | 0 |
| Environment and OS features | no locale 'ja_JP.SJIS' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'ja_JP.UTF-8' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'ja_JP.eucJP' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'ka_GE' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'kl_GL' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'ko_KR.eucKR' | 3 | 3 | 0 | 0 |
| Environment and OS features | no locale 'lt_LT' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'lv_LV' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'lzh_TW' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'lzh_TW.UTF-8' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'mfe_MU' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'mk_MK' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'mr_IN.UTF-8' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'my_MM' | 5 | 5 | 0 | 0 |
| Environment and OS features | no locale 'my_MM.UTF-8' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'nb_NO' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'nl_BE' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'nl_NL' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'nn_NO' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'no_NO' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'oc_FR' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'om_ET' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'or_IN' | 3 | 3 | 0 | 0 |
| Environment and OS features | no locale 'or_IN.UTF-8' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'pl_PL' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'ps_AF' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'pt_BR' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'pt_PT' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'ro_RO' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'ru_RU' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'ru_RU.KOI8-R' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'ru_UA' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'se_NO' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'shn_MM' | 4 | 4 | 0 | 0 |
| Environment and OS features | no locale 'shn_MM.UTF-8' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'sid_ET' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'sk_SK' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'sl_SI' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'so_SO' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'sq_AL' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'sr_YU' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'sv_FI' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'sv_SE' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'tg_TJ' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'th_TH.TIS620' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'ti_ET' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'tig_ER' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'uk_UA' | 3 | 3 | 0 | 0 |
| Environment and OS features | no locale 'uk_UA.KOI8-U' | 1 | 1 | 0 | 0 |
| Environment and OS features | no locale 'vi_VN' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'wa_BE' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'wal_ET' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'yo_NG' | 2 | 2 | 0 | 0 |
| Environment and OS features | no locale 'zh_CN.GB18030' | 1 | 1 | 0 | 0 |
| Upstream declarations and build guards | QName rewriting in XPath text is not supported in out_inNsContent_c14nPrefixQnameXpathElem | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | QName rewriting in XPath text is not supported in out_inNsContent_c14nQnameXpathElem | 2 | 2 | 0 | 0 |
| Upstream declarations and build guards | Redeclared namespace handling is not supported in out_inNsRedecl_c14nDefault | 2 | 2 | 0 | 0 |

### Original whole-module skip reasons

These are module dispositions, including modules with zero collected cases.
They overlap case skips when a module has expected IDs; never add them to the
case totals above. Each distinct reason is listed with affected module names.

| Group | Reason | GIL modules | t modules |
|---|---|---|---|
| Missing dependencies | No module named '_tkinter' | `test_idle`, `test_tcl`, `test_tkinter`, `test_ttk`, `test_ttk_textonly`, `test_turtle` | `test_idle`, `test_tcl`, `test_tkinter`, `test_ttk`, `test_ttk_textonly`, `test_turtle` |
| Other platforms | Android-specific | `test_android` | `test_android` |
| Other platforms | No module named '_winapi' | `test_winapi` | `test_winapi` |
| Other platforms | No module named '_wmi' | `test_wmi` | `test_wmi` |
| Other platforms | No module named 'winreg' | `test_winreg` | `test_winreg` |
| Other platforms | Windows only | `test.test_asyncio.test_windows_events`, `test.test_asyncio.test_windows_utils` | `test.test_asyncio.test_windows_events`, `test.test_asyncio.test_windows_utils` |
| Other platforms | test only applies to Windows | `test_launcher` | `test_launcher` |
| Other platforms | test only relevant on win32 | `test_winconsoleio` | `test_winconsoleio` |
| Other platforms | windows related tests | `test_msvcrt` | `test_msvcrt` |
| Resource policy | Use of the 'cpu' resource not enabled | `test_peg_generator` | `test_peg_generator` |
| Resource policy | Use of the 'network' resource not enabled | `test_smtpnet`, `test_socketserver`, `test_urllib2net`, `test_urllibnet` | `test_smtpnet`, `test_socketserver`, `test_urllib2net`, `test_urllibnet` |
| Resource policy | Use of the 'xpickle' resource not enabled | `test_xpickle` | `test_xpickle` |
| Upstream declarations and build guards | Apple-specific | `test_apple` | `test_apple` |
| Upstream declarations and build guards | CPython must be configured with the --with-dtrace option. | `test_dtrace` | `test_dtrace` |
| Upstream declarations and build guards | GIL disabled | — | `test_interpreters` |
| Upstream declarations and build guards | GIL enabled | `test.test_free_threading.test_bisect`, `test.test_free_threading.test_bytes_object`, `test.test_free_threading.test_capi`, `test.test_free_threading.test_code`, `test.test_free_threading.test_collections`, `test.test_free_threading.test_context`, `test.test_free_threading.test_cprofile`, `test.test_free_threading.test_csv`, `test.test_free_threading.test_datetime`, `test.test_free_threading.test_dbm_gnu`, `test.test_free_threading.test_dict`, `test.test_free_threading.test_dict_watcher`, `test.test_free_threading.test_enumerate`, `test.test_free_threading.test_exceptions`, `test.test_free_threading.test_frame`, `test.test_free_threading.test_func_annotations`, `test.test_free_threading.test_functions`, `test.test_free_threading.test_functools`, `test.test_free_threading.test_gc`, `test.test_free_threading.test_grp`, `test.test_free_threading.test_heapq`, `test.test_free_threading.test_io`, `test.test_free_threading.test_iteration`, `test.test_free_threading.test_itertools`, `test.test_free_threading.test_itertools_batched`, `test.test_free_threading.test_itertools_combinatoric`, `test.test_free_threading.test_list`, `test.test_free_threading.test_methodcaller`, `test.test_free_threading.test_mmap`, `test.test_free_threading.test_monitoring`, `test.test_free_threading.test_os`, `test.test_free_threading.test_pickle`, `test.test_free_threading.test_races`, `test.test_free_threading.test_re`, `test.test_free_threading.test_resource`, `test.test_free_threading.test_reversed`, `test.test_free_threading.test_set`, `test.test_free_threading.test_slots`, `test.test_free_threading.test_str`, `test.test_free_threading.test_string_template_race`, `test.test_free_threading.test_sys`, `test.test_free_threading.test_threading`, `test.test_free_threading.test_tokenize`, `test.test_free_threading.test_type`, `test.test_free_threading.test_types`, `test.test_free_threading.test_uuid`, `test.test_free_threading.test_weakref`, `test.test_free_threading.test_zip` | — |
| Upstream declarations and build guards | No module named 'winsound' | `test_winsound` | `test_winsound` |
| Upstream declarations and build guards | cases_generator directory could not be found | `test_generated_cases` | `test_generated_cases` |
| Upstream declarations and build guards | clinic directory could not be found | `test_clinic` | `test_clinic` |
| Upstream declarations and build guards | object &lt;module 'os' (frozen)&gt; has no attribute 'startfile' | `test_startfile` | `test_startfile` |
| Upstream declarations and build guards | test irrelevant for an installed Python | `test_asdl_parser` | `test_asdl_parser` |
| Upstream declarations and build guards | test requires loads of disk-space bytes and a long time to run | `test_zipfile64` | `test_zipfile64` |
| Upstream declarations and build guards | test works only on BSD | `test_kqueue` | `test_kqueue` |
| Upstream declarations and build guards | test works only on Solaris OS family | `test_devpoll` | `test_devpoll` |
| Upstream declarations and build guards | test_gdb only works on source builds at the moment. | `test.test_gdb.test_backtrace`, `test.test_gdb.test_cfunction`, `test.test_gdb.test_cfunction_full`, `test.test_gdb.test_misc`, `test.test_gdb.test_pretty_print` | `test.test_gdb.test_backtrace`, `test.test_gdb.test_cfunction`, `test.test_gdb.test_cfunction_full`, `test.test_gdb.test_misc`, `test.test_gdb.test_pretty_print` |

### Applicability decisions

The installed-oracle lane retains upstream guards for source-tree fixtures
(`Programs/_testembed`, `Tools` and freeze inputs), debug/reference-tracing
builds, unavailable JIT support in the two non-JIT builds, alternate operating
systems and kernel features. Their reason text is preserved above. Running
source-layout or debug assertions would require a separately labelled build or
execution layout; copying fixtures or changing `sys.executable` would change
those assertions' context. The legacy-pickle resource now actually checks for
Python 2.7 and 3.2–3.13; unavailable executables remain individual SKIP rows.
The OpenGL GLE library is not available in the configured package repositories.
No unapproved third-party source build was substituted for that system library.

With its dependency installed, the quaddtype test explicitly skips version 1.0.0
because upstream declares a critical import bug. PyPI supplies no newer stable
release in this audit. The omitted f2py documentation inputs are supplied from
the separately hash-pinned NumPy 2.5.4 source distribution; the original three
documentation skips can therefore be exercised with the Fortran compiler.

Installing PyInstaller exposed two real failures per NumPy lane:
`PythonLibraryNotFoundError`, because the oracle configurations do not build a
shared `libpython`. Those tests are now attempted and retained as FAIL. Enabling
shared-library linkage would change the baseline build configuration and needs
new build/performance provenance; it was not silently added to the two existing
reference configurations. The earlier free-threaded build-details failure and
upstream XFAIL/XPASS dispositions remain visible unless a rerun changes them.

### Coverage after the reruns

The complete [before scoreboard](../results/deps/scoreboard-before.tsv) and
[after scoreboard](../results/deps/scoreboard-after.tsv) retain every status
column. The table below includes every suite/runtime cell; “—” means that the
JIT runtime did not yet exist in the original snapshot.

| Suite / runtime | Expected before → after | PASS before → after | SKIP before → after | FAIL / ERROR after | Pass rate before → after |
|---|---:|---:|---:|---:|---:|
| bench/awfy / cpython-3.14.10 | 14 → 14 | 14 → 14 | 0 → 0 | 0 / 0 | 100.00% → 100.00% |
| bench/awfy / cpython-3.14.10-jit | — → 14 | — → 14 | — → 0 | 0 / 0 | — → 100.00% |
| bench/awfy / cpython-3.14.10t | 14 → 14 | 14 → 14 | 0 → 0 | 0 / 0 | 100.00% → 100.00% |
| bench/awfy / lucifer | 14 → 14 | 0 → 0 | 0 → 0 | 0 / 0 | 0.00% → 0.00% |
| bench/pyperformance / cpython-3.14.10 | 9 → 9 | 9 → 9 | 0 → 0 | 0 / 0 | 100.00% → 100.00% |
| bench/pyperformance / cpython-3.14.10-jit | — → 9 | — → 9 | — → 0 | 0 / 0 | — → 100.00% |
| bench/pyperformance / cpython-3.14.10t | 9 → 9 | 9 → 9 | 0 → 0 | 0 / 0 | 100.00% → 100.00% |
| bench/pyperformance / lucifer | 9 → 9 | 0 → 0 | 0 → 0 | 0 / 0 | 0.00% → 0.00% |
| libtest / cpython-3.14.10 | 53973 → 56200 | 45915 → 50832 | 8048 → 5351 | 7 / 0 | 85.07% → 90.45% |
| libtest / cpython-3.14.10t | 53973 → 56200 | 45900 → 50814 | 8059 → 5365 | 8 / 0 | 85.04% → 90.42% |
| libtest / lucifer | 53973 → 56200 | 0 → 0 | 0 → 0 | 0 / 0 | 0.00% → 0.00% |
| numpytest / cpython-3.14.10 | 52273 → 52273 | 50135 → 51226 | 2106 → 1010 | 5 / 0 | 95.91% → 98.00% |
| numpytest / cpython-3.14.10t | 52273 → 52273 | 50049 → 51140 | 2192 → 1096 | 5 / 0 | 95.75% → 97.83% |
| numpytest / lucifer | 52273 → 52273 | 0 → 0 | 0 → 0 | 0 / 0 | 0.00% → 0.00% |
| numpytest-parallel / cpython-3.14.10t | 52273 → 52273 | 50049 → 51140 | 2192 → 1096 | 5 / 0 | 95.75% → 97.83% |
| numpytest-parallel / lucifer | 52273 → 52273 | 0 → 0 | 0 → 0 | 0 / 0 | 0.00% → 0.00% |
| oracle / cpython-3.14.10 | 11 → 11 | 10 → 11 | 1 → 0 | 0 / 0 | 90.91% → 100.00% |
| oracle / cpython-3.14.10-jit | — → 11 | — → 11 | — → 0 | 0 / 0 | — → 100.00% |
| oracle / cpython-3.14.10t | 11 → 11 | 10 → 11 | 1 → 0 | 0 / 0 | 90.91% → 100.00% |
| oracle / lucifer | 11 → 11 | 0 → 0 | 0 → 0 | 0 / 0 | 0.00% → 0.00% |

No oracle cell contains NOTRUN, TIMEOUT or CRASH. Every Lucifer cell remains
NOTRUN with zero seconds: **0/160,780** suite/runtime rows in total. The libtest
inventory grew from 53,973 to **56,200**: 2,223 Tk/IDLE/turtle cases and five
real ZIP-path complexity cases were added; their former import-skip placeholder
was removed. [The inventory change](../results/deps/inventory-change.json) records
that replacement explicitly. NumPy remains **52,273** IDs in each lane.

All 24 French-locale NumPy cases, all three f2py documentation cases and the
compiler-dependent shards now run. NumPy has no remaining missing-dependency
skips. Its remaining skips are 705 CPU-dispatch/feature cases, seven other-platform
cases, and 298 upstream applicability/bug guards in the GIL build; the
free-threaded lanes add 84 ABI3 guards and two upstream immortalization guards.
The parallel free-threaded outcome map exactly matches its serial counterpart.

The remaining libtest dependency skips are 3,016 cases requiring unavailable
Python 2.7/3.2–3.13 executables and one GLE-library case per ABI. Two browser-
discovery cases per ABI retain their observed headless SKIP. Their Xvfb recheck
after installing `xdg-utils` is queued behind an active timing reservation. The resource-aware
conditional omissions fell from **5,595 to 67**: 63 network-module cases and four
oversized ZIP64 cases. Additional decorated network/URL skips remain explicit.

| Remaining skip group | libtest GIL | libtest t | NumPy GIL | NumPy t |
|---|---:|---:|---:|---:|
| CPU features | 0 | 0 | 705 | 705 |
| Environment and OS features | 385 | 383 | 0 | 0 |
| Missing dependencies | 3019 | 3019 | 0 | 0 |
| Other platforms | 764 | 765 | 7 | 7 |
| Resource policy | 143 | 143 | 0 | 0 |
| Upstream declarations and build guards | 1040 | 1055 | 298 | 384 |

Every current reason, including secondary subtest reasons and whole-module
dispositions, is retained in [skip-audit-after.json](../results/deps/skip-audit-after.json).
The original audit contains **549 distinct reasons** across its primary and
supplementary case tables. These counts never relabel a skip or expected failure
as a pass.

### Findings exposed by the additional coverage

The remaining libtest failures are seven in the GIL build and eight in the
free-threaded build. Six per build compare the frozen **2020a**
[zoneinfo fixture](../third_party/cpython/Lib/test/test_zoneinfo/data/zoneinfo_data.json)
against the installed **tzdata 2026.2** package; historical offsets and DST names
have changed. The seventh is the optional big-O
[`test_glob_depth`](../third_party/cpython/Lib/test/test_zipfile/_path/test_complexity.py)
complexity assertion. Free-threaded CPython additionally retains its original
`test_build_details` base-interpreter filename failure. No upstream assertions
or frozen data were edited to obtain a different outcome.

Each NumPy lane has **five FAIL**, **30 XFAIL** and **two XPASS**: the two
PyInstaller shared-library failures described above and three mypy 2.4.1
reveal mismatches (`array_constructors`, `ndarray_conversion`, `random`). All
**178** typing checks execute: **175 PASS**, three FAIL. These are tool/type
expectation failures rather than evidence of an interpreter crash.

Mypy’s initial source-shadowing setup error comes from the installed-wheel
layout: its source search reaches sibling dependency modules in site-packages. The observer copies the **145**
typing data files unchanged to a private directory before calling upstream’s
mypy API, then translates only that directory prefix in returned diagnostics
back to the original fixture paths. The API arguments, exit status and diagnostic
contents are otherwise retained; assertions and collected IDs are unchanged.
Both ABIs record the same input hash
`c6f4c42f69d7e3b7d09c5b9522555fc35f0166af46e73e107a41a36539eb44ac`.
A harness check injects an error and verifies that the relocation preserves it.

The published parallel plugin initially swallowed PyInstaller worker exceptions
as `PytestUnhandledThreadExceptionWarning`, incorrectly producing PASS rows.
The driver now promotes that warning to an error in both lanes, converting
these worker exceptions into failures of their parent tests. The two packaging tests consequently retain FAIL in the
parallel lane as well. No installed plugin or NumPy payload file was modified.

Three additional free-threaded observations passed on their subsequent runs:
queue visibility after a fixed sleep, process interruption/join, and the SSL
test’s shared unittest-subtest state. The two multiprocessing modules received
one diagnostic rerun with one controller. The SSL module was rerun with the
locale-affected modules. Their earlier dispositions remain in
[the finding/recheck record](../results/deps/finding-rechecks.json); the precise
cause of these schedule-sensitive observations remains **unverified**.

### JIT rows and verification

| Runtime | AWFY PASS / expected | pyperformance PASS / expected | AWFY time / GIL CPython | pyperformance time / GIL CPython |
|---|---:|---:|---:|---:|
| cpython-3.14.10 | 14/14 | 9/9 | 1.000000 | 1.000000 |
| cpython-3.14.10t | 14/14 | 9/9 | 1.189462 | 1.108104 |
| cpython-3.14.10-jit | 14/14 | 9/9 | 0.966330 | 0.978257 |
| lucifer | 0/14 | 0/9 | missing | missing |

Lower time ratios are faster. The JIT oracle retained **280 verified AWFY
iterations** across 14 fresh processes, all with a steady suffix; pyperformance
retained **81 values** across 27 measurement workers. Six JIT pyperformance rows
carry short-run instability warnings. These are foundation measurements, not a
noise-calibrated speedup claim. Per-row timings, samples and factors are in
[AWFY results](../results/bench/awfy/) and
[pyperformance results](../results/bench/pyperformance/). Availability/enabled
probes confirm JIT support and `PYTHON_JIT=1`; the two ordinary oracle builds
report JIT unavailable.

All **50 harness checks passed** through `make check-harness`, including
rebuild failure preservation, JIT environment propagation/rejection, real-venv
command preservation, full NOTRUN recovery, secondary skip reasons, pinned
documentation inputs/tampering, worker-warning policy and typing-error
preservation. Every changed manifest was checked for sorted unique IDs, LF
endings, complete expected coverage, valid statuses and finite nonnegative
durations. Both collections had zero errors. NumPy’s 925 wheel payload files
and 22 native binaries matched before/after the reruns; free-threaded imports
retained a disabled GIL without forcing it off.

[Installed Arch versions](../results/deps/installed-packages.tsv),
[observed Python package versions](../results/deps/python-packages.json) and
[f2py source-file provenance](../results/deps/f2py-sources.json) are retained
beside the scoreboards. Raw logs, compiled fixtures and interpreter installations
remain outside the public checkout.
