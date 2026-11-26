# Runtime scorecards

Source revision `1a5576678282aee16ff2a3ad916af3d20fa6af83`.

## Scorecards

Stock is CPython 3.14t. Ratios compare Lucifer time except for Flask, which compares requests/s on disjoint server and client CPUs. Each paired manifest has one vote. Change compares the same programs with yesterday's ratios; a dash means no comparable measurement.

| Card | Lucifer | Paired / observed | Target | Change since yesterday |
|---|---|---|---|---|
| 1. Pure-Python compute / Held-out real Python programs | Go 1.95×, PyPy 1.01×<br>stock 0.821× | Go 2/14, PyPy 1/14<br>stock 4/37 | AWFY ≤2× Go and ≤1× PyPy time on all 14<br>Warm-suite bar ≤0.35× stock and ≤0.72× PyPy | —<br>— |
| 2. Flask throughput vs stock | 1 CPU 0.267×, 8 CPUs — | 1 CPU 1/1, 8 CPUs 0/1 | ≥1.5× stock at both counts, ≥4× scaling, zero errors, p99 ≤stock | — |
| 3. Short programs vs stock | CLI 3.89×, startup 2.16× | CLI 6/6, startup 3/3 | Each CLI and startup program ≤1× stock time | — |
| 4. Native-heavy programs vs stock | majority 2.26×, mixed 1.95× | majority 20/20, mixed 27/27 | ≥10% gain on a second wheel, no bulk regression; warm-suite bar ≤0.35× stock | — |
| 5. Time to steady state | 24/77 settled | 52 unsettled, 1 unavailable | Track onset and compile cost; PLAN2 sets no numeric settling target | — |

## 1. Pure Python

Seconds per verified AWFY iteration or pyperf operation in the steady suffix. Go ratios require a matching port. Keep AWFY and the prospective held-outs separate.

