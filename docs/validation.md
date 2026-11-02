# Validation harnesses

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
