# Agent rules

- `vendor/` (ignored) holds shallow clones of reference sources: CPython, PyPy, numpy, pyperformance, are-we-fast-yet, free-threaded-compatibility. Read and count them; never build, install or execute anything from them.
- Cite sources with URLs or file paths; mark anything not verified as "unverified".
- Commit nothing specific to a person, host or local environment: no home-directory paths, user names, hostnames, private repository names or local tool names. The repository is public.

## Workflow

- Open work is tracked with `tk` (tickets in `.tickets/`). Run `tk help` before using it. `tk ready` lists unblocked work.
- Each task runs in its own git worktree and branch, named after its ticket. The coordinator reviews and merges to master.
- Tests are binary-level wherever possible. They run the real interpreter on Python source and compare against the CPython oracle, never against internal counters or struct offsets. Write the test first and show it failing.
- `make check-harness` must pass before merge. Scores live in `results/` and are summarized with `tools/scoreboard`.
- Commits: terse conventional messages (lowercase imperative subject; the body says why). No attribution trailers.
- Maximal simplicity: the simplest mechanism that earns the result. Consolidate what you touch and delete what becomes dead.
- Held-out benchmarks are not in this repository. Builders never look for them or ask about them.