| program | runtime | status | group | seconds | over_cpython | over_pypy | over_go | final_s |
|---|---|---|---|---|---|---|---|---|
| awfy.Bounce | cpython | ok | — | 2.0968 | 1 | — | 59.622 | 2.0706 |
| awfy.Bounce | go | ok | — | 0.035168 | 0.016772 | — | 1 | 0.035218 |
| awfy.Bounce | lucifer | unsettled | — | — | — | — | — | 0.16357 |
| awfy.Bounce | pypy | unsettled | — | — | — | — | — | 0.041836 |
| awfy.CD | cpython | ok | — | 3.2686 | 1 | — | — | 3.2604 |
| awfy.CD | go | unsettled | — | — | — | — | — | 0.10583 |
| awfy.CD | lucifer | ok | — | 1.1189 | 0.34232 | — | — | 1.1308 |
| awfy.CD | pypy | unsettled | — | — | — | — | — | 0.28477 |
| awfy.DeltaBlue | cpython | ok | — | 0.82764 | 1 | — | — | 0.82764 |
| awfy.DeltaBlue | go | unsettled | — | — | — | — | — | 0.039895 |
| awfy.DeltaBlue | lucifer | unsettled | — | — | — | — | — | 0.60768 |
| awfy.DeltaBlue | pypy | unsettled | — | — | — | — | — | 0.085577 |
| awfy.Havlak | cpython | ok | — | 4.6876 | 1 | — | — | 4.709 |
| awfy.Havlak | go | unsettled | — | — | — | — | — | 0.22822 |
| awfy.Havlak | lucifer | ok | — | 2.8396 | 0.60578 | — | — | 2.8396 |
| awfy.Havlak | pypy | unsettled | — | — | — | — | — | 0.58145 |
| awfy.Json | cpython | ok | — | 1.101 | 1 | — | — | 1.1049 |
| awfy.Json | go | unsettled | — | — | — | — | — | 0.050357 |
| awfy.Json | lucifer | unsettled | — | — | — | — | — | 0.3979 |
| awfy.Json | pypy | unsettled | — | — | — | — | — | 0.12388 |
| awfy.List | cpython | ok | — | 1.2794 | 1 | 3.8805 | 51.68 | 1.2765 |
| awfy.List | go | ok | — | 0.024756 | 0.01935 | 0.075087 | 1 | 0.024651 |
| awfy.List | lucifer | unsettled | — | — | — | — | — | 0.27785 |
| awfy.List | pypy | ok | — | 0.3297 | 0.2577 | 1 | 13.318 | 0.32833 |
| awfy.Mandelbrot | cpython | ok | — | 1.6247 | 1 | 29.7 | 34.55 | 1.609 |
| awfy.Mandelbrot | go | ok | — | 0.047025 | 0.028944 | 0.85964 | 1 | 0.047511 |
| awfy.Mandelbrot | lucifer | ok | — | 0.055039 | 0.033876 | 1.0061 | 1.1704 | 0.055036 |
| awfy.Mandelbrot | pypy | ok | — | 0.054704 | 0.03367 | 1 | 1.1633 | 0.054704 |
| awfy.NBody | cpython | ok | — | 2.3449 | 1 | 29.979 | — | 2.3411 |
| awfy.NBody | go | unsettled | — | — | — | — | — | 0.023718 |
| awfy.NBody | lucifer | unsettled | — | — | — | — | — | 0.3508 |
| awfy.NBody | pypy | ok | — | 0.07822 | 0.033357 | 1 | — | 0.07822 |
| awfy.Permute | cpython | ok | — | 2.4762 | 1 | — | — | 2.475 |
| awfy.Permute | go | unsettled | — | — | — | — | — | 0.024771 |
| awfy.Permute | lucifer | unsettled | — | — | — | — | — | 1.0251 |
| awfy.Permute | pypy | unsettled | — | — | — | — | — | 0.19257 |
| awfy.Queens | cpython | unsettled | — | — | — | — | — | 1.1983 |
| awfy.Queens | go | unsettled | — | — | — | — | — | 0.017352 |
| awfy.Queens | lucifer | unsettled | — | — | — | — | — | 0.33 |
| awfy.Queens | pypy | unsettled | — | — | — | — | — | 0.10054 |
| awfy.Richards | cpython | ok | — | 5.3203 | 1 | 27.169 | — | 5.3385 |
| awfy.Richards | go | unsettled | — | — | — | — | — | 0.089363 |
| awfy.Richards | lucifer | unsettled | — | — | — | — | — | 1.0843 |
| awfy.Richards | pypy | ok | — | 0.19582 | 0.036806 | 1 | — | 0.19582 |
| awfy.Sieve | cpython | ok | — | 2.5876 | 1 | — | — | 2.5812 |
| awfy.Sieve | go | unsettled | — | — | — | — | — | 0.036174 |
| awfy.Sieve | lucifer | unsettled | — | — | — | — | — | 0.31399 |
| awfy.Sieve | pypy | unsettled | — | — | — | — | — | 0.13722 |
| awfy.Storage | cpython | unsettled | — | — | — | — | — | 2.1186 |
| awfy.Storage | go | ok | — | 0.48157 | — | — | 1 | 0.4809 |
| awfy.Storage | lucifer | ok | — | 1.5694 | — | — | 3.2588 | 1.5705 |
| awfy.Storage | pypy | unsettled | — | — | — | — | — | 0.31136 |
| awfy.Towers | cpython | ok | — | 1.6517 | 1 | 6.0626 | — | 1.6487 |
| awfy.Towers | go | unsettled | — | — | — | — | — | 0.018341 |
| awfy.Towers | lucifer | unsettled | — | — | — | — | — | 0.41647 |
| awfy.Towers | pypy | ok | — | 0.27245 | 0.16495 | 1 | — | 0.27525 |
| pyperformance.argparse | cpython | unsettled | held out | — | — | — | — | 0.0011544 |
| pyperformance.argparse | lucifer | unsettled | held out | — | — | — | — | 0.0031069 |
| pyperformance.argparse | pypy | unsettled | held out | — | — | — | — | 0.0015665 |
| pyperformance.argparse_subparsers | cpython | unsettled | held out | — | — | — | — | 0.016275 |
| pyperformance.argparse_subparsers | lucifer | unsettled | held out | — | — | — | — | 0.023998 |
| pyperformance.argparse_subparsers | pypy | unsettled | held out | — | — | — | — | 0.026181 |
| pyperformance.async_generators | cpython | unsettled | held out | — | — | — | — | 0.4509 |
| pyperformance.async_generators | lucifer | ok | held out | 1.4138 | — | — | — | 1.4208 |
| pyperformance.async_generators | pypy | unsettled | held out | — | — | — | — | 0.2798 |
| pyperformance.base64/ascii85_large | cpython | ok | held out | 1.51 | 1 | — | — | 1.5022 |
| pyperformance.base64/ascii85_large | lucifer | ok | held out | 1.2095 | 0.80098 | — | — | 1.2084 |
| pyperformance.base64/ascii85_large | pypy | unsettled | held out | — | — | — | — | 0.28137 |
| pyperformance.base64/ascii85_small | cpython | unsettled | held out | — | — | — | — | 0.027298 |
| pyperformance.base64/ascii85_small | lucifer | unsettled | held out | — | — | — | — | 0.04103 |
| pyperformance.base64/ascii85_small | pypy | unsettled | held out | — | — | — | — | 0.0037697 |
| pyperformance.base64/base16_large | cpython | unsettled | held out | — | — | — | — | 0.0071541 |
| pyperformance.base64/base16_large | lucifer | ok | held out | 0.03364 | — | — | — | 0.033366 |
| pyperformance.base64/base16_large | pypy | unsettled | held out | — | — | — | — | 0.033213 |
| pyperformance.base64/base16_small | cpython | unsettled | held out | — | — | — | — | 0.00053031 |
| pyperformance.base64/base16_small | lucifer | unsettled | held out | — | — | — | — | 0.0076681 |
| pyperformance.base64/base16_small | pypy | unsettled | held out | — | — | — | — | 0.00065427 |
| pyperformance.base64/base32_large | cpython | unsettled | held out | — | — | — | — | 0.70022 |
| pyperformance.base64/base32_large | lucifer | unsettled | held out | — | — | — | — | 0.76116 |
| pyperformance.base64/base32_large | pypy | unsettled | held out | — | — | — | — | 0.10412 |
| pyperformance.base64/base32_small | cpython | unsettled | held out | — | — | — | — | 0.013263 |
| pyperformance.base64/base32_small | lucifer | unsettled | held out | — | — | — | — | 0.014 |
| pyperformance.base64/base32_small | pypy | unsettled | held out | — | — | — | — | 0.0022085 |
| pyperformance.base64/base64_large | cpython | ok | held out | 0.0067646 | 1 | — | — | 0.0067646 |
| pyperformance.base64/base64_large | lucifer | unsettled | held out | — | — | — | — | 0.032051 |
| pyperformance.base64/base64_large | pypy | unsettled | held out | — | — | — | — | 0.013289 |
| pyperformance.base64/base64_small | cpython | unsettled | held out | — | — | — | — | 0.000352 |
| pyperformance.base64/base64_small | lucifer | unsettled | held out | — | — | — | — | 0.0015896 |
| pyperformance.base64/base64_small | pypy | unsettled | held out | — | — | — | — | 0.00036715 |
| pyperformance.base64/base85_large | cpython | unsettled | held out | — | — | — | — | 0.56716 |
| pyperformance.base64/base85_large | lucifer | unsettled | held out | — | — | — | — | 0.4955 |
| pyperformance.base64/base85_large | pypy | unsettled | held out | — | — | — | — | 0.11592 |
| pyperformance.base64/base85_small | cpython | unsettled | held out | — | — | — | — | 0.010082 |
| pyperformance.base64/base85_small | lucifer | unsettled | held out | — | — | — | — | 0.016805 |
| pyperformance.base64/base85_small | pypy | unsettled | held out | — | — | — | — | 0.0018496 |
| pyperformance.base64/urlsafe_base64_small | cpython | unsettled | held out | — | — | — | — | 0.00067632 |
| pyperformance.base64/urlsafe_base64_small | lucifer | unsettled | held out | — | — | — | — | 0.0038045 |
| pyperformance.base64/urlsafe_base64_small | pypy | unsettled | held out | — | — | — | — | 0.0004523 |
| pyperformance.bpe_tokeniser | cpython | ok | held out | 6.5214 | 1 | 3.4686 | — | 6.5295 |
| pyperformance.bpe_tokeniser | lucifer | unsettled | held out | — | — | — | — | 4.3846 |
| pyperformance.bpe_tokeniser | pypy | ok | held out | 1.8801 | 0.2883 | 1 | — | 1.9126 |
| pyperformance.chaos | cpython | unsettled | held out | — | — | — | — | 0.083634 |
| pyperformance.chaos | lucifer | unsettled | held out | — | — | — | — | 0.035733 |
| pyperformance.chaos | pypy | unsettled | held out | — | — | — | — | 0.005501 |
| pyperformance.comprehensions | cpython | unsettled | held out | — | — | — | — | 2.7047e-05 |
| pyperformance.comprehensions | lucifer | ok | held out | 2.9822e-05 | — | — | — | 2.9903e-05 |
| pyperformance.comprehensions | pypy | unsettled | held out | — | — | — | — | 8.9542e-06 |
| pyperformance.coroutines | cpython | unsettled | held out | — | — | — | — | 0.033065 |
| pyperformance.coroutines | lucifer | unsettled | held out | — | — | — | — | 0.045242 |
| pyperformance.coroutines | pypy | unsettled | held out | — | — | — | — | 0.017437 |
| pyperformance.coverage | cpython | unsettled | held out | — | — | — | — | 0.88554 |
| pyperformance.coverage | lucifer | ok | held out | 2.6835 | — | — | — | 2.6792 |
| pyperformance.coverage | pypy | unsettled | held out | — | — | — | — | 0.18662 |
| pyperformance.crypto_pyaes | cpython | ok | held out | 0.12092 | 1 | — | — | 0.12092 |
| pyperformance.crypto_pyaes | lucifer | unsettled | held out | — | — | — | — | 0.080673 |
| pyperformance.crypto_pyaes | pypy | unsettled | held out | — | — | — | — | 0.0046298 |
| pyperformance.deepcopy/deepcopy | cpython | unsettled | held out | — | — | — | — | 0.00042977 |
| pyperformance.deepcopy/deepcopy | lucifer | unsettled | held out | — | — | — | — | 0.00055243 |
| pyperformance.deepcopy/deepcopy | pypy | ok | held out | 9.9303e-05 | — | 1 | — | 9.9303e-05 |
| pyperformance.deepcopy/deepcopy_memo | cpython | unsettled | held out | — | — | — | — | 4.0931e-05 |
| pyperformance.deepcopy/deepcopy_memo | lucifer | unsettled | held out | — | — | — | — | 3.7702e-05 |
| pyperformance.deepcopy/deepcopy_memo | pypy | unsettled | held out | — | — | — | — | 1.6165e-05 |
| pyperformance.deepcopy/deepcopy_reduce | cpython | ok | held out | 3.9126e-06 | 1 | — | — | 3.9126e-06 |
| pyperformance.deepcopy/deepcopy_reduce | lucifer | unsettled | held out | — | — | — | — | 7.0473e-06 |
| pyperformance.deepcopy/deepcopy_reduce | pypy | unsettled | held out | — | — | — | — | 4.382e-05 |
| pyperformance.deltablue | cpython | unsettled | held out | — | — | — | — | 0.0047738 |
| pyperformance.deltablue | lucifer | unsettled | held out | — | — | — | — | 0.0044939 |
| pyperformance.deltablue | pypy | unsettled | held out | — | — | — | — | 0.0008248 |
| pyperformance.fannkuch | cpython | unsettled | held out | — | — | — | — | 0.58672 |
| pyperformance.fannkuch | lucifer | ok | held out | 0.54166 | — | — | — | 0.54176 |
| pyperformance.fannkuch | pypy | unsettled | held out | — | — | — | — | 0.079305 |
| pyperformance.generators | cpython | ok | held out | 0.036009 | 1 | — | — | 0.036009 |
| pyperformance.generators | lucifer | unsettled | held out | — | — | — | — | 0.058657 |
| pyperformance.generators | pypy | unsettled | held out | — | — | — | — | 0.022148 |
| pyperformance.go | cpython | unsettled | held out | — | — | — | — | 0.15446 |
| pyperformance.go | lucifer | unsettled | held out | — | — | — | — | 0.11912 |
| pyperformance.go | pypy | unsettled | held out | — | — | — | — | 0.14094 |
| pyperformance.hexiom | cpython | unsettled | held out | — | — | — | — | 0.0086474 |
| pyperformance.hexiom | lucifer | unsettled | held out | — | — | — | — | 0.0062109 |
| pyperformance.hexiom | pypy | unsettled | held out | — | — | — | — | 0.0025989 |
| pyperformance.mdp | cpython | ok | held out | 2.0397 | 1 | 0.37131 | — | 2.0404 |
| pyperformance.mdp | lucifer | ok | held out | 7.1897 | 3.5249 | 1.3088 | — | 7.1897 |
| pyperformance.mdp | pypy | ok | held out | 5.4932 | 2.6931 | 1 | — | 5.4932 |
| pyperformance.meteor_contest | cpython | unsettled | held out | — | — | — | — | 0.14555 |
| pyperformance.meteor_contest | lucifer | unsettled | held out | — | — | — | — | 0.45361 |
| pyperformance.meteor_contest | pypy | ok | held out | 0.053798 | — | 1 | — | 0.053798 |
| pyperformance.nbody | cpython | unsettled | held out | — | — | — | — | 0.1819 |
| pyperformance.nbody | lucifer | ok | held out | 0.070556 | — | — | — | 0.070148 |
| pyperformance.nbody | pypy | unsettled | held out | — | — | — | — | 0.012028 |
| pyperformance.networkx | cpython | unsettled | held out | — | — | — | — | 0.6376 |
| pyperformance.networkx | lucifer | ok | held out | 0.73 | — | — | — | 0.73 |
| pyperformance.networkx | pypy | unsettled | held out | — | — | — | — | 0.53311 |
| pyperformance.networkx_connected_components | cpython | ok | held out | 0.58796 | 1 | — | — | 0.58466 |
| pyperformance.networkx_connected_components | lucifer | unsettled | held out | — | — | — | — | 0.64924 |
| pyperformance.networkx_connected_components | pypy | unsettled | held out | — | — | — | — | 0.57362 |
| pyperformance.networkx_k_core | cpython | ok | held out | 2.8789 | 1 | — | — | 2.8862 |
| pyperformance.networkx_k_core | lucifer | ok | held out | 4.1913 | 1.4559 | — | — | 4.172 |
| pyperformance.networkx_k_core | pypy | unsettled | held out | — | — | — | — | 2.2329 |
| pyperformance.nqueens | cpython | unsettled | held out | — | — | — | — | 0.11508 |
| pyperformance.nqueens | lucifer | ok | held out | 0.13434 | — | — | — | 0.13434 |
| pyperformance.nqueens | pypy | unsettled | held out | — | — | — | — | 0.027872 |
| pyperformance.pidigits | cpython | ok | held out | 0.18788 | 1 | 1.3619 | — | 0.18788 |
| pyperformance.pidigits | lucifer | unsettled | held out | — | — | — | — | 0.23929 |
| pyperformance.pidigits | pypy | ok | held out | 0.13796 | 0.73428 | 1 | — | 0.13792 |
| pyperformance.pprint/pprint_pformat | cpython | ok | held out | 2.3646 | 1 | — | — | 2.3686 |
| pyperformance.pprint/pprint_pformat | lucifer | ok | held out | 4.9318 | 2.0857 | — | — | 4.9318 |
| pyperformance.pprint/pprint_pformat | pypy | unsettled | held out | — | — | — | — | 0.48181 |
| pyperformance.pprint/pprint_safe_repr | cpython | unsettled | held out | — | — | — | — | 1.1686 |
| pyperformance.pprint/pprint_safe_repr | lucifer | ok | held out | 2.4615 | — | — | — | 2.4508 |
| pyperformance.pprint/pprint_safe_repr | pypy | unsettled | held out | — | — | — | — | 0.23077 |
| pyperformance.pyflate | cpython | unsettled | held out | — | — | — | — | 0.61894 |
| pyperformance.pyflate | lucifer | ok | held out | 0.7678 | — | 6.2016 | — | 0.7678 |
| pyperformance.pyflate | pypy | ok | held out | 0.12381 | — | 1 | — | 0.12381 |
| pyperformance.raytrace | cpython | unsettled | held out | — | — | — | — | 0.40785 |
| pyperformance.raytrace | lucifer | unsettled | held out | — | — | — | — | 0.18915 |
| pyperformance.raytrace | pypy | unsettled | held out | — | — | — | — | 0.011862 |
| pyperformance.regex_compile | cpython | unsettled | held out | — | — | — | — | 0.18797 |
| pyperformance.regex_compile | lucifer | unsettled | held out | — | — | — | — | 0.3995 |
| pyperformance.regex_compile | pypy | pyperf stderr differs from oracle | held out | — | — | — | — | — |
| pyperformance.richards | cpython | unsettled | held out | — | — | — | — | 0.066848 |
| pyperformance.richards | lucifer | ok | held out | 0.012151 | — | — | — | 0.012151 |
| pyperformance.richards | pypy | unsettled | held out | — | — | — | — | 0.0024372 |
| pyperformance.richards_super | cpython | unsettled | held out | — | — | — | — | 0.084027 |
| pyperformance.richards_super | lucifer | unsettled | held out | — | — | — | — | 0.016447 |
| pyperformance.richards_super | pypy | unsettled | held out | — | — | — | — | 0.0019062 |
| pyperformance.spectral_norm | cpython | ok | held out | 0.15453 | 1 | — | — | 0.15745 |
| pyperformance.spectral_norm | lucifer | ok | held out | 0.014832 | 0.095976 | — | — | 0.014832 |
| pyperformance.spectral_norm | pypy | unsettled | held out | — | — | — | — | 0.0055786 |
| pyperformance.sqlglot_v2 | cpython | unsettled | held out | — | — | — | — | 0.16607 |
| pyperformance.sqlglot_v2 | lucifer | unsettled | held out | — | — | — | — | 0.34527 |
| pyperformance.sqlglot_v2 | pypy | unsettled | held out | — | — | — | — | 0.11813 |
| pyperformance.sqlglot_v2_optimize | cpython | ok | held out | 0.087121 | 1 | — | — | 0.087121 |
| pyperformance.sqlglot_v2_optimize | lucifer | unsettled | held out | — | — | — | — | 0.2235 |
| pyperformance.sqlglot_v2_optimize | pypy | unsettled | held out | — | — | — | — | 0.15063 |
| pyperformance.sqlglot_v2_parse | cpython | unsettled | held out | — | — | — | — | 0.0020701 |
| pyperformance.sqlglot_v2_parse | lucifer | unsettled | held out | — | — | — | — | 0.0025525 |
| pyperformance.sqlglot_v2_parse | pypy | unsettled | held out | — | — | — | — | 0.0013399 |
| pyperformance.sqlglot_v2_transpile | cpython | unsettled | held out | — | — | — | — | 0.002222 |
| pyperformance.sqlglot_v2_transpile | lucifer | unsettled | held out | — | — | — | — | 0.0033231 |
| pyperformance.sqlglot_v2_transpile | pypy | unsettled | held out | — | — | — | — | 0.0022008 |
| pyperformance.tomli_loads | cpython | ok | held out | 2.7127 | 1 | 2.2814 | — | 2.7241 |
| pyperformance.tomli_loads | lucifer | ok | held out | 2.4971 | 0.92051 | 2.1 | — | 2.4618 |
| pyperformance.tomli_loads | pypy | ok | held out | 1.1891 | 0.43833 | 1 | — | 1.1891 |
| pyperformance.unpack_sequence | cpython | unsettled | held out | — | — | — | — | 6.8536e-08 |
| pyperformance.unpack_sequence | lucifer | unsettled | held out | — | — | — | — | 6.4832e-08 |
| pyperformance.unpack_sequence | pypy | unsettled | held out | — | — | — | — | 1.2736e-10 |

## 2. Application paths

Flask reports the median of three fresh servers at each layout. Its profiling row uses a separate warm server. Counts are per completed request or pyperf operation. The six percentages partition leaf cycle samples. Shared-client layouts are diagnostic, and failed requests disqualify a gate.

