# Validation

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
