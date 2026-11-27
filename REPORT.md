# Scorecard timing statistics

Branch `luc-scorecard-stats` starts at `83ea62d9`. Commit `a3fad06c` changes measurement and reporting. Commits `9701cde7` and `ece577de` distribute the longer Lucifer replays across shorter acquisitions.

## Measurement

- Hyperfine 2.0.0 starts a fresh process for one warmup and five measured runs. Each program's scratch JSON supplies wall-clock mean, sample standard deviation, median and minimum. Hyperfine calibrates the launch shell, including the candidate's resource wrapper. The fresh workload is one verified AWFY iteration, one named pyperf task, the original short command, or one Flask server serving a fixed HTTP transcript.
- All four AWFY runtimes execute ten verified iterations with the original inner work. The score uses the median of the last seven. Its 95% percentile interval resamples those seven iterations with replacement 9,999 times, with a fixed seed.
- Pyperf 2.10.0 discards three warmup values and supplies the mean and sample standard deviation of eight values at the fixed reference loop count. Flask uses three fresh servers per layout, each with ten seconds of warmup and ten seconds of measured traffic.
- Mean intervals use the two-sided 95% Student t critical value times the standard error. Ratio bounds divide the numerator's lower bound by the denominator's upper bound and vice versa. The card gives each program one vote in a geometric mean, propagating the endpoints through the same calculation. A program with several pyperf tasks first sums their times. Day-over-day movement requires identical program coverage and disjoint intervals.
- Card 5 counts Lucifer build and kill events and compile milliseconds during the measured window of a diagnostic replay. PyPy reports changes in its loop, bridge and compilation counters. Churn fails the card. Unavailable telemetry is reported separately.

## Installation