| program | runtime | status | ops_per_s | over_cpython | final_ops_per_s | rps | rps_min | rps_max | rps_over_cpython | scaling | p99_ms | errors |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| flask.1 | cpython | ok | — | — | — | 2189.3 | 2185.1 | 2195.8 | 1 | 1 | 7559.5 | 0 |
| flask.1 | lucifer | ok | — | — | — | 583.7 | 580.1 | 592.9 | 0.26661 | 1 | 8007.9 | 0 |
| flask.1 | pypy | ok | — | — | — | 8490.1 | 8157.3 | 8548.7 | 3.878 | 1 | 145.74 | 0 |
| flask.1.profile | cpython | insufficient disjoint CPUs | — | — | — | — | — | — | — | — | — | — |
| flask.1.profile | lucifer | insufficient disjoint CPUs | — | — | — | — | — | — | — | — | — | — |
| flask.1.profile | pypy | insufficient disjoint CPUs | — | — | — | — | — | — | — | — | — | — |
| flask.1.shared.profile | cpython | ok | — | — | — | — | — | — | — | — | — | 0 |
| flask.1.shared.profile | lucifer | ok | — | — | — | — | — | — | — | — | — | 0 |
| flask.1.shared.profile | pypy | ok | — | — | — | — | — | — | — | — | — | 0 |
| flask.8 | cpython | insufficient disjoint CPUs | — | — | — | — | — | — | — | — | — | — |
| flask.8 | lucifer | insufficient disjoint CPUs | — | — | — | — | — | — | — | — | — | — |
| flask.8 | pypy | insufficient disjoint CPUs | — | — | — | — | — | — | — | — | — | — |
| flask.8.profile | cpython | insufficient disjoint CPUs | — | — | — | — | — | — | — | — | — | — |
| flask.8.profile | lucifer | insufficient disjoint CPUs | — | — | — | — | — | — | — | — | — | — |
| flask.8.profile | pypy | insufficient disjoint CPUs | — | — | — | — | — | — | — | — | — | — |
| flask.8.shared | cpython | ok | — | — | — | 8501.9 | 8390.4 | 8567.5 | 1 | — | 34.326 | 0 |
| flask.8.shared | lucifer | ok | — | — | — | 3448.1 | 3405.4 | 3534.8 | 0.40557 | — | 61.752 | 0 |
| flask.8.shared | pypy | ok | — | — | — | 2893.9 | 2826.2 | 2922.1 | 0.34038 | — | 71.297 | 0 |
| pyperformance.chameleon | cpython | unsettled | — | — | 45.973 | — | — | — | — | — | — | — |
| pyperformance.chameleon | lucifer | unsettled | — | — | 26.575 | — | — | — | — | — | — | — |
| pyperformance.chameleon | pypy | unsettled | — | — | 230.9 | — | — | — | — | — | — | — |
| pyperformance.django_template | cpython | unsettled | — | — | 18.458 | — | — | — | — | — | — | — |
| pyperformance.django_template | lucifer | unsettled | — | — | 15.321 | — | — | — | — | — | — | — |
| pyperformance.django_template | pypy | unsettled | — | — | 137.35 | — | — | — | — | — | — | — |
| pyperformance.docutils | cpython | ok | 0.28459 | 1 | 0.28349 | — | — | — | — | — | — | — |
| pyperformance.docutils | lucifer | ok | 0.12136 | 2.3451 | 0.12152 | — | — | — | — | — | — | — |
| pyperformance.docutils | pypy | unsettled | — | — | 0.28099 | — | — | — | — | — | — | — |
| pyperformance.fastapi | cpython | unsettled | — | — | 2.6624 | — | — | — | — | — | — | — |
| pyperformance.fastapi | lucifer | unsettled | — | — | 1.0926 | — | — | — | — | — | — | — |
| pyperformance.fastapi | pypy | pyperf worker exit 1 | — | — | — | — | — | — | — | — | — | — |
| pyperformance.genshi/genshi_text | cpython | unsettled | — | — | 25.06 | — | — | — | — | — | — | — |
| pyperformance.genshi/genshi_text | lucifer | unsettled | — | — | 15.132 | — | — | — | — | — | — | — |
| pyperformance.genshi/genshi_text | pypy | unsettled | — | — | 29.069 | — | — | — | — | — | — | — |
| pyperformance.genshi/genshi_xml | cpython | unsettled | — | — | 12.524 | — | — | — | — | — | — | — |
| pyperformance.genshi/genshi_xml | lucifer | unsettled | — | — | 3.8511 | — | — | — | — | — | — | — |
| pyperformance.genshi/genshi_xml | pypy | unsettled | — | — | 27.688 | — | — | — | — | — | — | — |
| pyperformance.html5lib | cpython | unsettled | — | — | 12.046 | — | — | — | — | — | — | — |
| pyperformance.html5lib | lucifer | unsettled | — | — | 5.6828 | — | — | — | — | — | — | — |
| pyperformance.html5lib | pypy | unsettled | — | — | 27.869 | — | — | — | — | — | — | — |
| pyperformance.mako | cpython | unsettled | — | — | 49.434 | — | — | — | — | — | — | — |
| pyperformance.mako | lucifer | unsettled | — | — | 24.034 | — | — | — | — | — | — | — |
| pyperformance.mako | pypy | unsettled | — | — | 106.18 | — | — | — | — | — | — | — |
| pyperformance.sphinx | cpython | ok | 0.68949 | 1 | 0.68949 | — | — | — | — | — | — | — |
| pyperformance.sphinx | lucifer | ok | 0.2588 | 2.6642 | 0.26016 | — | — | — | — | — | — | — |
| pyperformance.sphinx | pypy | unsettled | — | — | 0.36595 | — | — | — | — | — | — | — |
| pyperformance.tornado_http | cpython | ok | 5.6305 | 1 | 5.5945 | — | — | — | — | — | — | — |
| pyperformance.tornado_http | lucifer | unsettled | — | — | 1.5457 | — | — | — | — | — | — | — |
| pyperformance.tornado_http | pypy | unsettled | — | — | 2.2605 | — | — | — | — | — | — | — |

### Warm CPU attribution

| program | runtime | profile_status | profile_units | instructions_per_unit | cycles_per_unit | compiled_pct | interpreter_pct | runtime_c_pct | native_pct | kernel_pct | unknown_pct |
|---|---|---|---|---|---|---|---|---|---|---|---|
| flask.1.profile | cpython | insufficient disjoint CPUs | — | — | — | — | — | — | — | — | — |
| flask.1.profile | lucifer | insufficient disjoint CPUs | — | — | — | — | — | — | — | — | — |
| flask.1.profile | pypy | insufficient disjoint CPUs | — | — | — | — | — | — | — | — | — |
| flask.1.shared.profile | cpython | ok | 10104 | 1.5161e+06 | 1.9347e+06 | 0 | 31.283 | 63.246 | 0.29233 | 5.1789 | 0 |
| flask.1.shared.profile | lucifer | ok | 2626 | 4.3006e+06 | 7.8319e+06 | 30.268 | 4.1099 | 55.079 | 0.098615 | 10.444 | 0 |
| flask.1.shared.profile | pypy | ok | 35821 | 3.9042e+05 | 4.9526e+05 | 0 | 1.4649 | 55.095 | 0 | 16.64 | 26.8 |
| flask.8.profile | cpython | insufficient disjoint CPUs | — | — | — | — | — | — | — | — | — |
| flask.8.profile | lucifer | insufficient disjoint CPUs | — | — | — | — | — | — | — | — | — |
| flask.8.profile | pypy | insufficient disjoint CPUs | — | — | — | — | — | — | — | — | — |
| pyperformance.chameleon | cpython | ok | 42 | 1.7021e+08 | 9.4918e+07 | 0 | 37.249 | 62.751 | 0 | 0 | 0 |
| pyperformance.chameleon | lucifer | ok | 24 | 2.6787e+08 | 1.4665e+08 | 39.843 | 0 | 60.157 | 0 | 0 | 0 |
| pyperformance.chameleon | pypy | ok | 132 | 3.1613e+07 | 1.9154e+07 | 0 | 0.83208 | 69.087 | 0 | 1.6636 | 28.418 |
| pyperformance.django_template | cpython | ok | 16 | 3.9527e+08 | 2.2996e+08 | 0 | 41.119 | 58.881 | 0 | 0 | 0 |
| pyperformance.django_template | lucifer | ok | 14 | 4.3815e+08 | 2.791e+08 | 33.449 | 1.619 | 64.392 | 0 | 0.54004 | 0 |
| pyperformance.django_template | pypy | ok | 110 | 5.6258e+07 | 3.0926e+07 | 0 | 0 | 55.167 | 0 | 1.8577 | 42.975 |
| pyperformance.docutils | cpython | ok | 1 | 1.9187e+10 | 1.4712e+10 | 0 | 24.753 | 73.542 | 0 | 1.7055 | 0 |
| pyperformance.docutils | lucifer | ok | 1 | 3.7716e+10 | 3.3193e+10 | 34.026 | 1.963 | 62.555 | 0 | 1.4563 | 0 |
| pyperformance.docutils | pypy | ok | 1 | 1.3984e+10 | 1.3359e+10 | 0 | 4.2268 | 75.711 | 0 | 0.94461 | 19.118 |
| pyperformance.fastapi | cpython | ok | 3 | 2.0989e+09 | 1.6388e+09 | 0 | 42.595 | 48.454 | 0.42889 | 7.5329 | 0.98906 |
| pyperformance.fastapi | lucifer | ok | 1 | 2.9945e+09 | 3.7729e+09 | 27.734 | 5.0434 | 61.683 | 0 | 5.3432 | 0.196 |
| pyperformance.genshi/genshi_text | cpython | ok | 20 | 3.0041e+08 | 1.6001e+08 | 0 | 52.638 | 46.041 | 0 | 1.3207 | 0 |
| pyperformance.genshi/genshi_text | lucifer | ok | 13 | 4.3336e+08 | 2.6757e+08 | 27.622 | 4.6746 | 66.493 | 0 | 1.2101 | 0 |
| pyperformance.genshi/genshi_text | pypy | ok | 18 | 2.2388e+08 | 1.4359e+08 | 0 | 0.81961 | 82.13 | 0 | 0.81951 | 16.231 |
| pyperformance.genshi/genshi_xml | cpython | ok | 9 | 5.7372e+08 | 3.3678e+08 | 0 | 41.216 | 55.998 | 0.6963 | 2.0894 | 0 |
| pyperformance.genshi/genshi_xml | lucifer | ok | 3 | 1.2331e+09 | 9.3507e+08 | 35.991 | 2.2512 | 61.668 | 0 | 0 | 0.089699 |
| pyperformance.genshi/genshi_xml | pypy | ok | 21 | 2.5468e+08 | 1.7911e+08 | 0 | 2.2435 | 65.237 | 0 | 1.1218 | 31.397 |
| pyperformance.html5lib | cpython | ok | 10 | 4.7845e+08 | 3.7342e+08 | 0 | 40.944 | 56.803 | 1.1255 | 1.127 | 0 |
| pyperformance.html5lib | lucifer | ok | 5 | 8.837e+08 | 7.5255e+08 | 24.658 | 11.89 | 62.891 | 0 | 0.56057 | 0 |
| pyperformance.html5lib | pypy | ok | 23 | 1.779e+08 | 1.4123e+08 | 0 | 0 | 78.618 | 0 | 1.2965 | 20.085 |
| pyperformance.mako | cpython | ok | 42 | 1.5935e+08 | 8.3891e+07 | 0 | 26.887 | 72.516 | 0 | 0.59736 | 0 |
| pyperformance.mako | lucifer | ok | 24 | 3.5973e+08 | 1.6092e+08 | 29.942 | 0 | 69.512 | 0 | 0.5455 | 0 |
| pyperformance.mako | pypy | ok | 84 | 6.1086e+07 | 3.7684e+07 | 0 | 1.3286 | 80.05 | 0 | 0.66345 | 17.957 |
| pyperformance.sphinx | cpython | ok | 1 | 8.4514e+09 | 5.7438e+09 | 0 | 25.296 | 70.675 | 1.835 | 2.1937 | 0 |
| pyperformance.sphinx | lucifer | ok | 1 | 1.9848e+10 | 1.6662e+10 | 28.757 | 2.2859 | 66.812 | 1.1355 | 1.0095 | 0 |
| pyperformance.sphinx | pypy | ok | 1 | 1.1628e+10 | 1.034e+10 | 0 | 4.5012 | 81.823 | 0 | 1.4678 | 12.208 |
| pyperformance.tornado_http | cpython | ok | 5 | 5.3004e+08 | 7.2953e+08 | 0 | 31.884 | 54.228 | 1.7164 | 12.171 | 0 |
| pyperformance.tornado_http | lucifer | ok | 2 | 2.013e+09 | 2.2893e+09 | 20.111 | 5.9135 | 61.925 | 0 | 12.051 | 0 |
| pyperformance.tornado_http | pypy | ok | 2 | 1.343e+09 | 1.713e+09 | 0 | 3.4114 | 84.17 | 0 | 8.039 | 4.3794 |

