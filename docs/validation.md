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
