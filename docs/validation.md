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