## 3. Short programs

Whole-process wall time at the corpus's fixed work and output checks. The CLI, startup and additional fresh-process workers have separate denominators.

| program | runtime | status | seconds | over_cpython |
|---|---|---|---|---|
| cli.black | cpython | ok | 0.47117 | 1 |
| cli.black | lucifer | ok | 2.0293 | 4.3071 |
| cli.black | pypy | ok | 1.6956 | 3.5988 |
| cli.isort | cpython | ok | 0.13783 | 1 |
| cli.isort | lucifer | ok | 0.48145 | 3.493 |
| cli.isort | pypy | ok | 0.26006 | 1.8868 |
| cli.markdown | cpython | ok | 0.12225 | 1 |
| cli.markdown | lucifer | ok | 0.53574 | 4.3822 |
| cli.markdown | pypy | ok | 0.21399 | 1.7504 |
| cli.pycodestyle | cpython | ok | 0.88031 | 1 |
| cli.pycodestyle | lucifer | ok | 3.3411 | 3.7953 |
| cli.pycodestyle | pypy | ok | 1.9331 | 2.1959 |
| cli.pyflakes | cpython | ok | 0.76585 | 1 |
| cli.pyflakes | lucifer | ok | 1.457 | 1.9024 |
| cli.pyflakes | pypy | ok | 1.6643 | 2.1732 |
| cli.pygments | cpython | ok | 0.28842 | 1 |
| cli.pygments | lucifer | ok | 2.0968 | 7.2702 |
| cli.pygments | pypy | ok | 0.90062 | 3.1226 |
| pyperformance.2to3 | cpython | ok | 0.3998 | 1 |
| pyperformance.2to3 | lucifer | ok | 2.0574 | 5.146 |
| pyperformance.2to3 | pypy | ok | 1.447 | 3.6194 |
| startup.import_stdlib | cpython | ok | 0.10428 | 1 |
| startup.import_stdlib | lucifer | ok | 0.27322 | 2.6202 |
| startup.import_stdlib | pypy | ok | 0.14129 | 1.3549 |
| startup.python_startup | cpython | ok | 0.014785 | 1 |
| startup.python_startup | lucifer | ok | 0.03033 | 2.0514 |
| startup.python_startup | pypy | ok | 0.026543 | 1.7953 |
| startup.python_startup_no_site | cpython | ok | 0.010377 | 1 |
| startup.python_startup_no_site | lucifer | ok | 0.019347 | 1.8644 |
| startup.python_startup_no_site | pypy | ok | 0.022876 | 2.2045 |

## 4. Native and mixed workloads

Median time over the final five values. The stock native share includes callees up to Python re-entry. Score native-majority, mixed and boxed boundary calls separately.

