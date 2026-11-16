# Agent rules

- `vendor/` (ignored) holds shallow clones of reference sources: CPython, PyPy, numpy, pyperformance, are-we-fast-yet, free-threaded-compatibility. Read and count them; never build, install or execute anything from them.
- Cite sources with URLs or file paths; mark anything not verified as "unverified".
- Commit nothing specific to a person, host or local environment: no home-directory paths, user names, hostnames, private repository names or local tool names. The repository is public.

## Workflow

- Open work is tracked with `tk` (tickets in `.tickets/`). Run `tk help` before using it, create tickets with `-a ""` (no assignee: the repo is public), and use `tk ready` to list unblocked work.
- Each task runs in its own git worktree and branch, named after its ticket. The coordinator reviews and merges to master.
- Tests are binary-level wherever possible. They run the real interpreter on Python source and compare against the CPython oracle, never against internal counters or struct offsets. Write the test first and show it failing.
- `make check-harness` must pass before merge. Scores live in `results/` and are summarized with `tools/scoreboard`.
- Runtime changes pass the quick tier before merge (Gates, below).
- Commits: terse conventional messages (lowercase imperative subject; the body says why). No attribution trailers.
- Maximal simplicity: the simplest mechanism that earns the result. Consolidate what you touch and delete what becomes dead.
- Held-out benchmarks are not in this repository. Builders never look for them or ask about them.

## Gates

- **Per change: `tools/floor quick`.** One command, run from the worktree root with the floor's environment (`tools/floor --help`). It builds, then runs from one queue: the kernel programs in the plain, `jit` and `jit-deopt` lanes, difftest and jitcheck compiled at first call and with failing guards, and the units `results/floor/quick.tsv` lists (Lib/test modules, ft-cpython modules, fuzzdiff generators, a numpy file, a PyPI package). It compares them with `results/floor/off` and ends with `quick: PASS` or `quick: FAIL`, one `LOST` line per floor pass lost, and the wall time from the start of the build; the target is 15 minutes on 8 jobs.
  - Which modes the slice runs in follows the diff against `origin/master` (`--base REF` for another): tests and docs alone, no slice; `runtime/jit/` alone, the slice compiled at first call and with failing guards; anything else (the interpreter, the kernel, `lib/`, the C-API, tools, results) or no change at all, those two and the tier off as well, so an interpreter regression shows as one. Only what git tracks counts: commits since the base and modified tracked files; untracked files (logs, scratch directories) never do.
  - A `LOST` line is a test the change broke: fix the change. A kernel, difftest or jitcheck row must pass whatever the floor says. A kernel check that fails runs once more alone and fails the gate only if it fails again; one that then passes prints as `FLAKY`, which says its program depends on timing.
  - kernelcheck runs the oracle twice per program text, flags, environment and input and caches the result in `build/tests/kernel/oracle` only when both runs agree; a program whose CPython output varies fails as `oracle varies`, which is a bug in the program.
- **Daily, on master: the full floor.** `tools/floor run --mode jit --out DIR`, the same for `deopt`, and `tools/floor compare results/floor/off DIR`, about five hours of 8 jobs. A floor pass lost there is bisected between the two dailies' commits with `tools/floor run --mode MODE --unit CORPUS/LABEL --out DIR` on the unit that lost it.
- **Re-recording the off baseline.** `tools/floor run --mode off --out results/floor/off` (`--corpus NAME` for one corpus, `--runs 2` to leave flaky tests out) after a change that moves what the interpreter passes on purpose: a fix that gains tests, a corpus or inventory change. Record it on master, after the daily shows the new passes, in the commit that explains them; never to absorb a regression.
- **Re-selecting the slice.** `tools/floor select --budget 450 --past DIR... --now DIR...` rewrites `results/floor/quick.tsv` from floor runs of the tier: `--past` runs (old dailies, branches) score units that lost passes there, `--now` runs (the latest daily, a quick run on master) leave out units that fail now, and `build/floor/unit-seconds.json` gives each unit's cost. Re-select after a daily changes either, when a unit flakes, or when the quick tier drifts past 15 minutes.
