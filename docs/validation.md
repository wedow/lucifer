# Validation harnesses

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