| program | runtime | status | seconds | over_cpython | group | stock_native_pct |
|---|---|---|---|---|---|---|
| boundary.cmath.isclose | cpython | ok | 9.7764e-08 | 1 | — | — |
| boundary.cmath.isclose | lucifer | ok | 1.8138e-07 | 1.8553 | — | — |
| boundary.cmath.isclose | pypy | ok | 5.1018e-08 | 0.52185 | — | — |
| boundary.cmath.isfinite | cpython | ok | 5.9516e-08 | 1 | — | — |
| boundary.cmath.isfinite | lucifer | ok | 1.7156e-07 | 2.8826 | — | — |
| boundary.cmath.isfinite | pypy | ok | 3.1337e-09 | 0.052654 | — | — |
| pyperformance.async_tree | cpython | ok | 0.38504 | 1 | mixed | 24.294 |
| pyperformance.async_tree | lucifer | ok | 0.81826 | 2.1251 | mixed | 24.294 |
| pyperformance.async_tree | pypy | ok | 0.512 | 1.3297 | mixed | 24.294 |
| pyperformance.async_tree_cpu_io_mixed | cpython | ok | 0.73233 | 1 | mixed | 49.321 |
| pyperformance.async_tree_cpu_io_mixed | lucifer | ok | 3.5301 | 4.8203 | mixed | 49.321 |
| pyperformance.async_tree_cpu_io_mixed | pypy | ok | 0.91205 | 1.2454 | mixed | 49.321 |
| pyperformance.async_tree_cpu_io_mixed_tg | cpython | ok | 0.68518 | 1 | native majority | 50.267 |
| pyperformance.async_tree_cpu_io_mixed_tg | lucifer | ok | 3.4337 | 5.0114 | native majority | 50.267 |
| pyperformance.async_tree_cpu_io_mixed_tg | pypy | ok | 0.83609 | 1.2202 | native majority | 50.267 |
| pyperformance.async_tree_eager | cpython | ok | 0.16609 | 1 | mixed | 23.321 |
| pyperformance.async_tree_eager | lucifer | ok | 0.41634 | 2.5067 | mixed | 23.321 |
| pyperformance.async_tree_eager | pypy | ok | 0.47708 | 2.8724 | mixed | 23.321 |
| pyperformance.async_tree_eager_cpu_io_mixed | cpython | ok | 0.58527 | 1 | native majority | 62.289 |
| pyperformance.async_tree_eager_cpu_io_mixed | lucifer | ok | 3.334 | 5.6965 | native majority | 62.289 |
| pyperformance.async_tree_eager_cpu_io_mixed | pypy | ok | 0.97736 | 1.6699 | native majority | 62.289 |
| pyperformance.async_tree_eager_cpu_io_mixed_tg | cpython | ok | 0.6764 | 1 | native majority | 53.006 |
| pyperformance.async_tree_eager_cpu_io_mixed_tg | lucifer | ok | 3.4325 | 5.0746 | native majority | 53.006 |
| pyperformance.async_tree_eager_cpu_io_mixed_tg | pypy | ok | 0.85209 | 1.2597 | native majority | 53.006 |
| pyperformance.async_tree_eager_io | cpython | ok | 0.7975 | 1 | mixed | 20.746 |
| pyperformance.async_tree_eager_io | lucifer | ok | 1.5202 | 1.9062 | mixed | 20.746 |
| pyperformance.async_tree_eager_io | pypy | ok | 0.6946 | 0.87097 | mixed | 20.746 |
| pyperformance.async_tree_eager_io_tg | cpython | ok | 0.71919 | 1 | mixed | 28.392 |
| pyperformance.async_tree_eager_io_tg | lucifer | ok | 1.5077 | 2.0964 | mixed | 28.392 |
| pyperformance.async_tree_eager_io_tg | pypy | ok | 0.58386 | 0.81183 | mixed | 28.392 |
| pyperformance.async_tree_eager_memoization | cpython | ok | 0.30618 | 1 | mixed | 20.6 |
| pyperformance.async_tree_eager_memoization | lucifer | ok | 1.0892 | 3.5575 | mixed | 20.6 |
| pyperformance.async_tree_eager_memoization | pypy | ok | 0.58356 | 1.9059 | mixed | 20.6 |
| pyperformance.async_tree_eager_memoization_tg | cpython | ok | 0.38626 | 1 | mixed | 27.339 |
| pyperformance.async_tree_eager_memoization_tg | lucifer | ok | 1.3683 | 3.5425 | mixed | 27.339 |
| pyperformance.async_tree_eager_memoization_tg | pypy | ok | 0.5082 | 1.3157 | mixed | 27.339 |
| pyperformance.async_tree_eager_tg | cpython | ok | 0.28303 | 1 | mixed | 20.802 |
| pyperformance.async_tree_eager_tg | lucifer | ok | 0.71919 | 2.541 | mixed | 20.802 |
| pyperformance.async_tree_eager_tg | pypy | ok | 0.41562 | 1.4685 | mixed | 20.802 |
| pyperformance.async_tree_io | cpython | ok | 0.77044 | 1 | mixed | 21.995 |
| pyperformance.async_tree_io | lucifer | ok | 1.5157 | 1.9673 | mixed | 21.995 |
| pyperformance.async_tree_io | pypy | ok | 0.67765 | 0.87956 | mixed | 21.995 |
| pyperformance.async_tree_io_tg | cpython | ok | 0.75387 | 1 | mixed | 23.162 |
| pyperformance.async_tree_io_tg | lucifer | ok | 1.5305 | 2.0302 | mixed | 23.162 |
| pyperformance.async_tree_io_tg | pypy | ok | 0.606 | 0.80385 | mixed | 23.162 |
| pyperformance.async_tree_memoization | cpython | ok | 0.44217 | 1 | mixed | 22.383 |
| pyperformance.async_tree_memoization | lucifer | ok | 1.0201 | 2.307 | mixed | 22.383 |
| pyperformance.async_tree_memoization | pypy | ok | 0.56826 | 1.2851 | mixed | 22.383 |
| pyperformance.async_tree_memoization_tg | cpython | ok | 0.41894 | 1 | mixed | 20.815 |
| pyperformance.async_tree_memoization_tg | lucifer | ok | 0.93927 | 2.242 | mixed | 20.815 |
| pyperformance.async_tree_memoization_tg | pypy | ok | 0.53422 | 1.2752 | mixed | 20.815 |
| pyperformance.async_tree_tg | cpython | ok | 0.32013 | 1 | mixed | 20.219 |
| pyperformance.async_tree_tg | lucifer | ok | 0.79043 | 2.4691 | mixed | 20.219 |
| pyperformance.async_tree_tg | pypy | ok | 0.42315 | 1.3218 | mixed | 20.219 |
| pyperformance.asyncio_tcp | cpython | ok | 0.50667 | 1 | mixed | 14.449 |
| pyperformance.asyncio_tcp | lucifer | ok | 0.62379 | 1.2312 | mixed | 14.449 |
| pyperformance.asyncio_tcp | pypy | ok | 3.2572 | 6.4286 | mixed | 14.449 |
| pyperformance.asyncio_tcp_ssl | cpython | ok | 1.8649 | 1 | native majority | 78.521 |
| pyperformance.asyncio_tcp_ssl | lucifer | ok | 11.092 | 5.9479 | native majority | 78.521 |
| pyperformance.asyncio_tcp_ssl | pypy | ok | 9.0374 | 4.8461 | native majority | 78.521 |
| pyperformance.asyncio_websockets | cpython | ok | 0.54391 | 1 | native majority | 97.629 |
| pyperformance.asyncio_websockets | lucifer | ok | 0.55952 | 1.0287 | native majority | 97.629 |
| pyperformance.asyncio_websockets | pypy | ok | 0.61103 | 1.1234 | native majority | 97.629 |
| pyperformance.concurrent_imap/bench_mp_pool | cpython | ok | 0.019342 | 1 | mixed | 27.14 |
| pyperformance.concurrent_imap/bench_mp_pool | lucifer | ok | 0.094287 | 4.8747 | mixed | 27.14 |
| pyperformance.concurrent_imap/bench_mp_pool | pypy | ok | 0.034556 | 1.7866 | mixed | 27.14 |
| pyperformance.concurrent_imap/bench_thread_pool | cpython | ok | 0.00219 | 1 | mixed | 27.14 |
| pyperformance.concurrent_imap/bench_thread_pool | lucifer | ok | 0.0095706 | 4.3702 | mixed | 27.14 |
| pyperformance.concurrent_imap/bench_thread_pool | pypy | ok | 0.0046057 | 2.1031 | mixed | 27.14 |
| pyperformance.dask | cpython | excluded by corpus lock or missing oracle check | — | — | unmeasured guard | — |
| pyperformance.dask | lucifer | excluded by corpus lock or missing oracle check | — | — | unmeasured guard | — |
| pyperformance.dask | pypy | excluded by corpus lock or missing oracle check | — | — | unmeasured guard | — |
| pyperformance.dulwich_log | cpython | ok | 0.090562 | 1 | mixed | 24.167 |
| pyperformance.dulwich_log | lucifer | ok | 0.32419 | 3.5797 | mixed | 24.167 |
| pyperformance.dulwich_log | pypy | ok | 0.052283 | 0.57732 | mixed | 24.167 |
| pyperformance.float | cpython | ok | 0.091571 | 1 | mixed | 11.628 |
| pyperformance.float | lucifer | ok | 0.038713 | 0.42276 | mixed | 11.628 |
| pyperformance.float | pypy | ok | 0.011794 | 0.1288 | mixed | 11.628 |
| pyperformance.gc_collect | cpython | ok | 0.0025611 | 1 | native majority | 93.532 |
| pyperformance.gc_collect | lucifer | ok | 0.0033137 | 1.2939 | native majority | 93.532 |
| pyperformance.gc_collect | pypy | ok | 0.0066877 | 2.6113 | native majority | 93.532 |
| pyperformance.gc_traversal | cpython | ok | 0.0035216 | 1 | native majority | 95.392 |
| pyperformance.gc_traversal | lucifer | ok | 0.0063457 | 1.802 | native majority | 95.392 |
| pyperformance.gc_traversal | pypy | ok | 0.008724 | 2.4773 | native majority | 95.392 |
| pyperformance.json_dumps | cpython | ok | 0.016392 | 1 | native majority | 73.092 |
| pyperformance.json_dumps | lucifer | ok | 0.045652 | 2.7851 | native majority | 73.092 |
| pyperformance.json_dumps | pypy | ok | 0.0075045 | 0.45782 | native majority | 73.092 |
| pyperformance.json_loads | cpython | ok | 4.1811e-05 | 1 | native majority | 88.692 |
| pyperformance.json_loads | lucifer | ok | 0.00011099 | 2.6544 | native majority | 88.692 |
| pyperformance.json_loads | pypy | ok | 1.1208e-05 | 0.26807 | native majority | 88.692 |
| pyperformance.logging/logging_format | cpython | ok | 1.1454e-05 | 1 | mixed | 7.6145 |
| pyperformance.logging/logging_format | lucifer | ok | 1.4209e-05 | 1.2405 | mixed | 7.6145 |
| pyperformance.logging/logging_format | pypy | ok | 3.6359e-06 | 0.31744 | mixed | 7.6145 |
| pyperformance.logging/logging_silent | cpython | ok | 1.121e-07 | 1 | mixed | 7.6145 |
| pyperformance.logging/logging_silent | lucifer | ok | 1.4272e-07 | 1.2732 | mixed | 7.6145 |
| pyperformance.logging/logging_silent | pypy | ok | 3.0952e-09 | 0.027612 | mixed | 7.6145 |
| pyperformance.logging/logging_simple | cpython | ok | 1.0158e-05 | 1 | mixed | 7.6145 |
| pyperformance.logging/logging_simple | lucifer | ok | 1.1856e-05 | 1.1672 | mixed | 7.6145 |
| pyperformance.logging/logging_simple | pypy | ok | 3.2549e-06 | 0.32043 | mixed | 7.6145 |
| pyperformance.pathlib | cpython | ok | 0.026169 | 1 | mixed | 41.694 |
| pyperformance.pathlib | lucifer | ok | 0.032354 | 1.2364 | mixed | 41.694 |
| pyperformance.pathlib | pypy | ok | 0.01194 | 0.45626 | mixed | 41.694 |
| pyperformance.pickle | cpython | ok | 1.5013e-05 | 1 | native majority | 100 |
| pyperformance.pickle | lucifer | ok | 2.6565e-05 | 1.7694 | native majority | 100 |
| pyperformance.pickle | pypy | ok | 2.4602e-05 | 1.6387 | native majority | 100 |
| pyperformance.pickle_dict | cpython | ok | 3.7174e-05 | 1 | native majority | 99.127 |
| pyperformance.pickle_dict | lucifer | ok | 4.7267e-05 | 1.2715 | native majority | 99.127 |
| pyperformance.pickle_dict | pypy | ok | 0.0001532 | 4.1211 | native majority | 99.127 |
| pyperformance.pickle_list | cpython | ok | 6.4372e-06 | 1 | native majority | 99.434 |
| pyperformance.pickle_list | lucifer | ok | 7.9213e-06 | 1.2306 | native majority | 99.434 |
| pyperformance.pickle_list | pypy | ok | 2.4111e-05 | 3.7456 | native majority | 99.434 |
| pyperformance.pickle_pure_python | cpython | ok | 0.00051136 | 1 | mixed | 12.923 |
| pyperformance.pickle_pure_python | lucifer | ok | 0.00071296 | 1.3943 | mixed | 12.923 |
| pyperformance.pickle_pure_python | pypy | ok | 2.7604e-05 | 0.053982 | mixed | 12.923 |
| pyperformance.regex_dna | cpython | ok | 0.2054 | 1 | native majority | 100 |
| pyperformance.regex_dna | lucifer | ok | 0.15312 | 0.74546 | native majority | 100 |
| pyperformance.regex_dna | pypy | ok | 0.090345 | 0.43984 | native majority | 100 |
| pyperformance.regex_effbot | cpython | ok | 0.0029627 | 1 | native majority | 93.214 |
| pyperformance.regex_effbot | lucifer | ok | 0.0049757 | 1.6794 | native majority | 93.214 |
| pyperformance.regex_effbot | pypy | ok | 0.0028356 | 0.95708 | native majority | 93.214 |
| pyperformance.regex_v8 | cpython | ok | 0.022995 | 1 | native majority | 87.088 |
| pyperformance.regex_v8 | lucifer | ok | 0.096716 | 4.206 | native majority | 87.088 |
| pyperformance.regex_v8 | pypy | ok | 0.022391 | 0.97373 | native majority | 87.088 |
| pyperformance.scimark/scimark_fft | cpython | ok | 0.51862 | 1 | mixed | 19.629 |
| pyperformance.scimark/scimark_fft | lucifer | ok | 0.05457 | 0.10522 | mixed | 19.629 |
| pyperformance.scimark/scimark_fft | pypy | ok | 0.0095742 | 0.018461 | mixed | 19.629 |
| pyperformance.scimark/scimark_lu | cpython | ok | 0.16964 | 1 | mixed | 19.629 |
| pyperformance.scimark/scimark_lu | lucifer | ok | 0.028373 | 0.16726 | mixed | 19.629 |
| pyperformance.scimark/scimark_lu | pypy | ok | 0.0013027 | 0.0076791 | mixed | 19.629 |
| pyperformance.scimark/scimark_monte_carlo | cpython | ok | 0.10728 | 1 | mixed | 19.629 |
| pyperformance.scimark/scimark_monte_carlo | lucifer | ok | 0.030636 | 0.28557 | mixed | 19.629 |
| pyperformance.scimark/scimark_monte_carlo | pypy | ok | 0.0035659 | 0.03324 | mixed | 19.629 |
| pyperformance.scimark/scimark_sor | cpython | ok | 0.16064 | 1 | mixed | 19.629 |
| pyperformance.scimark/scimark_sor | lucifer | ok | 0.0057591 | 0.035851 | mixed | 19.629 |
| pyperformance.scimark/scimark_sor | pypy | ok | 0.00099063 | 0.0061667 | mixed | 19.629 |
| pyperformance.scimark/scimark_sparse_mat_mult | cpython | ok | 0.0066824 | 1 | mixed | 19.629 |
| pyperformance.scimark/scimark_sparse_mat_mult | lucifer | ok | 0.00068756 | 0.10289 | mixed | 19.629 |
| pyperformance.scimark/scimark_sparse_mat_mult | pypy | ok | 0.00023925 | 0.035803 | mixed | 19.629 |
| pyperformance.sqlalchemy_declarative | cpython | ok | 0.18846 | 1 | mixed | 8.3601 |
| pyperformance.sqlalchemy_declarative | lucifer | ok | 0.57172 | 3.0336 | mixed | 8.3601 |
| pyperformance.sqlalchemy_declarative | pypy | pyperf worker exit 1 | — | — | mixed | 8.3601 |
| pyperformance.sqlalchemy_imperative | cpython | ok | 0.028134 | 1 | mixed | 9.6815 |
| pyperformance.sqlalchemy_imperative | lucifer | ok | 0.060805 | 2.1612 | mixed | 9.6815 |
| pyperformance.sqlalchemy_imperative | pypy | ok | 0.017347 | 0.61659 | mixed | 9.6815 |
| pyperformance.sqlite_synth | cpython | ok | 2.5916e-06 | 1 | native majority | 79.48 |
| pyperformance.sqlite_synth | lucifer | ok | 9.507e-06 | 3.6683 | native majority | 79.48 |
| pyperformance.sqlite_synth | pypy | ok | 5.4806e-06 | 2.1147 | native majority | 79.48 |
| pyperformance.sympy/sympy_expand | cpython | ok | 0.57978 | 1 | mixed | 6.8864 |
| pyperformance.sympy/sympy_expand | lucifer | ok | 1.3552 | 2.3375 | mixed | 6.8864 |
| pyperformance.sympy/sympy_expand | pypy | ok | 0.15537 | 0.26799 | mixed | 6.8864 |
| pyperformance.sympy/sympy_integrate | cpython | ok | 0.021088 | 1 | mixed | 6.8864 |
| pyperformance.sympy/sympy_integrate | lucifer | ok | 0.095269 | 4.5178 | mixed | 6.8864 |
| pyperformance.sympy/sympy_integrate | pypy | ok | 0.082178 | 3.897 | mixed | 6.8864 |
| pyperformance.sympy/sympy_str | cpython | ok | 0.32684 | 1 | mixed | 6.8864 |
| pyperformance.sympy/sympy_str | lucifer | ok | 0.89686 | 2.7441 | mixed | 6.8864 |
| pyperformance.sympy/sympy_str | pypy | ok | 0.13714 | 0.41958 | mixed | 6.8864 |
| pyperformance.sympy/sympy_sum | cpython | ok | 0.19341 | 1 | mixed | 6.8864 |
| pyperformance.sympy/sympy_sum | lucifer | ok | 0.61041 | 3.156 | mixed | 6.8864 |
| pyperformance.sympy/sympy_sum | pypy | ok | 0.14373 | 0.74312 | mixed | 6.8864 |
| pyperformance.telco | cpython | ok | 0.01246 | 1 | native majority | 60.366 |
| pyperformance.telco | lucifer | ok | 0.032412 | 2.6014 | native majority | 60.366 |
| pyperformance.telco | pypy | ok | 0.0086529 | 0.69448 | native majority | 60.366 |
| pyperformance.typing_runtime_protocols | cpython | ok | 0.0002757 | 1 | mixed | 12.813 |
| pyperformance.typing_runtime_protocols | lucifer | ok | 0.0006913 | 2.5074 | mixed | 12.813 |
| pyperformance.typing_runtime_protocols | pypy | ok | 0.000819 | 2.9706 | mixed | 12.813 |
| pyperformance.unpickle | cpython | ok | 2.3274e-05 | 1 | native majority | 88.214 |
| pyperformance.unpickle | lucifer | ok | 4.4333e-05 | 1.9048 | native majority | 88.214 |
| pyperformance.unpickle | pypy | ok | 4.9274e-05 | 2.1171 | native majority | 88.214 |
| pyperformance.unpickle_list | cpython | ok | 5.7764e-06 | 1 | native majority | 93.488 |
| pyperformance.unpickle_list | lucifer | ok | 8.6443e-06 | 1.4965 | native majority | 93.488 |
| pyperformance.unpickle_list | pypy | ok | 1.5851e-05 | 2.7442 | native majority | 93.488 |
| pyperformance.unpickle_pure_python | cpython | ok | 0.0003297 | 1 | mixed | 8.8334 |
| pyperformance.unpickle_pure_python | lucifer | ok | 0.00054058 | 1.6396 | mixed | 8.8334 |
| pyperformance.unpickle_pure_python | pypy | ok | 4.5938e-05 | 0.13933 | mixed | 8.8334 |
| pyperformance.xdsl | cpython | ok | 0.063127 | 1 | mixed | 5.7943 |
| pyperformance.xdsl | lucifer | ok | 0.14441 | 2.2877 | mixed | 5.7943 |
| pyperformance.xdsl | pypy | ok | 0.010009 | 0.15854 | mixed | 5.7943 |
| pyperformance.xml_etree | pypy | pyperf benchmark names differ from oracle | — | — | native majority | 54.984 |
| pyperformance.xml_etree/xml_etree_generate | cpython | ok | 0.14791 | 1 | native majority | 54.984 |
| pyperformance.xml_etree/xml_etree_generate | lucifer | ok | 0.39071 | 2.6416 | native majority | 54.984 |
| pyperformance.xml_etree/xml_etree_iterparse | cpython | ok | 0.11955 | 1 | native majority | 54.984 |
| pyperformance.xml_etree/xml_etree_iterparse | lucifer | ok | 0.24012 | 2.0085 | native majority | 54.984 |
| pyperformance.xml_etree/xml_etree_parse | cpython | ok | 0.18679 | 1 | native majority | 54.984 |
| pyperformance.xml_etree/xml_etree_parse | lucifer | ok | 0.26146 | 1.3998 | native majority | 54.984 |
| pyperformance.xml_etree/xml_etree_process | cpython | ok | 0.099701 | 1 | native majority | 54.984 |
| pyperformance.xml_etree/xml_etree_process | lucifer | ok | 0.24061 | 2.4133 | native majority | 54.984 |