Download the x86-64 Linux musl release archive, verify its SHA-256 digest, extract it and install the executable in the user-local bin directory on PATH. The command `hyperfine --version` returned `hyperfine 2.0.0`. The corpus lock supplies pyperf. [Hyperfine v2.0.0 release](https://github.com/sharkdp/hyperfine/releases/tag/v2.0.0)

Run these commands from a scratch directory with the user-local bin directory on PATH.

```sh
asset=hyperfine-v2.0.0-x86_64-unknown-linux-musl
curl -fLO "https://github.com/sharkdp/hyperfine/releases/download/v2.0.0/$asset.tar.gz"
echo "38e59087bd65520930a13cf513d8a493b7043741e8ad62349a8a9929eb61bc5c  $asset.tar.gz" | sha256sum -c -
tar -xzf "$asset.tar.gz"
install -Dm755 "$asset/hyperfine" "$(python3 -m site --user-base)/bin/hyperfine"
hyperfine --version
```

## Verification

The pre-change regression run failed on the pyperf estimator, missing post-warmup build counts, and headline format. The full `make check-harness` passed before the scheduling and Flask follow-ups. All 18 focused scorecard tests passed on the final code with pyperf installed. An intermediate check inherited the daily’s weekend gate settings and failed two gate fixtures. Running the fixtures with their own settings cleared both failures. A real CPython 3.14t Mandelbrot measurement exercised both timing paths. Hyperfine also accepted a command whose verified exit code was 1.

The four-runtime dry run used the daily build and scoring path. A dry-only copy, `lucifer-daily.new`, substituted the focused gates. Three kernel programs matched CPython 3.14t in each of plain, JIT and JIT-deopt. The trial table identifies the other correctness gates as unrun.

The trial began on `a3fad06c`. Two Lucifer corpus workers released their slots at completed manifest boundaries after 19m08s and 20m44s. The original controller recorded both interrupts and exited 1. Later slots ran the remaining programs with the same estimators and sample counts. Before rendering the tables below, the receipt inventory verified all 95 corpus manifests per Python runtime and all 14 AWFY programs per runtime, without duplicates. The recovered summary rendered successfully.

CPython initially failed the fresh-process Flask transcript because the controller inherited the candidate’s interpreter settings. Commit `6ed2f0f7` isolates the controller. Both available layouts then finished all five hyperfine runs within the same web acquisition.

Commit `5b164faf` keeps a completed Flask run in the throughput comparison when individual requests fail. The stock single-CPU run served 2,221 requests/s with one request error. The error remains visible alongside the ratio.

The published README and dated scorecard preserve the original correctness rows and their measurement dates from `83ea62d9`. The trial README below uses its own focused gate results.

Every AWFY program and all 36 held-out programs have a Lucifer/stock score. Dask has no runnable reference in the pinned corpus. PyPy failed four additional manifest checks. The test allocation could run the eight-CPU shared-client Flask layout, while the eight-CPU disjoint layout was unavailable. A lower timing bound of zero gives an unbounded upper ratio, displayed as `inf`.

## Daily time

The four-runtime dry run took **2h43m39s** (9,819 seconds) from checkout through README generation, including the build and focused gates. The eleven measurement arms took 2h42m49s including waits. The longest arm took 26m10s. The task used 20 host jobs counting individual gate units.

The earlier Friday daily spent 38m01s in its full correctness gates. Replacing this trial’s three seconds of focused gates with that observed duration gives **3h21m37s** for the Lucifer daily. Adding the preceding 1h25m30s of the same morning’s chain gives an estimated **4h47m07s** against the six-hour ceiling. Only the focused four-runtime dry-run time was measured on this branch.

## Trial headline

| Card | Lucifer | Failed programs | Target | Change since yesterday |
|---|---|---|---|---|
| 1. Pure-Python compute / Held-out real Python programs | Go 10.7× (10–11.3×), 14/14<br>PyPy 3.3× (3.06–3.44×), 14/14<br>stock 1.03× (0.937–1.13×), 36/36 | Go 0 failed<br>PyPy 0 failed<br>stock 0 failed | AWFY ≤2× Go and ≤1× PyPy time on all 14<br>Warm-suite bar ≤0.35× stock and ≤0.72× PyPy | Go —<br>PyPy —<br>stock — |
| 2. Application paths vs stock | 1 CPU 0.266× (0.246–0.285×), 1/1<br>8 CPUs —, 0/1<br>applications (time) 2.12× (1.94–2.32×), 9/9 | 1 CPU 0 failed<br>8 CPUs 1 failed<br>applications (time) 0 failed | Flask ≥1.5× stock at both counts, ≥4× scaling, zero errors, p99 ≤stock | 1 CPU —<br>8 CPUs —<br>applications (time) — |
| 3. Short programs vs stock | CLI 4.38× (4.1–4.67×), 6/6<br>startup 2.26× (2.01–2.54×), 3/3<br>fresh process 5.5× (4.23–7.22×), 1/1 | CLI 0 failed<br>startup 0 failed<br>fresh process 0 failed | Each CLI and startup program ≤1× stock time | CLI —<br>startup —<br>fresh process — |
| 4. Native-heavy programs vs stock | majority 2.25× (2.07–2.44×), 20/20<br>mixed 2× (1.82–2.19×), 28/28<br>boundary 3.49× (3.19–3.82×), 2/2<br>unclassified —, 0/1 | majority 0 failed<br>mixed 0 failed<br>boundary 0 failed<br>unclassified 1 failed | ≥10% gain on a second wheel, no bulk regression, warm-suite bar ≤0.35× stock | majority —<br>mixed —<br>boundary —<br>unclassified — |
| 5. Post-warm-up JIT churn | Lucifer FAIL 92/111 with churn<br>PyPy FAIL 76/107 with churn | Lucifer 2 unmeasured<br>PyPy 6 unmeasured | Zero builds, kills and compile time after warm-up | — |

### All-program geometric mean by card

Each ratio is Lucifer time divided by reference time. Flask contributes the inverse of each disjoint layout’s throughput ratio. Go covers the verified AWFY ports.

| Card | Time ratios | Failed programs | Change since yesterday |
|---|---|---|---|
| 1 | stock 0.694× (0.639–0.754×), 50/50<br>PyPy 3.87× (3.42–4.39×), 49/50<br>Go 10.7× (10–11.3×), 14/14 | stock 0 failed<br>PyPy 1 failed<br>Go 0 failed | stock —<br>PyPy —<br>Go — |
| 2 | stock 2.25× (2.05–2.45×), 10/11<br>PyPy 4.11× (3.44–4.98×), 9/11 | stock 1 failed<br>PyPy 2 failed | stock —<br>PyPy — |
| 3 | stock 3.67× (3.32–4.06×), 10/10<br>PyPy 1.56× (1.42–1.7×), 10/10 | stock 0 failed<br>PyPy 0 failed | stock —<br>PyPy — |
| 4 | stock 2.14× (1.96–2.34×), 50/51<br>PyPy 2.39× (2.05–inf×), 48/51 | stock 1 failed<br>PyPy 3 failed | stock —<br>PyPy — |

## Trial README table

| Metric | Score | Counts | Date |
|---|---|---|---|
| Pure-Python compute vs Go / PyPy | Go 10.7× (10–11.3×), 14/14<br>PyPy 3.3× (3.06–3.44×), 14/14 | Go 0 failed<br>PyPy 0 failed | 2026-11-27 |
| Held-out real Python programs vs CPython 3.14t | stock 1.03× (0.937–1.13×), 36/36 | stock 0 failed | 2026-11-27 |
| Flask throughput and application time vs CPython 3.14t | 1 CPU 0.266× (0.246–0.285×), 1/1<br>8 CPUs —, 0/1<br>applications (time) 2.12× (1.94–2.32×), 9/9 | 1 CPU 0 failed<br>8 CPUs 1 failed<br>applications (time) 0 failed | 2026-11-27 |
| Short programs (CLI, startup) vs CPython 3.14t | CLI 4.38× (4.1–4.67×), 6/6<br>startup 2.26× (2.01–2.54×), 3/3<br>fresh process 5.5× (4.23–7.22×), 1/1 | CLI 0 failed<br>startup 0 failed<br>fresh process 0 failed | 2026-11-27 |
| Native-heavy programs vs CPython 3.14t | majority 2.25× (2.07–2.44×), 20/20<br>mixed 2× (1.82–2.19×), 28/28<br>boundary 3.49× (3.19–3.82×), 2/2<br>unclassified —, 0/1 | majority 0 failed<br>mixed 0 failed<br>boundary 0 failed<br>unclassified 1 failed | 2026-11-27 |
| JIT churn after warm-up | Lucifer FAIL 92/111 with churn<br>PyPy FAIL 76/107 with churn | Lucifer 2 unmeasured<br>PyPy 6 unmeasured | 2026-11-27 |
| All compute programs (time) | stock 0.694× (0.639–0.754×), 50/50<br>PyPy 3.87× (3.42–4.39×), 49/50<br>Go 10.7× (10–11.3×), 14/14 | stock 0 failed<br>PyPy 1 failed<br>Go 0 failed | 2026-11-27 |
| All application paths (time per operation or request) | stock 2.25× (2.05–2.45×), 10/11<br>PyPy 4.11× (3.44–4.98×), 9/11 | stock 1 failed<br>PyPy 2 failed | 2026-11-27 |
| All short programs (time) | stock 3.67× (3.32–4.06×), 10/10<br>PyPy 1.56× (1.42–1.7×), 10/10 | stock 0 failed<br>PyPy 0 failed | 2026-11-27 |
| All native and mixed programs (time) | stock 2.14× (1.96–2.34×), 50/51<br>PyPy 2.39× (2.05–inf×), 48/51 | stock 1 failed<br>PyPy 3 failed | 2026-11-27 |
| C-API functions implemented | — | outside the focused gate | 2026-11-27 |
| numpy test suite passing | — | outside the focused gate | 2026-11-27 |
| `Lib/test` passing, relative to CPython | — | outside the focused gate | 2026-11-27 |
| Kernel programs passing (worst measured mode) | 100.0% | 3 of 3 focused programs, 3 modes | 2026-11-27 |
| Differential programs matching CPython (worst measured JIT mode) | — | outside the focused gate | 2026-11-27 |