## 5. Time to steady state

The last five values define the final median. The suffix must contain at least five values, all within 5% of that median. A batch reports its first operation. An unsettled row fails this card. Unit-event replays measure Lucifer churn. PyPy loop and bridge counters are in the CSV.

| program | runtime | status | first_steady_op | warmup_s | compile_ms | post_compile_ms | post_rebuilds | post_kills |
|---|---|---|---|---|---|---|---|---|
| awfy.Bounce | cpython | ok | 1 | 0 | — | — | — | — |
| awfy.Bounce | go | ok | 2 | 0.037459 | — | — | — | — |
| awfy.Bounce | lucifer | unsettled | — | — | 11.154 | — | — | — |
| awfy.Bounce | pypy | unsettled | — | — | 14.793 | — | — | — |
| awfy.CD | cpython | ok | 1 | 0 | — | — | — | — |
| awfy.CD | go | unsettled | — | — | — | — | — | — |
| awfy.CD | lucifer | ok | 5 | 4.6074 | 161.58 | 59.772 | 17 | 14 |
| awfy.CD | pypy | unsettled | — | — | 703.82 | — | — | — |
| awfy.DeltaBlue | cpython | ok | 2 | 0.78388 | — | — | — | — |
| awfy.DeltaBlue | go | unsettled | — | — | — | — | — | — |
| awfy.DeltaBlue | lucifer | unsettled | — | — | 197.58 | — | — | — |
| awfy.DeltaBlue | pypy | unsettled | — | — | 239.55 | — | — | — |
| awfy.Havlak | cpython | ok | 1 | 0 | — | — | — | — |
| awfy.Havlak | go | unsettled | — | — | — | — | — | — |
| awfy.Havlak | lucifer | ok | 2 | 3.0333 | 445.44 | 190.8 | 37 | 37 |
| awfy.Havlak | pypy | unsettled | — | — | 208.81 | — | — | — |
| awfy.Json | cpython | ok | 1 | 0 | — | — | — | — |
| awfy.Json | go | unsettled | — | — | — | — | — | — |
| awfy.Json | lucifer | unsettled | — | — | 217.55 | — | — | — |
| awfy.Json | pypy | unsettled | — | — | 461.47 | — | — | — |
| awfy.List | cpython | ok | 1 | 0 | — | — | — | — |
| awfy.List | go | ok | 4 | 0.083252 | — | — | — | — |
| awfy.List | lucifer | unsettled | — | — | 5.805 | — | — | — |
| awfy.List | pypy | ok | 5 | 1.38 | 27.277 | 0 | — | — |
| awfy.Mandelbrot | cpython | ok | 1 | 0 | — | — | — | — |
| awfy.Mandelbrot | go | ok | 1 | 0 | — | — | — | — |
| awfy.Mandelbrot | lucifer | ok | 3 | 0.15035 | 7.692 | 0 | 0 | 0 |
| awfy.Mandelbrot | pypy | ok | 2 | 0.065579 | 7.5319 | 0 | — | — |
| awfy.NBody | cpython | ok | 1 | 0 | — | — | — | — |
| awfy.NBody | go | unsettled | — | — | — | — | — | — |
| awfy.NBody | lucifer | unsettled | — | — | 16.694 | — | — | — |
| awfy.NBody | pypy | ok | 2 | 0.10173 | 9.2213 | 0 | — | — |
| awfy.Permute | cpython | ok | 1 | 0 | — | — | — | — |
| awfy.Permute | go | unsettled | — | — | — | — | — | — |
| awfy.Permute | lucifer | unsettled | — | — | 16.575 | — | — | — |
| awfy.Permute | pypy | unsettled | — | — | 45.427 | — | — | — |
| awfy.Queens | cpython | unsettled | — | — | — | — | — | — |
| awfy.Queens | go | unsettled | — | — | — | — | — | — |
| awfy.Queens | lucifer | unsettled | — | — | 22.256 | — | — | — |
| awfy.Queens | pypy | unsettled | — | — | 14.88 | — | — | — |
| awfy.Richards | cpython | ok | 1 | 0 | — | — | — | — |
| awfy.Richards | go | unsettled | — | — | — | — | — | — |
| awfy.Richards | lucifer | unsettled | — | — | 20.079 | — | — | — |
| awfy.Richards | pypy | ok | 6 | 1.2037 | 70.634 | 1.8649 | — | — |
| awfy.Sieve | cpython | ok | 1 | 0 | — | — | — | — |
| awfy.Sieve | go | unsettled | — | — | — | — | — | — |
| awfy.Sieve | lucifer | unsettled | — | — | 4.437 | — | — | — |
| awfy.Sieve | pypy | unsettled | — | — | 5.6558 | — | — | — |
| awfy.Storage | cpython | unsettled | — | — | — | — | — | — |
| awfy.Storage | go | ok | 1 | 0 | — | — | — | — |
| awfy.Storage | lucifer | ok | 1 | 0 | 9.809 | 8.449 | 8 | 7 |
| awfy.Storage | pypy | unsettled | — | — | 20.838 | — | — | — |
| awfy.Towers | cpython | ok | 1 | 0 | — | — | — | — |
| awfy.Towers | go | unsettled | — | — | — | — | — | — |
| awfy.Towers | lucifer | unsettled | — | — | 7.546 | — | — | — |
| awfy.Towers | pypy | ok | 3 | 0.50107 | 9.5975 | 0 | — | — |
| flask.1 | cpython | ok | 1 | 0.19909 | — | — | — | — |
| flask.1 | lucifer | unsettled | — | — | 4651.4 | — | — | — |
| flask.1 | pypy | unsettled | — | — | 1101.4 | — | — | — |
| flask.8 | cpython | insufficient disjoint CPUs | — | — | — | — | — | — |
| flask.8 | lucifer | insufficient disjoint CPUs | — | — | — | — | — | — |
| flask.8 | pypy | insufficient disjoint CPUs | — | — | — | — | — | — |
| flask.8.shared | cpython | ok | 87644 | 10.738 | — | — | — | — |
| flask.8.shared | lucifer | ok | 29313 | 10.951 | 6122.6 | 827.12 | 158 | 153 |
| flask.8.shared | pypy | unsettled | — | — | 1928.7 | — | — | — |
| pyperformance.argparse | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.argparse | lucifer | unsettled | — | — | 505.5 | — | — | — |
| pyperformance.argparse | pypy | unsettled | — | — | 373.42 | — | — | — |
| pyperformance.argparse_subparsers | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.argparse_subparsers | lucifer | unsettled | — | — | 243.68 | — | — | — |
| pyperformance.argparse_subparsers | pypy | unsettled | — | — | 68.402 | — | — | — |
| pyperformance.async_generators | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.async_generators | lucifer | ok | 1 | 0 | 85.961 | 37.72 | 50 | 54 |
| pyperformance.async_generators | pypy | unsettled | — | — | 47.454 | — | — | — |
| pyperformance.base64/ascii85_large | cpython | ok | 1 | 0 | — | — | — | — |
| pyperformance.base64/ascii85_large | lucifer | ok | 1 | 0 | 394.89 | 3.452 | 1 | 1 |
| pyperformance.base64/ascii85_large | pypy | unsettled | — | — | 74.887 | — | — | — |
| pyperformance.base64/ascii85_small | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.base64/ascii85_small | lucifer | unsettled | — | — | 386.13 | — | — | — |
| pyperformance.base64/ascii85_small | pypy | unsettled | — | — | 74.887 | — | — | — |
| pyperformance.base64/base16_large | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.base64/base16_large | lucifer | ok | 3 | 0.07882 | 296.28 | 0 | 0 | 0 |
| pyperformance.base64/base16_large | pypy | unsettled | — | — | 46.329 | — | — | — |
| pyperformance.base64/base16_small | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.base64/base16_small | lucifer | unsettled | — | — | 289.03 | — | — | — |
| pyperformance.base64/base16_small | pypy | unsettled | — | — | 46.103 | — | — | — |
| pyperformance.base64/base32_large | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.base64/base32_large | lucifer | unsettled | — | — | 256.72 | — | — | — |
| pyperformance.base64/base32_large | pypy | unsettled | — | — | 42.772 | — | — | — |
| pyperformance.base64/base32_small | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.base64/base32_small | lucifer | unsettled | — | — | 256.05 | — | — | — |
| pyperformance.base64/base32_small | pypy | unsettled | — | — | 40.031 | — | — | — |
| pyperformance.base64/base64_large | cpython | ok | 2 | 0.0072894 | — | — | — | — |
| pyperformance.base64/base64_large | lucifer | unsettled | — | — | 201.87 | — | — | — |
| pyperformance.base64/base64_large | pypy | unsettled | — | — | 25.35 | — | — | — |
| pyperformance.base64/base64_small | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.base64/base64_small | lucifer | unsettled | — | — | 77.667 | — | — | — |
| pyperformance.base64/base64_small | pypy | unsettled | — | — | 24.748 | — | — | — |
| pyperformance.base64/base85_large | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.base64/base85_large | lucifer | unsettled | — | — | 424.62 | — | — | — |
| pyperformance.base64/base85_large | pypy | unsettled | — | — | 83.966 | — | — | — |
| pyperformance.base64/base85_small | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.base64/base85_small | lucifer | unsettled | — | — | 424.08 | — | — | — |
| pyperformance.base64/base85_small | pypy | unsettled | — | — | 83.966 | — | — | — |
| pyperformance.base64/urlsafe_base64_small | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.base64/urlsafe_base64_small | lucifer | unsettled | — | — | 226.66 | — | — | — |
| pyperformance.base64/urlsafe_base64_small | pypy | unsettled | — | — | 28.121 | — | — | — |
| pyperformance.bpe_tokeniser | cpython | ok | 1 | 0 | — | — | — | — |
| pyperformance.bpe_tokeniser | lucifer | unsettled | — | — | 304.16 | — | — | — |
| pyperformance.bpe_tokeniser | pypy | ok | 2 | 2.0567 | 73.526 | 6.2785 | — | — |
| pyperformance.chameleon | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.chameleon | lucifer | unsettled | — | — | 409.7 | — | — | — |
| pyperformance.chameleon | pypy | unsettled | — | — | 113.61 | — | — | — |
| pyperformance.chaos | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.chaos | lucifer | unsettled | — | — | 114.84 | — | — | — |
| pyperformance.chaos | pypy | unsettled | — | — | 75.633 | — | — | — |
| pyperformance.comprehensions | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.comprehensions | lucifer | ok | 2843 | 0.17925 | 107.1 | 0.184 | 0 | 0 |
| pyperformance.comprehensions | pypy | unsettled | — | — | 34.347 | — | — | — |
| pyperformance.coroutines | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.coroutines | lucifer | unsettled | — | — | 49.6 | — | — | — |
| pyperformance.coroutines | pypy | unsettled | — | — | 38.599 | — | — | — |
| pyperformance.coverage | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.coverage | lucifer | ok | 1 | 0 | 246.35 | 163.33 | 443 | 536 |
| pyperformance.coverage | pypy | unsettled | — | — | 145.82 | — | — | — |
| pyperformance.crypto_pyaes | cpython | ok | 4 | 0.35116 | — | — | — | — |
| pyperformance.crypto_pyaes | lucifer | unsettled | — | — | 107.78 | — | — | — |
| pyperformance.crypto_pyaes | pypy | unsettled | — | — | 24.771 | — | — | — |
| pyperformance.deepcopy/deepcopy | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.deepcopy/deepcopy | lucifer | unsettled | — | — | 139.7 | — | — | — |
| pyperformance.deepcopy/deepcopy | pypy | ok | 209 | 0.096948 | 63.263 | 0.96703 | — | — |
| pyperformance.deepcopy/deepcopy_memo | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.deepcopy/deepcopy_memo | lucifer | unsettled | — | — | 295.05 | — | — | — |
| pyperformance.deepcopy/deepcopy_memo | pypy | unsettled | — | — | 124.88 | — | — | — |
| pyperformance.deepcopy/deepcopy_reduce | cpython | ok | 625 | 0.0034106 | — | — | — | — |
| pyperformance.deepcopy/deepcopy_reduce | lucifer | unsettled | — | — | 271.42 | — | — | — |
| pyperformance.deepcopy/deepcopy_reduce | pypy | unsettled | — | — | 104.28 | — | — | — |
| pyperformance.deltablue | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.deltablue | lucifer | unsettled | — | — | 233.41 | — | — | — |
| pyperformance.deltablue | pypy | unsettled | — | — | 175.54 | — | — | — |
| pyperformance.django_template | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.django_template | lucifer | unsettled | — | — | 225.4 | — | — | — |
| pyperformance.django_template | pypy | unsettled | — | — | 55.58 | — | — | — |
| pyperformance.docutils | cpython | ok | 1 | 0 | — | — | — | — |
| pyperformance.docutils | lucifer | ok | 3 | 20.74 | 5058.6 | 2084.4 | 628 | 611 |
| pyperformance.docutils | pypy | unsettled | — | — | 5034.1 | — | — | — |
| pyperformance.fannkuch | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.fannkuch | lucifer | ok | 1 | 0 | 52.742 | 4.371 | 10 | 9 |
| pyperformance.fannkuch | pypy | unsettled | — | — | 19.529 | — | — | — |
| pyperformance.fastapi | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.fastapi | lucifer | unsettled | — | — | 2067.1 | — | — | — |
| pyperformance.fastapi | pypy | pyperf worker exit 1 | — | — | — | — | — | — |
| pyperformance.generators | cpython | ok | 10 | 0.56838 | — | — | — | — |
| pyperformance.generators | lucifer | unsettled | — | — | 55.845 | — | — | — |
| pyperformance.generators | pypy | unsettled | — | — | 152.29 | — | — | — |
| pyperformance.genshi/genshi_text | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.genshi/genshi_text | lucifer | unsettled | — | — | 158.05 | — | — | — |
| pyperformance.genshi/genshi_text | pypy | unsettled | — | — | 186.97 | — | — | — |
| pyperformance.genshi/genshi_xml | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.genshi/genshi_xml | lucifer | unsettled | — | — | 424.31 | — | — | — |
| pyperformance.genshi/genshi_xml | pypy | unsettled | — | — | 309.35 | — | — | — |
| pyperformance.go | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.go | lucifer | unsettled | — | — | 163.31 | — | — | — |
| pyperformance.go | pypy | unsettled | — | — | 652.87 | — | — | — |
| pyperformance.hexiom | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.hexiom | lucifer | unsettled | — | — | 243.83 | — | — | — |
| pyperformance.hexiom | pypy | unsettled | — | — | 135.36 | — | — | — |
| pyperformance.html5lib | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.html5lib | lucifer | unsettled | — | — | 339.48 | — | — | — |
| pyperformance.html5lib | pypy | unsettled | — | — | 218.73 | — | — | — |
| pyperformance.mako | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.mako | lucifer | unsettled | — | — | 177.28 | — | — | — |
| pyperformance.mako | pypy | unsettled | — | — | 50.454 | — | — | — |
| pyperformance.mdp | cpython | ok | 1 | 0 | — | — | — | — |
| pyperformance.mdp | lucifer | ok | 2 | 7.611 | 537.58 | 184.09 | 45 | 44 |
| pyperformance.mdp | pypy | ok | 2 | 6.1075 | 313.61 | 22.169 | — | — |
| pyperformance.meteor_contest | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.meteor_contest | lucifer | unsettled | — | — | 87.912 | — | — | — |
| pyperformance.meteor_contest | pypy | ok | 2 | 0.067987 | 30.384 | 3.9251 | — | — |
| pyperformance.nbody | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.nbody | lucifer | ok | 3 | 0.18158 | 54.338 | 0 | 0 | 0 |
| pyperformance.nbody | pypy | unsettled | — | — | 13.46 | — | — | — |
| pyperformance.networkx | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.networkx | lucifer | ok | 4 | 2.2437 | 201.93 | 0 | 0 | 0 |
| pyperformance.networkx | pypy | unsettled | — | — | 70.232 | — | — | — |
| pyperformance.networkx_connected_components | cpython | ok | 1 | 0 | — | — | — | — |
| pyperformance.networkx_connected_components | lucifer | unsettled | — | — | 197.91 | — | — | — |
| pyperformance.networkx_connected_components | pypy | unsettled | — | — | 60.233 | — | — | — |
| pyperformance.networkx_k_core | cpython | ok | 1 | 0 | — | — | — | — |
| pyperformance.networkx_k_core | lucifer | ok | 2 | 4.4664 | 380.21 | 98.559 | 57 | 50 |
| pyperformance.networkx_k_core | pypy | unsettled | — | — | 174 | — | — | — |
| pyperformance.nqueens | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.nqueens | lucifer | ok | 4 | 0.40831 | 64.843 | 0 | 0 | 0 |
| pyperformance.nqueens | pypy | unsettled | — | — | 30.8 | — | — | — |
| pyperformance.pidigits | cpython | ok | 4 | 0.59137 | — | — | — | — |
| pyperformance.pidigits | lucifer | unsettled | — | — | 84.35 | — | — | — |
| pyperformance.pidigits | pypy | ok | 2 | 0.15406 | 22.822 | 2.703 | — | — |
| pyperformance.pprint/pprint_pformat | cpython | ok | 1 | 0 | — | — | — | — |
| pyperformance.pprint/pprint_pformat | lucifer | ok | 2 | 5.2798 | 799.94 | 220.4 | 35 | 32 |
| pyperformance.pprint/pprint_pformat | pypy | unsettled | — | — | 41.974 | — | — | — |
| pyperformance.pprint/pprint_safe_repr | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.pprint/pprint_safe_repr | lucifer | ok | 2 | 2.6715 | 313.48 | 174.23 | 26 | 27 |
| pyperformance.pprint/pprint_safe_repr | pypy | unsettled | — | — | 35.706 | — | — | — |
| pyperformance.pyflate | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.pyflate | lucifer | ok | 4 | 3.1924 | 258.84 | 31.607 | 19 | 18 |
| pyperformance.pyflate | pypy | ok | 4 | 0.5497 | 94.712 | 11.671 | — | — |
| pyperformance.raytrace | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.raytrace | lucifer | unsettled | — | — | 244.38 | — | — | — |
| pyperformance.raytrace | pypy | unsettled | — | — | 141.51 | — | — | — |
| pyperformance.regex_compile | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.regex_compile | lucifer | unsettled | — | — | 831.29 | — | — | — |
| pyperformance.regex_compile | pypy | pyperf stderr differs from oracle | — | — | — | — | — | — |
| pyperformance.richards | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.richards | lucifer | ok | 7 | 0.1499 | 96.552 | 0 | 0 | 1 |
| pyperformance.richards | pypy | unsettled | — | — | 48.429 | — | — | — |
| pyperformance.richards_super | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.richards_super | lucifer | unsettled | — | — | 102.21 | — | — | — |
| pyperformance.richards_super | pypy | unsettled | — | — | 53.249 | — | — | — |
| pyperformance.spectral_norm | cpython | ok | 3 | 0.28594 | — | — | — | — |
| pyperformance.spectral_norm | lucifer | ok | 2 | 0.05939 | 57.301 | 0.545 | 0 | 0 |
| pyperformance.spectral_norm | pypy | unsettled | — | — | 19.968 | — | — | — |
| pyperformance.sphinx | cpython | ok | 4 | 4.3853 | — | — | — | — |
| pyperformance.sphinx | lucifer | ok | 3 | 12.105 | 3535.9 | 1419.6 | 675 | 672 |
| pyperformance.sphinx | pypy | unsettled | — | — | 5099.3 | — | — | — |
| pyperformance.sqlglot_v2 | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.sqlglot_v2 | lucifer | unsettled | — | — | 595.89 | — | — | — |
| pyperformance.sqlglot_v2 | pypy | unsettled | — | — | 538.38 | — | — | — |
| pyperformance.sqlglot_v2_optimize | cpython | ok | 7 | 0.47849 | — | — | — | — |
| pyperformance.sqlglot_v2_optimize | lucifer | unsettled | — | — | 729.46 | — | — | — |
| pyperformance.sqlglot_v2_optimize | pypy | unsettled | — | — | 807.07 | — | — | — |
| pyperformance.sqlglot_v2_parse | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.sqlglot_v2_parse | lucifer | unsettled | — | — | 271.98 | — | — | — |
| pyperformance.sqlglot_v2_parse | pypy | unsettled | — | — | 328.6 | — | — | — |
| pyperformance.sqlglot_v2_transpile | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.sqlglot_v2_transpile | lucifer | unsettled | — | — | 337.69 | — | — | — |
| pyperformance.sqlglot_v2_transpile | pypy | unsettled | — | — | 405.77 | — | — | — |
| pyperformance.tomli_loads | cpython | ok | 1 | 0 | — | — | — | — |
| pyperformance.tomli_loads | lucifer | ok | 2 | 3.0648 | 335.81 | 136.46 | 19 | 20 |
| pyperformance.tomli_loads | pypy | ok | 2 | 1.6823 | 208.27 | 0 | — | — |
| pyperformance.tornado_http | cpython | ok | 1 | 0 | — | — | — | — |
| pyperformance.tornado_http | lucifer | unsettled | — | — | 841.32 | — | — | — |
| pyperformance.tornado_http | pypy | unsettled | — | — | 705.55 | — | — | — |
| pyperformance.unpack_sequence | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.unpack_sequence | lucifer | unsettled | — | — | 188.1 | — | — | — |
| pyperformance.unpack_sequence | pypy | unsettled | — | — | 43.638 | — | — | — |

## Paired repeatability

Low and high are the observed Lucifer/stock ratios in five interleaved pairs. Spread is 100 × (high/low − 1). A final_s range is diagnostic.

| card | program | metric | pairs | low | high | spread_pct |
|---|---|---|---|---|---|---|
| 1 | awfy.Bounce | seconds | 1 | 0.077377 | 0.077377 | 0 |
| 1 | awfy.Bounce | final_s | 5 | 0.075646 | 0.088834 | 17.435 |
| 1 | pyperformance.argparse_subparsers | final_s | 5 | 1.5306 | 1.7922 | 17.086 |
| 1 | pyperformance.tomli_loads | seconds | 2 | 0.94359 | 0.95323 | 1.022 |
| 1 | pyperformance.tomli_loads | final_s | 5 | 0.92294 | 0.97599 | 5.747 |
| 2 | pyperformance.django_template | final_s | 5 | 1.2247 | 1.4639 | 19.531 |
| 2 | pyperformance.django_template | cycles_per_unit | 5 | 1.1974 | 1.2688 | 5.964 |
| 3 | cli.markdown | seconds | 5 | 4.3193 | 4.5654 | 5.6979 |
| 3 | startup.python_startup | seconds | 5 | 2.068 | 2.2223 | 7.4606 |
| 4 | boundary.cmath.isclose | seconds | 5 | 1.6534 | 2.0418 | 23.488 |
| 4 | boundary.cmath.isfinite | seconds | 5 | 2.4424 | 2.8967 | 18.601 |
| 4 | pyperformance.asyncio_tcp_ssl | seconds | 5 | 6.0616 | 6.1736 | 1.848 |
| 4 | pyperformance.asyncio_tcp_ssl | final_s | 5 | 6.0616 | 6.1736 | 1.848 |
| 5 | awfy.Bounce | first_steady_op | 1 | 6 | 6 | 0 |
| 5 | pyperformance.tomli_loads | first_steady_op | 2 | 1.5 | 2 | 33.333 |

## Gates

These counts and gate outcomes come from the [correctness run on 26 November](../status-2026-11-26/summary.md). The scorecard timings above were measured separately on the same date.

| program | status | score | counts | date | detail |
|---|---|---|---|---|---|
| check-fast-float | pass |  |  | 2026-11-26 |  |
| check-blocking | pass |  |  | 2026-11-26 |  |
| check-globals | fail |  |  | 2026-11-26 |  |
| check-capi | pass |  |  | 2026-11-26 |  |
| check-kernel | pass |  |  | 2026-11-26 | 971/971 checks pass (plain) |
| check-kernel-jit | fail |  |  | 2026-11-26 | FAIL qsbr_retire_finalizer; 970/971 checks pass (jit) |
| check-embed | fail |  |  | 2026-11-26 |  |
| check-jit | pass |  |  | 2026-11-26 | 0 failing; PASS difftest always; PASS difftest deopt |
| capi | measured | 70.8% | 1,153 of 1,628 | 2026-11-26 | Implemented functions in the recorded C-API inventory. |
| numpy | measured | 97.1% | 50,745 of 52,273 collected | 2026-11-15 | Recorded suite run. |
| libtest | measured | 79.8% | 40,528 of CPython's 50,816 | 2026-11-21 | Recorded suite run. |
| kernel | measured | 99.9% | 970 of 971, 2 modes recorded | 2026-11-26 | Plain passed 971/971. JIT passed 970/971 and check-kernel-jit stopped before the forced-deoptimization lane. |
| differential | measured | 100.0% | 193 of 193, 2 modes | 2026-11-26 | Always and deoptimization lanes each passed all 193. |

## Problems

- check-globals: fail
- check-kernel-jit: fail FAIL qsbr_retire_finalizer; 970/971 checks pass (jit)
- check-embed: fail
- Card 1, cpython, unsettled: awfy.Queens, awfy.Storage, pyperformance.argparse, pyperformance.argparse_subparsers, pyperformance.async_generators, pyperformance.base64/ascii85_small, pyperformance.base64/base16_large, pyperformance.base64/base16_small, pyperformance.base64/base32_large, pyperformance.base64/base32_small, pyperformance.base64/base64_small, pyperformance.base64/base85_large, pyperformance.base64/base85_small, pyperformance.base64/urlsafe_base64_small, pyperformance.chaos, pyperformance.comprehensions, pyperformance.coroutines, pyperformance.coverage, pyperformance.deepcopy/deepcopy, pyperformance.deepcopy/deepcopy_memo, pyperformance.deltablue, pyperformance.fannkuch, pyperformance.go, pyperformance.hexiom, pyperformance.meteor_contest, pyperformance.nbody, pyperformance.networkx, pyperformance.nqueens, pyperformance.pprint/pprint_safe_repr, pyperformance.pyflate, pyperformance.raytrace, pyperformance.regex_compile, pyperformance.richards, pyperformance.richards_super, pyperformance.sqlglot_v2, pyperformance.sqlglot_v2_parse, pyperformance.sqlglot_v2_transpile, pyperformance.unpack_sequence
- Card 1, go, unsettled: awfy.CD, awfy.DeltaBlue, awfy.Havlak, awfy.Json, awfy.NBody, awfy.Permute, awfy.Queens, awfy.Richards, awfy.Sieve, awfy.Towers
- Card 1, lucifer, unsettled: awfy.Bounce, awfy.DeltaBlue, awfy.Json, awfy.List, awfy.NBody, awfy.Permute, awfy.Queens, awfy.Richards, awfy.Sieve, awfy.Towers, pyperformance.argparse, pyperformance.argparse_subparsers, pyperformance.base64/ascii85_small, pyperformance.base64/base16_small, pyperformance.base64/base32_large, pyperformance.base64/base32_small, pyperformance.base64/base64_large, pyperformance.base64/base64_small, pyperformance.base64/base85_large, pyperformance.base64/base85_small, pyperformance.base64/urlsafe_base64_small, pyperformance.bpe_tokeniser, pyperformance.chaos, pyperformance.coroutines, pyperformance.crypto_pyaes, pyperformance.deepcopy/deepcopy, pyperformance.deepcopy/deepcopy_memo, pyperformance.deepcopy/deepcopy_reduce, pyperformance.deltablue, pyperformance.generators, pyperformance.go, pyperformance.hexiom, pyperformance.meteor_contest, pyperformance.networkx_connected_components, pyperformance.pidigits, pyperformance.raytrace, pyperformance.regex_compile, pyperformance.richards_super, pyperformance.sqlglot_v2, pyperformance.sqlglot_v2_optimize, pyperformance.sqlglot_v2_parse, pyperformance.sqlglot_v2_transpile, pyperformance.unpack_sequence
- Card 1, pypy, pyperf stderr differs from oracle: pyperformance.regex_compile
- Card 1, pypy, unsettled: awfy.Bounce, awfy.CD, awfy.DeltaBlue, awfy.Havlak, awfy.Json, awfy.Permute, awfy.Queens, awfy.Sieve, awfy.Storage, pyperformance.argparse, pyperformance.argparse_subparsers, pyperformance.async_generators, pyperformance.base64/ascii85_large, pyperformance.base64/ascii85_small, pyperformance.base64/base16_large, pyperformance.base64/base16_small, pyperformance.base64/base32_large, pyperformance.base64/base32_small, pyperformance.base64/base64_large, pyperformance.base64/base64_small, pyperformance.base64/base85_large, pyperformance.base64/base85_small, pyperformance.base64/urlsafe_base64_small, pyperformance.chaos, pyperformance.comprehensions, pyperformance.coroutines, pyperformance.coverage, pyperformance.crypto_pyaes, pyperformance.deepcopy/deepcopy_memo, pyperformance.deepcopy/deepcopy_reduce, pyperformance.deltablue, pyperformance.fannkuch, pyperformance.generators, pyperformance.go, pyperformance.hexiom, pyperformance.nbody, pyperformance.networkx, pyperformance.networkx_connected_components, pyperformance.networkx_k_core, pyperformance.nqueens, pyperformance.pprint/pprint_pformat, pyperformance.pprint/pprint_safe_repr, pyperformance.raytrace, pyperformance.richards, pyperformance.richards_super, pyperformance.spectral_norm, pyperformance.sqlglot_v2, pyperformance.sqlglot_v2_optimize, pyperformance.sqlglot_v2_parse, pyperformance.sqlglot_v2_transpile, pyperformance.unpack_sequence
- Card 2, cpython, insufficient disjoint CPUs: flask.1.profile, flask.8, flask.8.profile
- Card 2, cpython, unsettled: pyperformance.chameleon, pyperformance.django_template, pyperformance.fastapi, pyperformance.genshi/genshi_text, pyperformance.genshi/genshi_xml, pyperformance.html5lib, pyperformance.mako
- Card 2, lucifer, insufficient disjoint CPUs: flask.1.profile, flask.8, flask.8.profile
- Card 2, lucifer, unsettled: pyperformance.chameleon, pyperformance.django_template, pyperformance.fastapi, pyperformance.genshi/genshi_text, pyperformance.genshi/genshi_xml, pyperformance.html5lib, pyperformance.mako, pyperformance.tornado_http
- Card 2, pypy, insufficient disjoint CPUs: flask.1.profile, flask.8, flask.8.profile
- Card 2, pypy, pyperf worker exit 1: pyperformance.fastapi
- Card 2, pypy, unsettled: pyperformance.chameleon, pyperformance.django_template, pyperformance.docutils, pyperformance.genshi/genshi_text, pyperformance.genshi/genshi_xml, pyperformance.html5lib, pyperformance.mako, pyperformance.sphinx, pyperformance.tornado_http
- Card 4, cpython, excluded by corpus lock or missing oracle check: pyperformance.dask
- Card 4, lucifer, excluded by corpus lock or missing oracle check: pyperformance.dask
- Card 4, pypy, excluded by corpus lock or missing oracle check: pyperformance.dask
- Card 4, pypy, pyperf benchmark names differ from oracle: pyperformance.xml_etree
- Card 4, pypy, pyperf worker exit 1: pyperformance.sqlalchemy_declarative
- Card 5, cpython, insufficient disjoint CPUs: flask.8
- Card 5, cpython, unsettled: awfy.Queens, awfy.Storage, pyperformance.argparse, pyperformance.argparse_subparsers, pyperformance.async_generators, pyperformance.base64/ascii85_small, pyperformance.base64/base16_large, pyperformance.base64/base16_small, pyperformance.base64/base32_large, pyperformance.base64/base32_small, pyperformance.base64/base64_small, pyperformance.base64/base85_large, pyperformance.base64/base85_small, pyperformance.base64/urlsafe_base64_small, pyperformance.chameleon, pyperformance.chaos, pyperformance.comprehensions, pyperformance.coroutines, pyperformance.coverage, pyperformance.deepcopy/deepcopy, pyperformance.deepcopy/deepcopy_memo, pyperformance.deltablue, pyperformance.django_template, pyperformance.fannkuch, pyperformance.fastapi, pyperformance.genshi/genshi_text, pyperformance.genshi/genshi_xml, pyperformance.go, pyperformance.hexiom, pyperformance.html5lib, pyperformance.mako, pyperformance.meteor_contest, pyperformance.nbody, pyperformance.networkx, pyperformance.nqueens, pyperformance.pprint/pprint_safe_repr, pyperformance.pyflate, pyperformance.raytrace, pyperformance.regex_compile, pyperformance.richards, pyperformance.richards_super, pyperformance.sqlglot_v2, pyperformance.sqlglot_v2_parse, pyperformance.sqlglot_v2_transpile, pyperformance.unpack_sequence
- Card 5, go, unsettled: awfy.CD, awfy.DeltaBlue, awfy.Havlak, awfy.Json, awfy.NBody, awfy.Permute, awfy.Queens, awfy.Richards, awfy.Sieve, awfy.Towers
- Card 5, lucifer, insufficient disjoint CPUs: flask.8
- Card 5, lucifer, unsettled: awfy.Bounce, awfy.DeltaBlue, awfy.Json, awfy.List, awfy.NBody, awfy.Permute, awfy.Queens, awfy.Richards, awfy.Sieve, awfy.Towers, flask.1, pyperformance.argparse, pyperformance.argparse_subparsers, pyperformance.base64/ascii85_small, pyperformance.base64/base16_small, pyperformance.base64/base32_large, pyperformance.base64/base32_small, pyperformance.base64/base64_large, pyperformance.base64/base64_small, pyperformance.base64/base85_large, pyperformance.base64/base85_small, pyperformance.base64/urlsafe_base64_small, pyperformance.bpe_tokeniser, pyperformance.chameleon, pyperformance.chaos, pyperformance.coroutines, pyperformance.crypto_pyaes, pyperformance.deepcopy/deepcopy, pyperformance.deepcopy/deepcopy_memo, pyperformance.deepcopy/deepcopy_reduce, pyperformance.deltablue, pyperformance.django_template, pyperformance.fastapi, pyperformance.generators, pyperformance.genshi/genshi_text, pyperformance.genshi/genshi_xml, pyperformance.go, pyperformance.hexiom, pyperformance.html5lib, pyperformance.mako, pyperformance.meteor_contest, pyperformance.networkx_connected_components, pyperformance.pidigits, pyperformance.raytrace, pyperformance.regex_compile, pyperformance.richards_super, pyperformance.sqlglot_v2, pyperformance.sqlglot_v2_optimize, pyperformance.sqlglot_v2_parse, pyperformance.sqlglot_v2_transpile, pyperformance.tornado_http, pyperformance.unpack_sequence
- Card 5, pypy, insufficient disjoint CPUs: flask.8
- Card 5, pypy, pyperf stderr differs from oracle: pyperformance.regex_compile
- Card 5, pypy, pyperf worker exit 1: pyperformance.fastapi
- Card 5, pypy, unsettled: awfy.Bounce, awfy.CD, awfy.DeltaBlue, awfy.Havlak, awfy.Json, awfy.Permute, awfy.Queens, awfy.Sieve, awfy.Storage, flask.1, flask.8.shared, pyperformance.argparse, pyperformance.argparse_subparsers, pyperformance.async_generators, pyperformance.base64/ascii85_large, pyperformance.base64/ascii85_small, pyperformance.base64/base16_large, pyperformance.base64/base16_small, pyperformance.base64/base32_large, pyperformance.base64/base32_small, pyperformance.base64/base64_large, pyperformance.base64/base64_small, pyperformance.base64/base85_large, pyperformance.base64/base85_small, pyperformance.base64/urlsafe_base64_small, pyperformance.chameleon, pyperformance.chaos, pyperformance.comprehensions, pyperformance.coroutines, pyperformance.coverage, pyperformance.crypto_pyaes, pyperformance.deepcopy/deepcopy_memo, pyperformance.deepcopy/deepcopy_reduce, pyperformance.deltablue, pyperformance.django_template, pyperformance.docutils, pyperformance.fannkuch, pyperformance.generators, pyperformance.genshi/genshi_text, pyperformance.genshi/genshi_xml, pyperformance.go, pyperformance.hexiom, pyperformance.html5lib, pyperformance.mako, pyperformance.nbody, pyperformance.networkx, pyperformance.networkx_connected_components, pyperformance.networkx_k_core, pyperformance.nqueens, pyperformance.pprint/pprint_pformat, pyperformance.pprint/pprint_safe_repr, pyperformance.raytrace, pyperformance.richards, pyperformance.richards_super, pyperformance.spectral_norm, pyperformance.sphinx, pyperformance.sqlglot_v2, pyperformance.sqlglot_v2_optimize, pyperformance.sqlglot_v2_parse, pyperformance.sqlglot_v2_transpile, pyperformance.tornado_http, pyperformance.unpack_sequence

The 9 measurement arms took 72.2 minutes including queue waits.
