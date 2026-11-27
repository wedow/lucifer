# Runtime scorecards

Source revision `ee5a90dfee1d83078f83b8110b7dd0a7d4960377`.

## Scorecards

Stock is CPython 3.14t. Ratios compare Lucifer time except for Flask, which compares requests/s on disjoint server and client CPUs. Each paired manifest has one vote. Change compares the same programs with yesterday's ratios; a dash means no comparable measurement.

| Card | Lucifer | Paired / observed | Target | Change since yesterday |
|---|---|---|---|---|
| 1. Pure-Python compute / Held-out real Python programs | Go 10.7×, PyPy 2.67×<br>stock 0.813× | Go 5/14, PyPy 3/14<br>stock 9/36 | AWFY ≤2× Go and ≤1× PyPy time on all 14<br>Warm-suite bar ≤0.35× stock and ≤0.72× PyPy | Go coverage changed, PyPy coverage changed<br>stock coverage changed |
| 2. Flask throughput vs stock | 1 CPU 0.264×, 8 CPUs 0.427× | 1 CPU 1/1, 8 CPUs 1/1 | ≥1.5× stock at both counts, ≥4× scaling, zero errors, p99 ≤stock | 1 CPU -0.871%, 8 CPUs — |
| 3. Short programs vs stock | CLI 3.98×, startup 2.12× | CLI 6/6, startup 3/3 | Each CLI and startup program ≤1× stock time | CLI +2.45%, startup -1.88% |
| 4. Native-heavy programs vs stock | majority 2.41×, mixed 1.97× | majority 21/21, mixed 27/27 | ≥10% gain on a second wheel, no bulk regression; warm-suite bar ≤0.35× stock | majority coverage changed, mixed coverage changed |
| 5. Time to steady state | 23/75 settled | 52 unsettled, 0 unavailable | Track onset and compile cost; PLAN2 sets no numeric settling target | coverage changed |

## 1. Pure Python

Seconds per verified AWFY iteration or pyperf operation in the steady suffix. Go ratios require a matching port. Keep AWFY and the prospective held-outs separate.

| program | runtime | status | group | seconds | over_cpython | over_pypy | over_go | final_s |
|---|---|---|---|---|---|---|---|---|
| awfy.Bounce | cpython | ok | — | 1.923 | 1 | 48.168 | — | 1.9297 |
| awfy.Bounce | go | unsettled | — | — | — | — | — | 0.037138 |
| awfy.Bounce | lucifer | ok | — | 0.15189 | 0.078989 | 3.8047 | — | 0.15189 |
| awfy.Bounce | pypy | ok | — | 0.039922 | 0.020761 | 1 | — | 0.039922 |
| awfy.CD | cpython | ok | — | 3.1203 | 1 | — | — | 3.1154 |
| awfy.CD | go | unsettled | — | — | — | — | — | 0.10157 |
| awfy.CD | lucifer | unsettled | — | — | — | — | — | 1.1008 |
| awfy.CD | pypy | unsettled | — | — | — | — | — | 0.28395 |
| awfy.DeltaBlue | cpython | unsettled | — | — | — | — | — | 0.77288 |
| awfy.DeltaBlue | go | unsettled | — | — | — | — | — | 0.046244 |
| awfy.DeltaBlue | lucifer | unsettled | — | — | — | — | — | 0.606 |
| awfy.DeltaBlue | pypy | unsettled | — | — | — | — | — | 0.08252 |
| awfy.Havlak | cpython | ok | — | 4.4739 | 1 | 7.4871 | 20.58 | 4.484 |
| awfy.Havlak | go | ok | — | 0.21739 | 0.048592 | 0.36381 | 1 | 0.21739 |
| awfy.Havlak | lucifer | ok | — | 2.8343 | 0.63352 | 4.7432 | 13.038 | 2.8258 |
| awfy.Havlak | pypy | ok | — | 0.59755 | 0.13356 | 1 | 2.7487 | 0.59755 |
| awfy.Json | cpython | unsettled | — | — | — | — | — | 1.0566 |
| awfy.Json | go | unsettled | — | — | — | — | — | 0.050517 |
| awfy.Json | lucifer | unsettled | — | — | — | — | — | 0.37878 |
| awfy.Json | pypy | unsettled | — | — | — | — | — | 0.11752 |
| awfy.List | cpython | unsettled | — | — | — | — | — | 1.2605 |
| awfy.List | go | unsettled | — | — | — | — | — | 0.025519 |
| awfy.List | lucifer | ok | — | 0.27627 | — | — | — | 0.27627 |
| awfy.List | pypy | unsettled | — | — | — | — | — | 0.31762 |
| awfy.Mandelbrot | cpython | ok | — | 1.5768 | 1 | 28.892 | 33.516 | 1.5652 |
| awfy.Mandelbrot | go | ok | — | 0.047047 | 0.029837 | 0.86205 | 1 | 0.046533 |
| awfy.Mandelbrot | lucifer | ok | — | 0.057438 | 0.036426 | 1.0524 | 1.2209 | 0.057649 |
| awfy.Mandelbrot | pypy | ok | — | 0.054576 | 0.034611 | 1 | 1.16 | 0.054895 |
| awfy.NBody | cpython | ok | — | 2.2333 | 1 | — | 109.5 | 2.2221 |
| awfy.NBody | go | ok | — | 0.020394 | 0.0091321 | — | 1 | 0.020416 |
| awfy.NBody | lucifer | ok | — | 0.34053 | 0.15248 | — | 16.697 | 0.34053 |
| awfy.NBody | pypy | unsettled | — | — | — | — | — | 0.077015 |
| awfy.Permute | cpython | ok | — | 2.3901 | 1 | — | 93.192 | 2.4288 |
| awfy.Permute | go | ok | — | 0.025646 | 0.010731 | — | 1 | 0.025678 |
| awfy.Permute | lucifer | ok | — | 0.94221 | 0.39422 | — | 36.738 | 0.93559 |
| awfy.Permute | pypy | unsettled | — | — | — | — | — | 0.1916 |
| awfy.Queens | cpython | unsettled | — | — | — | — | — | 1.1524 |
| awfy.Queens | go | unsettled | — | — | — | — | — | 0.017006 |
| awfy.Queens | lucifer | unsettled | — | — | — | — | — | 0.3365 |
| awfy.Queens | pypy | ok | — | 0.099137 | — | 1 | — | 0.099405 |
| awfy.Richards | cpython | ok | — | 5.0052 | 1 | — | 63.453 | 5.0122 |
| awfy.Richards | go | ok | — | 0.07888 | 0.01576 | — | 1 | 0.078867 |
| awfy.Richards | lucifer | ok | — | 1.119 | 0.22357 | — | 14.186 | 1.119 |
| awfy.Richards | pypy | unsettled | — | — | — | — | — | 0.20492 |
| awfy.Sieve | cpython | ok | — | 2.5018 | 1 | 18.838 | — | 2.4961 |
| awfy.Sieve | go | unsettled | — | — | — | — | — | 0.036104 |
| awfy.Sieve | lucifer | unsettled | — | — | — | — | — | 0.29936 |
| awfy.Sieve | pypy | ok | — | 0.13281 | 0.053084 | 1 | — | 0.13281 |
| awfy.Storage | cpython | ok | — | 2.0467 | 1 | — | — | 2.0566 |
| awfy.Storage | go | unsettled | — | — | — | — | — | 0.52041 |
| awfy.Storage | lucifer | unsettled | — | — | — | — | — | 1.4989 |
| awfy.Storage | pypy | unsettled | — | — | — | — | — | 0.30754 |
| awfy.Towers | cpython | ok | — | 1.6525 | 1 | — | — | 1.6728 |
| awfy.Towers | go | unsettled | — | — | — | — | — | 0.020679 |
| awfy.Towers | lucifer | unsettled | — | — | — | — | — | 0.39073 |
| awfy.Towers | pypy | unsettled | — | — | — | — | — | 0.25991 |
| pyperformance.argparse_subparsers | cpython | unsettled | held out | — | — | — | — | 0.015017 |
| pyperformance.argparse_subparsers | lucifer | unsettled | held out | — | — | — | — | 0.024807 |
| pyperformance.argparse_subparsers | pypy | unsettled | held out | — | — | — | — | 0.024143 |
| pyperformance.async_generators | cpython | ok | held out | 0.42168 | 1 | — | — | 0.42168 |
| pyperformance.async_generators | lucifer | unsettled | held out | — | — | — | — | 1.4205 |
| pyperformance.async_generators | pypy | unsettled | held out | — | — | — | — | 0.2663 |
| pyperformance.base64/ascii85_large | cpython | ok | held out | 1.4003 | 1 | — | — | 1.3929 |
| pyperformance.base64/ascii85_large | lucifer | ok | held out | 1.2285 | 0.87727 | — | — | 1.2285 |
| pyperformance.base64/ascii85_large | pypy | unsettled | held out | — | — | — | — | 0.27679 |
| pyperformance.base64/ascii85_small | cpython | ok | held out | 0.026645 | 1 | — | — | 0.026666 |
| pyperformance.base64/ascii85_small | lucifer | unsettled | held out | — | — | — | — | 0.043555 |
| pyperformance.base64/ascii85_small | pypy | unsettled | held out | — | — | — | — | 0.003744 |
| pyperformance.base64/base16_large | cpython | unsettled | held out | — | — | — | — | 0.0071503 |
| pyperformance.base64/base16_large | lucifer | unsettled | held out | — | — | — | — | 0.034184 |
| pyperformance.base64/base16_large | pypy | unsettled | held out | — | — | — | — | 0.034054 |
| pyperformance.base64/base16_small | cpython | unsettled | held out | — | — | — | — | 0.00038777 |
| pyperformance.base64/base16_small | lucifer | unsettled | held out | — | — | — | — | 0.0062562 |
| pyperformance.base64/base16_small | pypy | unsettled | held out | — | — | — | — | 0.0011164 |
| pyperformance.base64/base32_large | cpython | ok | held out | 0.66975 | 1 | — | — | 0.66975 |
| pyperformance.base64/base32_large | lucifer | ok | held out | 0.78025 | 1.165 | — | — | 0.7801 |
| pyperformance.base64/base32_large | pypy | unsettled | held out | — | — | — | — | 0.10182 |
| pyperformance.base64/base32_small | cpython | unsettled | held out | — | — | — | — | 0.012822 |
| pyperformance.base64/base32_small | lucifer | unsettled | held out | — | — | — | — | 0.018064 |
| pyperformance.base64/base32_small | pypy | unsettled | held out | — | — | — | — | 0.0021524 |
| pyperformance.base64/base64_large | cpython | ok | held out | 0.0067414 | 1 | — | — | 0.0067414 |
| pyperformance.base64/base64_large | lucifer | unsettled | held out | — | — | — | — | 0.033156 |
| pyperformance.base64/base64_large | pypy | unsettled | held out | — | — | — | — | 0.016476 |
| pyperformance.base64/base64_small | cpython | ok | held out | 0.00032906 | 1 | — | — | 0.00032906 |
| pyperformance.base64/base64_small | lucifer | unsettled | held out | — | — | — | — | 0.0016576 |
| pyperformance.base64/base64_small | pypy | unsettled | held out | — | — | — | — | 0.00065679 |
| pyperformance.base64/base85_large | cpython | unsettled | held out | — | — | — | — | 0.55507 |
| pyperformance.base64/base85_large | lucifer | unsettled | held out | — | — | — | — | 0.5199 |
| pyperformance.base64/base85_large | pypy | ok | held out | 0.11514 | — | 1 | — | 0.11536 |
| pyperformance.base64/base85_small | cpython | unsettled | held out | — | — | — | — | 0.010484 |
| pyperformance.base64/base85_small | lucifer | unsettled | held out | — | — | — | — | 0.018144 |
| pyperformance.base64/base85_small | pypy | unsettled | held out | — | — | — | — | 0.0016145 |
| pyperformance.base64/urlsafe_base64_small | cpython | unsettled | held out | — | — | — | — | 0.00056836 |
| pyperformance.base64/urlsafe_base64_small | lucifer | unsettled | held out | — | — | — | — | 0.0024817 |
| pyperformance.base64/urlsafe_base64_small | pypy | unsettled | held out | — | — | — | — | 0.00081508 |
| pyperformance.bpe_tokeniser | cpython | ok | held out | 6.1588 | 1 | 3.3225 | — | 6.1512 |
| pyperformance.bpe_tokeniser | lucifer | ok | held out | 4.3943 | 0.7135 | 2.3706 | — | 4.3732 |
| pyperformance.bpe_tokeniser | pypy | ok | held out | 1.8537 | 0.30098 | 1 | — | 1.8316 |
| pyperformance.chaos | cpython | unsettled | held out | — | — | — | — | 0.093482 |
| pyperformance.chaos | lucifer | unsettled | held out | — | — | — | — | 0.036431 |
| pyperformance.chaos | pypy | unsettled | held out | — | — | — | — | 0.0063759 |
| pyperformance.comprehensions | cpython | ok | held out | 2.2646e-05 | 1 | 2.7714 | — | 2.256e-05 |
| pyperformance.comprehensions | lucifer | unsettled | held out | — | — | — | — | 2.9712e-05 |
| pyperformance.comprehensions | pypy | ok | held out | 8.1712e-06 | 0.36083 | 1 | — | 8.1712e-06 |
| pyperformance.coroutines | cpython | unsettled | held out | — | — | — | — | 0.029555 |
| pyperformance.coroutines | lucifer | unsettled | held out | — | — | — | — | 0.04539 |
| pyperformance.coroutines | pypy | ok | held out | 0.015946 | — | 1 | — | 0.015856 |
| pyperformance.crypto_pyaes | cpython | unsettled | held out | — | — | — | — | 0.11659 |
| pyperformance.crypto_pyaes | lucifer | unsettled | held out | — | — | — | — | 0.086671 |
| pyperformance.crypto_pyaes | pypy | unsettled | held out | — | — | — | — | 0.0044797 |
| pyperformance.deepcopy/deepcopy | cpython | unsettled | held out | — | — | — | — | 0.00040372 |
| pyperformance.deepcopy/deepcopy | lucifer | unsettled | held out | — | — | — | — | 0.00051846 |
| pyperformance.deepcopy/deepcopy | pypy | unsettled | held out | — | — | — | — | 9.4704e-05 |
| pyperformance.deepcopy/deepcopy_memo | cpython | ok | held out | 4.0947e-05 | 1 | — | — | 4.0658e-05 |
| pyperformance.deepcopy/deepcopy_memo | lucifer | unsettled | held out | — | — | — | — | 3.8934e-05 |
| pyperformance.deepcopy/deepcopy_memo | pypy | unsettled | held out | — | — | — | — | 9.5145e-06 |
| pyperformance.deepcopy/deepcopy_reduce | cpython | unsettled | held out | — | — | — | — | 3.9176e-06 |
| pyperformance.deepcopy/deepcopy_reduce | lucifer | unsettled | held out | — | — | — | — | 6.3951e-06 |
| pyperformance.deepcopy/deepcopy_reduce | pypy | unsettled | held out | — | — | — | — | 3.5078e-05 |
| pyperformance.deltablue | cpython | unsettled | held out | — | — | — | — | 0.0046907 |
| pyperformance.deltablue | lucifer | unsettled | held out | — | — | — | — | 0.0043611 |
| pyperformance.deltablue | pypy | unsettled | held out | — | — | — | — | 0.00087058 |
| pyperformance.fannkuch | cpython | unsettled | held out | — | — | — | — | 0.58485 |
| pyperformance.fannkuch | lucifer | ok | held out | 0.54066 | — | 6.734 | — | 0.537 |
| pyperformance.fannkuch | pypy | ok | held out | 0.080288 | — | 1 | — | 0.079721 |
| pyperformance.generators | cpython | ok | held out | 0.033775 | 1 | — | — | 0.033775 |
| pyperformance.generators | lucifer | ok | held out | 0.05701 | 1.6879 | — | — | 0.057034 |
| pyperformance.generators | pypy | unsettled | held out | — | — | — | — | 0.023829 |
| pyperformance.go | cpython | unsettled | held out | — | — | — | — | 0.14706 |
| pyperformance.go | lucifer | unsettled | held out | — | — | — | — | 0.1322 |
| pyperformance.go | pypy | unsettled | held out | — | — | — | — | 0.14979 |
| pyperformance.hexiom | cpython | unsettled | held out | — | — | — | — | 0.0078465 |
| pyperformance.hexiom | lucifer | unsettled | held out | — | — | — | — | 0.0054404 |
| pyperformance.hexiom | pypy | unsettled | held out | — | — | — | — | 0.0023391 |
| pyperformance.mdp | cpython | ok | held out | 1.9608 | 1 | 0.35651 | — | 1.9629 |
| pyperformance.mdp | lucifer | ok | held out | 7.1427 | 3.6427 | 1.2987 | — | 7.1305 |
| pyperformance.mdp | pypy | ok | held out | 5.5001 | 2.805 | 1 | — | 5.4957 |
| pyperformance.meteor_contest | cpython | unsettled | held out | — | — | — | — | 0.13321 |
| pyperformance.meteor_contest | lucifer | unsettled | held out | — | — | — | — | 0.42508 |
| pyperformance.meteor_contest | pypy | ok | held out | 0.052799 | — | 1 | — | 0.052683 |
| pyperformance.nbody | cpython | unsettled | held out | — | — | — | — | 0.16317 |
| pyperformance.nbody | lucifer | ok | held out | 0.07038 | — | — | — | 0.070175 |
| pyperformance.nbody | pypy | unsettled | held out | — | — | — | — | 0.010437 |
| pyperformance.networkx | cpython | ok | held out | 0.61256 | 1 | — | — | 0.61289 |
| pyperformance.networkx | lucifer | ok | held out | 0.74663 | 1.2189 | — | — | 0.75111 |
| pyperformance.networkx | pypy | unsettled | held out | — | — | — | — | 0.55635 |
| pyperformance.networkx_connected_components | cpython | ok | held out | 0.57064 | 1 | — | — | 0.5703 |
| pyperformance.networkx_connected_components | lucifer | ok | held out | 0.65651 | 1.1505 | — | — | 0.64914 |
| pyperformance.networkx_connected_components | pypy | unsettled | held out | — | — | — | — | 0.56532 |
| pyperformance.networkx_k_core | cpython | ok | held out | 2.8494 | 1 | — | — | 2.8454 |
| pyperformance.networkx_k_core | lucifer | ok | held out | 4.1469 | 1.4554 | — | — | 4.099 |
| pyperformance.networkx_k_core | pypy | unsettled | held out | — | — | — | — | 2.1857 |
| pyperformance.nqueens | cpython | ok | held out | 0.11074 | 1 | 4.377 | — | 0.11168 |
| pyperformance.nqueens | lucifer | unsettled | held out | — | — | — | — | 0.13751 |
| pyperformance.nqueens | pypy | ok | held out | 0.025301 | 0.22847 | 1 | — | 0.025301 |
| pyperformance.pidigits | cpython | ok | held out | 0.19334 | 1 | — | — | 0.19334 |
| pyperformance.pidigits | lucifer | unsettled | held out | — | — | — | — | 0.24484 |
| pyperformance.pidigits | pypy | unsettled | held out | — | — | — | — | 0.14568 |
| pyperformance.pprint/pprint_pformat | cpython | unsettled | held out | — | — | — | — | 2.2041 |
| pyperformance.pprint/pprint_pformat | lucifer | ok | held out | 4.7653 | — | — | — | 4.7396 |
| pyperformance.pprint/pprint_pformat | pypy | unsettled | held out | — | — | — | — | 0.47723 |
| pyperformance.pprint/pprint_safe_repr | cpython | ok | held out | 1.0993 | 1 | — | — | 1.0993 |
| pyperformance.pprint/pprint_safe_repr | lucifer | unsettled | held out | — | — | — | — | 2.4995 |
| pyperformance.pprint/pprint_safe_repr | pypy | unsettled | held out | — | — | — | — | 0.23441 |
| pyperformance.pyflate | cpython | ok | held out | 0.5759 | 1 | 4.377 | — | 0.57122 |
| pyperformance.pyflate | lucifer | unsettled | held out | — | — | — | — | 0.75494 |
| pyperformance.pyflate | pypy | ok | held out | 0.13158 | 0.22847 | 1 | — | 0.13129 |
| pyperformance.raytrace | cpython | unsettled | held out | — | — | — | — | 0.39982 |
| pyperformance.raytrace | lucifer | unsettled | held out | — | — | — | — | 0.19533 |
| pyperformance.raytrace | pypy | ok | held out | 0.013967 | — | 1 | — | 0.013967 |
| pyperformance.regex_compile | cpython | unsettled | held out | — | — | — | — | 0.19751 |
| pyperformance.regex_compile | lucifer | unsettled | held out | — | — | — | — | 0.40672 |
| pyperformance.regex_compile | pypy | pyperf stderr differs from oracle | held out | — | — | — | — | — |
| pyperformance.richards | cpython | ok | held out | 0.062786 | 1 | 36.481 | — | 0.062786 |
| pyperformance.richards | lucifer | unsettled | held out | — | — | — | — | 0.013724 |
| pyperformance.richards | pypy | ok | held out | 0.001721 | 0.027411 | 1 | — | 0.001721 |
| pyperformance.richards_super | cpython | ok | held out | 0.072833 | 1 | — | — | 0.072833 |
| pyperformance.richards_super | lucifer | ok | held out | 0.013044 | 0.1791 | — | — | 0.013044 |
| pyperformance.richards_super | pypy | unsettled | held out | — | — | — | — | 0.001848 |
| pyperformance.spectral_norm | cpython | ok | held out | 0.14751 | 1 | — | — | 0.14751 |
| pyperformance.spectral_norm | lucifer | ok | held out | 0.014778 | 0.10019 | — | — | 0.014786 |
| pyperformance.spectral_norm | pypy | unsettled | held out | — | — | — | — | 0.0055203 |
| pyperformance.sqlglot_v2 | cpython | unsettled | held out | — | — | — | — | 0.16212 |
| pyperformance.sqlglot_v2 | lucifer | unsettled | held out | — | — | — | — | 0.33947 |
| pyperformance.sqlglot_v2 | pypy | unsettled | held out | — | — | — | — | 0.13052 |
| pyperformance.sqlglot_v2_optimize | cpython | unsettled | held out | — | — | — | — | 0.073612 |
| pyperformance.sqlglot_v2_optimize | lucifer | unsettled | held out | — | — | — | — | 0.21824 |
| pyperformance.sqlglot_v2_optimize | pypy | unsettled | held out | — | — | — | — | 0.14811 |
| pyperformance.sqlglot_v2_parse | cpython | unsettled | held out | — | — | — | — | 0.0017629 |
| pyperformance.sqlglot_v2_parse | lucifer | unsettled | held out | — | — | — | — | 0.0026113 |
| pyperformance.sqlglot_v2_parse | pypy | unsettled | held out | — | — | — | — | 0.0013048 |
| pyperformance.sqlglot_v2_transpile | cpython | unsettled | held out | — | — | — | — | 0.0022267 |
| pyperformance.sqlglot_v2_transpile | lucifer | unsettled | held out | — | — | — | — | 0.0035512 |
| pyperformance.sqlglot_v2_transpile | pypy | unsettled | held out | — | — | — | — | 0.002061 |
| pyperformance.tomli_loads | cpython | ok | held out | 2.5562 | 1 | 2.1634 | — | 2.5223 |
| pyperformance.tomli_loads | lucifer | ok | held out | 2.4747 | 0.96811 | 2.0944 | — | 2.4808 |
| pyperformance.tomli_loads | pypy | ok | held out | 1.1816 | 0.46223 | 1 | — | 1.1816 |
| pyperformance.unpack_sequence | cpython | unsettled | held out | — | — | — | — | 6.5294e-08 |
| pyperformance.unpack_sequence | lucifer | unsettled | held out | — | — | — | — | 6.7893e-08 |
| pyperformance.unpack_sequence | pypy | unsettled | held out | — | — | — | — | 1.3949e-10 |
| pyperformance.xdsl | cpython | unsettled | held out | — | — | — | — | 0.074613 |
| pyperformance.xdsl | lucifer | unsettled | held out | — | — | — | — | 0.14071 |
| pyperformance.xdsl | pypy | unsettled | held out | — | — | — | — | 0.010694 |

## 2. Application paths

Flask reports the median of three fresh servers at each layout. Its profiling row uses a separate warm server. Counts are per completed request or pyperf operation. The six percentages partition leaf cycle samples. Shared-client layouts are diagnostic, and failed requests disqualify a gate.

| program | runtime | status | ops_per_s | over_cpython | final_ops_per_s | rps | rps_min | rps_max | rps_over_cpython | scaling | p99_ms | errors |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| flask.1 | cpython | ok | — | — | — | 2224.8 | 2224.1 | 2250.2 | 1 | 1 | 7092.2 | 0 |
| flask.1 | lucifer | ok | — | — | — | 588 | 587.9 | 598.1 | 0.26429 | 1 | 7964.3 | 0 |
| flask.1 | pypy | ok | — | — | — | 8483.4 | 8077.8 | 8582.5 | 3.8131 | 1 | 155.26 | 0 |
| flask.1.profile | cpython | ok | — | — | — | — | — | — | — | — | — | 0 |
| flask.1.profile | lucifer | ok | — | — | — | — | — | — | — | — | — | 0 |
| flask.1.profile | pypy | ok | — | — | — | — | — | — | — | — | — | 0 |
| flask.8 | cpython | ok | — | — | — | 8100 | 7939.2 | 8542.1 | 1 | 3.6408 | 34.096 | 0 |
| flask.8 | lucifer | ok | — | — | — | 3454.9 | 3438.4 | 3551.7 | 0.42653 | 5.8757 | 50.144 | 0 |
| flask.8 | pypy | ok | — | — | — | 2882.7 | 2802.9 | 2883.7 | 0.35589 | 0.3398 | 73.515 | 0 |
| flask.8.profile | cpython | ok | — | — | — | — | — | — | — | — | — | 0 |
| flask.8.profile | lucifer | ok | — | — | — | — | — | — | — | — | — | 0 |
| flask.8.profile | pypy | hardware counts unavailable | — | — | — | — | — | — | — | — | — | 0 |
| pyperformance.chameleon | cpython | unsettled | — | — | 49.977 | — | — | — | — | — | — | — |
| pyperformance.chameleon | lucifer | unsettled | — | — | 28.18 | — | — | — | — | — | — | — |
| pyperformance.chameleon | pypy | unsettled | — | — | 227.31 | — | — | — | — | — | — | — |
| pyperformance.django_template | cpython | unsettled | — | — | 19.04 | — | — | — | — | — | — | — |
| pyperformance.django_template | lucifer | unsettled | — | — | 13.49 | — | — | — | — | — | — | — |
| pyperformance.django_template | pypy | unsettled | — | — | 134.72 | — | — | — | — | — | — | — |
| pyperformance.docutils | cpython | ok | 0.29072 | 1 | 0.29011 | — | — | — | — | — | — | — |
| pyperformance.docutils | lucifer | ok | 0.12326 | 2.3587 | 0.12378 | — | — | — | — | — | — | — |
| pyperformance.docutils | pypy | unsettled | — | — | 0.27704 | — | — | — | — | — | — | — |
| pyperformance.fastapi | cpython | unsettled | — | — | 2.7916 | — | — | — | — | — | — | — |
| pyperformance.fastapi | lucifer | unsettled | — | — | 1.0611 | — | — | — | — | — | — | — |
| pyperformance.fastapi | pypy | pyperf worker exit 1 | — | — | — | — | — | — | — | — | — | — |
| pyperformance.genshi/genshi_text | cpython | unsettled | — | — | 29.713 | — | — | — | — | — | — | — |
| pyperformance.genshi/genshi_text | lucifer | unsettled | — | — | 14.704 | — | — | — | — | — | — | — |
| pyperformance.genshi/genshi_text | pypy | unsettled | — | — | 29.8 | — | — | — | — | — | — | — |
| pyperformance.genshi/genshi_xml | cpython | unsettled | — | — | 11.901 | — | — | — | — | — | — | — |
| pyperformance.genshi/genshi_xml | lucifer | unsettled | — | — | 3.7627 | — | — | — | — | — | — | — |
| pyperformance.genshi/genshi_xml | pypy | unsettled | — | — | 24.945 | — | — | — | — | — | — | — |
| pyperformance.html5lib | cpython | unsettled | — | — | 12.461 | — | — | — | — | — | — | — |
| pyperformance.html5lib | lucifer | unsettled | — | — | 6.0376 | — | — | — | — | — | — | — |
| pyperformance.html5lib | pypy | unsettled | — | — | 27.02 | — | — | — | — | — | — | — |
| pyperformance.mako | cpython | ok | 56.29 | 1 | 56.295 | — | — | — | — | — | — | — |
| pyperformance.mako | lucifer | unsettled | — | — | 24.901 | — | — | — | — | — | — | — |
| pyperformance.mako | pypy | unsettled | — | — | 113.06 | — | — | — | — | — | — | — |
| pyperformance.sphinx | cpython | unsettled | — | — | 0.67212 | — | — | — | — | — | — | — |
| pyperformance.sphinx | lucifer | ok | 0.26498 | — | 0.26498 | — | — | — | — | — | — | — |
| pyperformance.sphinx | pypy | unsettled | — | — | 0.37467 | — | — | — | — | — | — | — |
| pyperformance.tornado_http | cpython | ok | 5.9675 | 1 | 5.9675 | — | — | — | — | — | — | — |
| pyperformance.tornado_http | lucifer | unsettled | — | — | 1.6002 | — | — | — | — | — | — | — |
| pyperformance.tornado_http | pypy | unsettled | — | — | 2.2765 | — | — | — | — | — | — | — |

### Warm CPU attribution

| program | runtime | profile_status | profile_units | instructions_per_unit | cycles_per_unit | compiled_pct | interpreter_pct | runtime_c_pct | native_pct | kernel_pct | unknown_pct |
|---|---|---|---|---|---|---|---|---|---|---|---|
| flask.1.profile | cpython | ok | 10824 | 1.4847e+06 | 1.936e+06 | 0 | 29.585 | 63.2 | 0.19113 | 7.024 | 0 |
| flask.1.profile | lucifer | ok | 2788 | 4.1733e+06 | 7.5156e+06 | 30.404 | 6.2765 | 55.572 | 0 | 7.7467 | 0 |
| flask.1.profile | pypy | ok | 40723 | 3.7598e+05 | 4.8847e+05 | 0 | 0.56301 | 55.38 | 0 | 20.505 | 23.552 |
| flask.8.profile | cpython | ok | 42006 | 1.5198e+06 | 2.8815e+06 | 0 | 26.809 | 64.597 | 0.35005 | 8.2268 | 0.01743 |
| flask.8.profile | lucifer | ok | 17470 | 3.2626e+06 | 7.2114e+06 | 30.054 | 3.9041 | 53.293 | 0.17042 | 12.528 | 0.050999 |
| flask.8.profile | pypy | hardware counts unavailable | 14348 | 5.8498e+05 | — | 0 | 0.76495 | 60.74 | 0 | 19.99 | 18.505 |
| pyperformance.chameleon | cpython | ok | 42 | 1.7017e+08 | 8.6333e+07 | 0 | 28.407 | 71.013 | 0 | 0.58 | 0 |
| pyperformance.chameleon | lucifer | ok | 24 | 2.6805e+08 | 1.4794e+08 | 42.798 | 0.59061 | 55.43 | 0 | 1.1812 | 0 |
| pyperformance.chameleon | pypy | ok | 120 | 3.135e+07 | 1.8318e+07 | 0 | 0 | 65.698 | 0 | 1.9185 | 32.383 |
| pyperformance.django_template | cpython | ok | 14 | 3.989e+08 | 2.3225e+08 | 0 | 42.071 | 56.635 | 0 | 1.2937 | 0 |
| pyperformance.django_template | lucifer | ok | 12 | 4.4008e+08 | 2.9249e+08 | 28.171 | 2.3915 | 68.241 | 0 | 1.1963 | 0 |
| pyperformance.django_template | pypy | ok | 108 | 5.6284e+07 | 3.032e+07 | 0 | 0.64004 | 53.341 | 0 | 1.2806 | 44.738 |
| pyperformance.docutils | cpython | ok | 1 | 1.9057e+10 | 1.4537e+10 | 0 | 23.861 | 74.259 | 0 | 1.8796 | 0 |
| pyperformance.docutils | lucifer | ok | 1 | 3.7694e+10 | 3.2497e+10 | 33.572 | 1.892 | 63.111 | 0 | 1.4252 | 0 |
| pyperformance.docutils | pypy | ok | 1 | 1.4976e+10 | 1.3985e+10 | 0 | 3.7688 | 74.831 | 0 | 1.5005 | 19.9 |
| pyperformance.fastapi | cpython | ok | 3 | 2.0533e+09 | 1.5405e+09 | 0 | 47.777 | 44.4 | 0.40511 | 6.7182 | 0.69989 |
| pyperformance.fastapi | lucifer | ok | 1 | 3.1004e+09 | 3.8005e+09 | 30.706 | 7.9036 | 55.398 | 0 | 5.6772 | 0.31438 |
| pyperformance.genshi/genshi_text | cpython | ok | 24 | 3.0089e+08 | 1.539e+08 | 0 | 41.978 | 55.742 | 0 | 2.2797 | 0 |
| pyperformance.genshi/genshi_text | lucifer | ok | 10 | 4.3364e+08 | 2.4231e+08 | 30.206 | 3.5626 | 64.502 | 0 | 1.7293 | 0 |
| pyperformance.genshi/genshi_text | pypy | ok | 18 | 2.2389e+08 | 1.534e+08 | 0 | 0 | 81.67 | 0 | 1.5318 | 16.798 |
| pyperformance.genshi/genshi_xml | cpython | ok | 9 | 5.7539e+08 | 3.1572e+08 | 0 | 44.191 | 54.325 | 0 | 1.4849 | 0 |
| pyperformance.genshi/genshi_xml | lucifer | ok | 3 | 1.2367e+09 | 9.4336e+08 | 38.912 | 2.8651 | 56.645 | 0 | 1.5788 | 0 |
| pyperformance.genshi/genshi_xml | pypy | ok | 19 | 2.5918e+08 | 1.6861e+08 | 0 | 0 | 58.078 | 0 | 0 | 41.922 |
| pyperformance.html5lib | cpython | ok | 9 | 4.69e+08 | 3.4096e+08 | 0 | 43.514 | 54.426 | 1.3733 | 0.68644 | 0 |
| pyperformance.html5lib | lucifer | ok | 5 | 8.857e+08 | 7.2013e+08 | 24.993 | 10.631 | 62.616 | 0 | 1.76 | 0 |
| pyperformance.html5lib | pypy | ok | 19 | 1.7437e+08 | 1.3134e+08 | 0 | 0 | 87.228 | 0 | 0 | 12.772 |
| pyperformance.mako | cpython | ok | 48 | 1.5942e+08 | 8.2243e+07 | 0 | 26.295 | 73.172 | 0 | 0.53346 | 0 |
| pyperformance.mako | lucifer | ok | 24 | 3.597e+08 | 1.6376e+08 | 29.837 | 0 | 69.626 | 0 | 0.53654 | 0 |
| pyperformance.mako | pypy | ok | 90 | 6.0856e+07 | 3.5363e+07 | 0 | 0.66079 | 80.042 | 0 | 0.66008 | 18.637 |
| pyperformance.sphinx | cpython | ok | 1 | 8.4259e+09 | 6.1431e+09 | 0 | 26.376 | 69.164 | 2.059 | 2.4017 | 0 |
| pyperformance.sphinx | lucifer | ok | 1 | 1.9883e+10 | 1.6069e+10 | 31.574 | 3.4812 | 63.204 | 0.52351 | 1.2169 | 0 |
| pyperformance.sphinx | pypy | ok | 1 | 1.1548e+10 | 1.0312e+10 | 0 | 3.4558 | 81.715 | 0 | 0.60351 | 14.225 |
| pyperformance.tornado_http | cpython | ok | 5 | 5.2769e+08 | 7.0916e+08 | 0 | 34.855 | 43.545 | 4.4206 | 17.179 | 0 |
| pyperformance.tornado_http | lucifer | ok | 2 | 1.9756e+09 | 2.2432e+09 | 15.641 | 8.9915 | 65.284 | 0 | 10.084 | 0 |
| pyperformance.tornado_http | pypy | ok | 2 | 1.2331e+09 | 1.6604e+09 | 0 | 7.9802 | 74.728 | 0 | 12.207 | 5.0846 |

## 3. Short programs

Whole-process wall time at the corpus's fixed work and output checks. The CLI, startup and additional fresh-process workers have separate denominators.

| program | runtime | status | seconds | over_cpython |
|---|---|---|---|---|
| cli.black | cpython | ok | 0.4662 | 1 |
| cli.black | lucifer | ok | 2.0065 | 4.304 |
| cli.black | pypy | ok | 1.6662 | 3.5739 |
| cli.isort | cpython | ok | 0.13918 | 1 |
| cli.isort | lucifer | ok | 0.47381 | 3.4042 |
| cli.isort | pypy | ok | 0.26522 | 1.9055 |
| cli.markdown | cpython | ok | 0.11636 | 1 |
| cli.markdown | lucifer | ok | 0.50879 | 4.3728 |
| cli.markdown | pypy | ok | 0.20822 | 1.7895 |
| cli.pycodestyle | cpython | ok | 0.81894 | 1 |
| cli.pycodestyle | lucifer | ok | 3.2888 | 4.0159 |
| cli.pycodestyle | pypy | ok | 1.9154 | 2.3389 |
| cli.pyflakes | cpython | ok | 0.71848 | 1 |
| cli.pyflakes | lucifer | ok | 1.5091 | 2.1004 |
| cli.pyflakes | pypy | ok | 1.6837 | 2.3435 |
| cli.pygments | cpython | ok | 0.27264 | 1 |
| cli.pygments | lucifer | ok | 2.0191 | 7.4057 |
| cli.pygments | pypy | ok | 0.92931 | 3.4085 |
| pyperformance.2to3 | cpython | ok | 0.38832 | 1 |
| pyperformance.2to3 | lucifer | ok | 2.0454 | 5.2674 |
| pyperformance.2to3 | pypy | ok | 1.441 | 3.7108 |
| startup.import_stdlib | cpython | ok | 0.10609 | 1 |
| startup.import_stdlib | lucifer | ok | 0.28093 | 2.6482 |
| startup.import_stdlib | pypy | ok | 0.13893 | 1.3096 |
| startup.python_startup | cpython | ok | 0.01554 | 1 |
| startup.python_startup | lucifer | ok | 0.030496 | 1.9623 |
| startup.python_startup | pypy | ok | 0.024532 | 1.5786 |
| startup.python_startup_no_site | cpython | ok | 0.01037 | 1 |
| startup.python_startup_no_site | lucifer | ok | 0.018889 | 1.8215 |
| startup.python_startup_no_site | pypy | ok | 0.022614 | 2.1807 |

## 4. Native and mixed workloads

Median time over the final five values. The stock native share includes callees up to Python re-entry. Score native-majority, mixed and boxed boundary calls separately.

| program | runtime | status | seconds | over_cpython | group | stock_native_pct |
|---|---|---|---|---|---|---|
| boundary.cmath.isclose | cpython | ok | 9.5564e-08 | 1 | — | — |
| boundary.cmath.isclose | lucifer | ok | 1.9082e-07 | 1.9968 | — | — |
| boundary.cmath.isclose | pypy | ok | 5.0566e-08 | 0.52913 | — | — |
| boundary.cmath.isfinite | cpython | ok | 5.3079e-08 | 1 | — | — |
| boundary.cmath.isfinite | lucifer | ok | 1.3949e-07 | 2.6279 | — | — |
| boundary.cmath.isfinite | pypy | ok | 3.1153e-09 | 0.058692 | — | — |
| pyperformance.argparse | cpython | ok | 0.0013358 | 1 | mixed | 7.5372 |
| pyperformance.argparse | lucifer | ok | 0.0034811 | 2.6061 | mixed | 7.5372 |
| pyperformance.argparse | pypy | ok | 0.0014185 | 1.062 | mixed | 7.5372 |
| pyperformance.async_tree | cpython | ok | 0.3452 | 1 | mixed | 18.455 |
| pyperformance.async_tree | lucifer | ok | 0.77436 | 2.2432 | mixed | 18.455 |
| pyperformance.async_tree | pypy | ok | 0.48188 | 1.396 | mixed | 18.455 |
| pyperformance.async_tree_cpu_io_mixed | cpython | ok | 0.67147 | 1 | native majority | 51.163 |
| pyperformance.async_tree_cpu_io_mixed | lucifer | ok | 3.4428 | 5.1273 | native majority | 51.163 |
| pyperformance.async_tree_cpu_io_mixed | pypy | ok | 0.91311 | 1.3599 | native majority | 51.163 |
| pyperformance.async_tree_cpu_io_mixed_tg | cpython | ok | 0.64702 | 1 | native majority | 50.042 |
| pyperformance.async_tree_cpu_io_mixed_tg | lucifer | ok | 3.4171 | 5.2814 | native majority | 50.042 |
| pyperformance.async_tree_cpu_io_mixed_tg | pypy | ok | 0.85522 | 1.3218 | native majority | 50.042 |
| pyperformance.async_tree_eager | cpython | ok | 0.16685 | 1 | mixed | 19.848 |
| pyperformance.async_tree_eager | lucifer | ok | 0.41798 | 2.5051 | mixed | 19.848 |
| pyperformance.async_tree_eager | pypy | ok | 0.52171 | 3.1269 | mixed | 19.848 |
| pyperformance.async_tree_eager_cpu_io_mixed | cpython | ok | 0.54422 | 1 | native majority | 57.524 |
| pyperformance.async_tree_eager_cpu_io_mixed | lucifer | ok | 3.1964 | 5.8733 | native majority | 57.524 |
| pyperformance.async_tree_eager_cpu_io_mixed | pypy | ok | 0.91548 | 1.6822 | native majority | 57.524 |
| pyperformance.async_tree_eager_cpu_io_mixed_tg | cpython | ok | 0.63622 | 1 | native majority | 54.286 |
| pyperformance.async_tree_eager_cpu_io_mixed_tg | lucifer | ok | 3.4046 | 5.3512 | native majority | 54.286 |
| pyperformance.async_tree_eager_cpu_io_mixed_tg | pypy | ok | 0.82808 | 1.3016 | native majority | 54.286 |
| pyperformance.async_tree_eager_io | cpython | ok | 0.7066 | 1 | mixed | 28.869 |
| pyperformance.async_tree_eager_io | lucifer | ok | 1.4368 | 2.0333 | mixed | 28.869 |
| pyperformance.async_tree_eager_io | pypy | ok | 0.67954 | 0.96171 | mixed | 28.869 |
| pyperformance.async_tree_eager_io_tg | cpython | ok | 0.70528 | 1 | mixed | 21.112 |
| pyperformance.async_tree_eager_io_tg | lucifer | ok | 1.6134 | 2.2877 | mixed | 21.112 |
| pyperformance.async_tree_eager_io_tg | pypy | ok | 0.61553 | 0.87275 | mixed | 21.112 |
| pyperformance.async_tree_eager_memoization | cpython | ok | 0.28193 | 1 | mixed | 22.507 |
| pyperformance.async_tree_eager_memoization | lucifer | ok | 1.0453 | 3.7075 | mixed | 22.507 |
| pyperformance.async_tree_eager_memoization | pypy | ok | 0.56357 | 1.999 | mixed | 22.507 |
| pyperformance.async_tree_eager_memoization_tg | cpython | ok | 0.35069 | 1 | mixed | 24.447 |
| pyperformance.async_tree_eager_memoization_tg | lucifer | ok | 1.2618 | 3.598 | mixed | 24.447 |
| pyperformance.async_tree_eager_memoization_tg | pypy | ok | 0.53804 | 1.5343 | mixed | 24.447 |
| pyperformance.async_tree_eager_tg | cpython | ok | 0.26806 | 1 | mixed | 7.3352 |
| pyperformance.async_tree_eager_tg | lucifer | ok | 0.72993 | 2.723 | mixed | 7.3352 |
| pyperformance.async_tree_eager_tg | pypy | ok | 0.43209 | 1.6119 | mixed | 7.3352 |
| pyperformance.async_tree_io | cpython | ok | 0.77751 | 1 | mixed | 27.001 |
| pyperformance.async_tree_io | lucifer | ok | 1.5014 | 1.9311 | mixed | 27.001 |
| pyperformance.async_tree_io | pypy | ok | 0.68337 | 0.87892 | mixed | 27.001 |
| pyperformance.async_tree_io_tg | cpython | ok | 0.72363 | 1 | mixed | 20.556 |
| pyperformance.async_tree_io_tg | lucifer | ok | 1.4876 | 2.0557 | mixed | 20.556 |
| pyperformance.async_tree_io_tg | pypy | ok | 0.61833 | 0.85448 | mixed | 20.556 |
| pyperformance.async_tree_memoization | cpython | ok | 0.43638 | 1 | mixed | 27.245 |
| pyperformance.async_tree_memoization | lucifer | ok | 1.0492 | 2.4044 | mixed | 27.245 |
| pyperformance.async_tree_memoization | pypy | ok | 0.57686 | 1.3219 | mixed | 27.245 |
| pyperformance.async_tree_memoization_tg | cpython | ok | 0.38797 | 1 | mixed | 20.112 |
| pyperformance.async_tree_memoization_tg | lucifer | ok | 0.90525 | 2.3333 | mixed | 20.112 |
| pyperformance.async_tree_memoization_tg | pypy | ok | 0.53796 | 1.3866 | mixed | 20.112 |
| pyperformance.async_tree_tg | cpython | ok | 0.31718 | 1 | mixed | 19.719 |
| pyperformance.async_tree_tg | lucifer | ok | 0.78957 | 2.4894 | mixed | 19.719 |
| pyperformance.async_tree_tg | pypy | ok | 0.42677 | 1.3455 | mixed | 19.719 |
| pyperformance.asyncio_tcp | cpython | ok | 0.50894 | 1 | mixed | 10.944 |
| pyperformance.asyncio_tcp | lucifer | ok | 0.62489 | 1.2278 | mixed | 10.944 |
| pyperformance.asyncio_tcp | pypy | ok | 2.8958 | 5.6899 | mixed | 10.944 |
| pyperformance.asyncio_tcp_ssl | cpython | ok | 1.8072 | 1 | native majority | 76.711 |
| pyperformance.asyncio_tcp_ssl | lucifer | ok | 10.922 | 6.0438 | native majority | 76.711 |
| pyperformance.asyncio_tcp_ssl | pypy | ok | 9.6699 | 5.3508 | native majority | 76.711 |
| pyperformance.asyncio_websockets | cpython | ok | 0.52546 | 1 | native majority | 97.497 |
| pyperformance.asyncio_websockets | lucifer | ok | 0.56921 | 1.0833 | native majority | 97.497 |
| pyperformance.asyncio_websockets | pypy | ok | 0.60021 | 1.1423 | native majority | 97.497 |
| pyperformance.concurrent_imap/bench_mp_pool | cpython | ok | 0.021204 | 1 | mixed | 40.293 |
| pyperformance.concurrent_imap/bench_mp_pool | lucifer | ok | 0.087 | 4.1029 | mixed | 40.293 |
| pyperformance.concurrent_imap/bench_mp_pool | pypy | ok | 0.031316 | 1.4768 | mixed | 40.293 |
| pyperformance.concurrent_imap/bench_thread_pool | cpython | ok | 0.0021148 | 1 | mixed | 40.293 |
| pyperformance.concurrent_imap/bench_thread_pool | lucifer | ok | 0.0090215 | 4.2659 | mixed | 40.293 |
| pyperformance.concurrent_imap/bench_thread_pool | pypy | ok | 0.0057429 | 2.7156 | mixed | 40.293 |
| pyperformance.coverage | cpython | ok | 0.8335 | 1 | mixed | 6.7638 |
| pyperformance.coverage | lucifer | ok | 2.4882 | 2.9853 | mixed | 6.7638 |
| pyperformance.coverage | pypy | ok | 0.20248 | 0.24293 | mixed | 6.7638 |
| pyperformance.dask | cpython | excluded by corpus lock or missing oracle check | — | — | unmeasured guard | — |
| pyperformance.dask | lucifer | excluded by corpus lock or missing oracle check | — | — | unmeasured guard | — |
| pyperformance.dask | pypy | excluded by corpus lock or missing oracle check | — | — | unmeasured guard | — |
| pyperformance.dulwich_log | cpython | ok | 0.092522 | 1 | mixed | 22.632 |
| pyperformance.dulwich_log | lucifer | ok | 0.29561 | 3.195 | mixed | 22.632 |
| pyperformance.dulwich_log | pypy | ok | 0.057288 | 0.61918 | mixed | 22.632 |
| pyperformance.float | cpython | ok | 0.082882 | 1 | mixed | 12.259 |
| pyperformance.float | lucifer | ok | 0.038714 | 0.4671 | mixed | 12.259 |
| pyperformance.float | pypy | ok | 0.013475 | 0.16258 | mixed | 12.259 |
| pyperformance.gc_collect | cpython | ok | 0.0024426 | 1 | native majority | 93.078 |
| pyperformance.gc_collect | lucifer | ok | 0.0033686 | 1.3791 | native majority | 93.078 |
| pyperformance.gc_collect | pypy | ok | 0.0066807 | 2.7351 | native majority | 93.078 |
| pyperformance.gc_traversal | cpython | ok | 0.0032336 | 1 | native majority | 95.152 |
| pyperformance.gc_traversal | lucifer | ok | 0.0065602 | 2.0287 | native majority | 95.152 |
| pyperformance.gc_traversal | pypy | ok | 0.0085757 | 2.6521 | native majority | 95.152 |
| pyperformance.json_dumps | cpython | ok | 0.01589 | 1 | native majority | 66.745 |
| pyperformance.json_dumps | lucifer | ok | 0.045476 | 2.8619 | native majority | 66.745 |
| pyperformance.json_dumps | pypy | ok | 0.0071881 | 0.45237 | native majority | 66.745 |
| pyperformance.json_loads | cpython | ok | 4.0128e-05 | 1 | native majority | 91.286 |
| pyperformance.json_loads | lucifer | ok | 0.00010438 | 2.6012 | native majority | 91.286 |
| pyperformance.json_loads | pypy | ok | 9.7977e-06 | 0.24416 | native majority | 91.286 |
| pyperformance.logging/logging_format | cpython | ok | 1.0742e-05 | 1 | mixed | 7.0332 |
| pyperformance.logging/logging_format | lucifer | ok | 1.4552e-05 | 1.3547 | mixed | 7.0332 |
| pyperformance.logging/logging_format | pypy | ok | 3.4741e-06 | 0.32342 | mixed | 7.0332 |
| pyperformance.logging/logging_silent | cpython | ok | 1.1074e-07 | 1 | mixed | 7.0332 |
| pyperformance.logging/logging_silent | lucifer | ok | 1.3634e-07 | 1.2312 | mixed | 7.0332 |
| pyperformance.logging/logging_silent | pypy | ok | 4.9626e-09 | 0.044815 | mixed | 7.0332 |
| pyperformance.logging/logging_simple | cpython | ok | 1.053e-05 | 1 | mixed | 7.0332 |
| pyperformance.logging/logging_simple | lucifer | ok | 1.2089e-05 | 1.148 | mixed | 7.0332 |
| pyperformance.logging/logging_simple | pypy | ok | 2.9938e-06 | 0.28432 | mixed | 7.0332 |
| pyperformance.pathlib | cpython | ok | 0.023065 | 1 | mixed | 41.941 |
| pyperformance.pathlib | lucifer | ok | 0.031095 | 1.3481 | mixed | 41.941 |
| pyperformance.pathlib | pypy | ok | 0.012679 | 0.54972 | mixed | 41.941 |
| pyperformance.pickle | cpython | ok | 1.4588e-05 | 1 | native majority | 99.46 |
| pyperformance.pickle | lucifer | ok | 2.4298e-05 | 1.6655 | native majority | 99.46 |
| pyperformance.pickle | pypy | ok | 2.789e-05 | 1.9118 | native majority | 99.46 |
| pyperformance.pickle_dict | cpython | ok | 3.9553e-05 | 1 | native majority | 98.896 |
| pyperformance.pickle_dict | lucifer | ok | 5.0505e-05 | 1.2769 | native majority | 98.896 |
| pyperformance.pickle_dict | pypy | ok | 0.00015085 | 3.8139 | native majority | 98.896 |
| pyperformance.pickle_list | cpython | ok | 6.2061e-06 | 1 | native majority | 99.459 |
| pyperformance.pickle_list | lucifer | ok | 8.8387e-06 | 1.4242 | native majority | 99.459 |
| pyperformance.pickle_list | pypy | ok | 2.2862e-05 | 3.6838 | native majority | 99.459 |
| pyperformance.pickle_pure_python | cpython | ok | 0.00047198 | 1 | mixed | 9.5007 |
| pyperformance.pickle_pure_python | lucifer | ok | 0.00075129 | 1.5918 | mixed | 9.5007 |
| pyperformance.pickle_pure_python | pypy | ok | 3.8093e-05 | 0.080709 | mixed | 9.5007 |
| pyperformance.regex_dna | cpython | ok | 0.18781 | 1 | native majority | 99.58 |
| pyperformance.regex_dna | lucifer | ok | 0.17309 | 0.92161 | native majority | 99.58 |
| pyperformance.regex_dna | pypy | ok | 0.097109 | 0.51705 | native majority | 99.58 |
| pyperformance.regex_effbot | cpython | ok | 0.0027575 | 1 | native majority | 94.938 |
| pyperformance.regex_effbot | lucifer | ok | 0.0046132 | 1.673 | native majority | 94.938 |
| pyperformance.regex_effbot | pypy | ok | 0.0027025 | 0.98007 | native majority | 94.938 |
| pyperformance.regex_v8 | cpython | ok | 0.021605 | 1 | native majority | 85.272 |
| pyperformance.regex_v8 | lucifer | ok | 0.092426 | 4.278 | native majority | 85.272 |
| pyperformance.regex_v8 | pypy | ok | 0.021322 | 0.98688 | native majority | 85.272 |
| pyperformance.scimark/scimark_fft | cpython | ok | 0.46671 | 1 | mixed | 18.893 |
| pyperformance.scimark/scimark_fft | lucifer | ok | 0.054981 | 0.11781 | mixed | 18.893 |
| pyperformance.scimark/scimark_fft | pypy | ok | 0.0086035 | 0.018434 | mixed | 18.893 |
| pyperformance.scimark/scimark_lu | cpython | ok | 0.17194 | 1 | mixed | 18.893 |
| pyperformance.scimark/scimark_lu | lucifer | ok | 0.028775 | 0.16736 | mixed | 18.893 |
| pyperformance.scimark/scimark_lu | pypy | ok | 0.0018902 | 0.010994 | mixed | 18.893 |
| pyperformance.scimark/scimark_monte_carlo | cpython | ok | 0.11246 | 1 | mixed | 18.893 |
| pyperformance.scimark/scimark_monte_carlo | lucifer | ok | 0.030562 | 0.27176 | mixed | 18.893 |
| pyperformance.scimark/scimark_monte_carlo | pypy | ok | 0.002872 | 0.025537 | mixed | 18.893 |
| pyperformance.scimark/scimark_sor | cpython | ok | 0.15435 | 1 | mixed | 18.893 |
| pyperformance.scimark/scimark_sor | lucifer | ok | 0.0057391 | 0.037182 | mixed | 18.893 |
| pyperformance.scimark/scimark_sor | pypy | ok | 0.00098489 | 0.0063807 | mixed | 18.893 |
| pyperformance.scimark/scimark_sparse_mat_mult | cpython | ok | 0.0085064 | 1 | mixed | 18.893 |
| pyperformance.scimark/scimark_sparse_mat_mult | lucifer | ok | 0.00065313 | 0.076781 | mixed | 18.893 |
| pyperformance.scimark/scimark_sparse_mat_mult | pypy | ok | 0.00024443 | 0.028735 | mixed | 18.893 |
| pyperformance.sqlalchemy_declarative | cpython | ok | 0.1756 | 1 | mixed | 9.7645 |
| pyperformance.sqlalchemy_declarative | lucifer | ok | 0.54336 | 3.0943 | mixed | 9.7645 |
| pyperformance.sqlalchemy_declarative | pypy | pyperf worker exit 1 | — | — | mixed | 9.7645 |
| pyperformance.sqlalchemy_imperative | cpython | ok | 0.029829 | 1 | mixed | 10.898 |
| pyperformance.sqlalchemy_imperative | lucifer | ok | 0.06054 | 2.0296 | mixed | 10.898 |
| pyperformance.sqlalchemy_imperative | pypy | ok | 0.017682 | 0.59277 | mixed | 10.898 |
| pyperformance.sqlite_synth | cpython | ok | 2.6976e-06 | 1 | native majority | 85.122 |
| pyperformance.sqlite_synth | lucifer | ok | 6.0756e-06 | 2.2522 | native majority | 85.122 |
| pyperformance.sqlite_synth | pypy | ok | 4.896e-06 | 1.8149 | native majority | 85.122 |
| pyperformance.sympy/sympy_expand | cpython | ok | 0.57433 | 1 | mixed | 6.124 |
| pyperformance.sympy/sympy_expand | lucifer | ok | 1.3434 | 2.3391 | mixed | 6.124 |
| pyperformance.sympy/sympy_expand | pypy | ok | 0.15454 | 0.26908 | mixed | 6.124 |
| pyperformance.sympy/sympy_integrate | cpython | ok | 0.02009 | 1 | mixed | 6.124 |
| pyperformance.sympy/sympy_integrate | lucifer | ok | 0.089694 | 4.4645 | mixed | 6.124 |
| pyperformance.sympy/sympy_integrate | pypy | ok | 0.083968 | 4.1795 | mixed | 6.124 |
| pyperformance.sympy/sympy_str | cpython | ok | 0.3111 | 1 | mixed | 6.124 |
| pyperformance.sympy/sympy_str | lucifer | ok | 0.86472 | 2.7795 | mixed | 6.124 |
| pyperformance.sympy/sympy_str | pypy | ok | 0.15126 | 0.4862 | mixed | 6.124 |
| pyperformance.sympy/sympy_sum | cpython | ok | 0.19751 | 1 | mixed | 6.124 |
| pyperformance.sympy/sympy_sum | lucifer | ok | 0.56797 | 2.8757 | mixed | 6.124 |
| pyperformance.sympy/sympy_sum | pypy | ok | 0.14258 | 0.7219 | mixed | 6.124 |
| pyperformance.telco | cpython | ok | 0.011757 | 1 | native majority | 63.908 |
| pyperformance.telco | lucifer | ok | 0.032228 | 2.7412 | native majority | 63.908 |
| pyperformance.telco | pypy | ok | 0.0090313 | 0.76816 | native majority | 63.908 |
| pyperformance.typing_runtime_protocols | cpython | ok | 0.0002476 | 1 | mixed | 18.016 |
| pyperformance.typing_runtime_protocols | lucifer | ok | 0.00067432 | 2.7234 | mixed | 18.016 |
| pyperformance.typing_runtime_protocols | pypy | ok | 0.00075335 | 3.0426 | mixed | 18.016 |
| pyperformance.unpickle | cpython | ok | 2.2683e-05 | 1 | native majority | 91.529 |
| pyperformance.unpickle | lucifer | ok | 4.5786e-05 | 2.0185 | native majority | 91.529 |
| pyperformance.unpickle | pypy | ok | 4.3085e-05 | 1.8995 | native majority | 91.529 |
| pyperformance.unpickle_list | cpython | ok | 5.4994e-06 | 1 | native majority | 93.222 |
| pyperformance.unpickle_list | lucifer | ok | 8.7432e-06 | 1.5899 | native majority | 93.222 |
| pyperformance.unpickle_list | pypy | ok | 1.5891e-05 | 2.8897 | native majority | 93.222 |
| pyperformance.unpickle_pure_python | cpython | ok | 0.00031194 | 1 | mixed | 12.844 |
| pyperformance.unpickle_pure_python | lucifer | ok | 0.00059014 | 1.8918 | mixed | 12.844 |
| pyperformance.unpickle_pure_python | pypy | ok | 4.7981e-05 | 0.15382 | mixed | 12.844 |
| pyperformance.xml_etree | pypy | pyperf benchmark names differ from oracle | — | — | native majority | 54.822 |
| pyperformance.xml_etree/xml_etree_generate | cpython | ok | 0.13269 | 1 | native majority | 54.822 |
| pyperformance.xml_etree/xml_etree_generate | lucifer | ok | 0.38882 | 2.9302 | native majority | 54.822 |
| pyperformance.xml_etree/xml_etree_iterparse | cpython | ok | 0.11685 | 1 | native majority | 54.822 |
| pyperformance.xml_etree/xml_etree_iterparse | lucifer | ok | 0.25511 | 2.1833 | native majority | 54.822 |
| pyperformance.xml_etree/xml_etree_parse | cpython | ok | 0.17488 | 1 | native majority | 54.822 |
| pyperformance.xml_etree/xml_etree_parse | lucifer | ok | 0.26343 | 1.5064 | native majority | 54.822 |
| pyperformance.xml_etree/xml_etree_process | cpython | ok | 0.090501 | 1 | native majority | 54.822 |
| pyperformance.xml_etree/xml_etree_process | lucifer | ok | 0.25377 | 2.8041 | native majority | 54.822 |

## 5. Time to steady state

The last five values define the final median. The suffix must contain at least five values, all within 5% of that median. A batch reports its first operation. An unsettled row fails this card. Unit-event replays measure Lucifer churn. PyPy loop and bridge counters are in the CSV.

| program | runtime | status | first_steady_op | warmup_s | compile_ms | post_compile_ms | post_rebuilds | post_kills |
|---|---|---|---|---|---|---|---|---|
| awfy.Bounce | cpython | ok | 1 | 0 | — | — | — | — |
| awfy.Bounce | go | unsettled | — | — | — | — | — | — |
| awfy.Bounce | lucifer | ok | 2 | 0.17456 | 11.501 | 1.2 | 1 | 1 |
| awfy.Bounce | pypy | ok | 4 | 0.18102 | 13.05 | 0 | — | — |
| awfy.CD | cpython | ok | 1 | 0 | — | — | — | — |
| awfy.CD | go | unsettled | — | — | — | — | — | — |
| awfy.CD | lucifer | unsettled | — | — | 166.1 | — | — | — |
| awfy.CD | pypy | unsettled | — | — | 682.93 | — | — | — |
| awfy.DeltaBlue | cpython | unsettled | — | — | — | — | — | — |
| awfy.DeltaBlue | go | unsettled | — | — | — | — | — | — |
| awfy.DeltaBlue | lucifer | unsettled | — | — | 193.48 | — | — | — |
| awfy.DeltaBlue | pypy | unsettled | — | — | 239.22 | — | — | — |
| awfy.Havlak | cpython | ok | 1 | 0 | — | — | — | — |
| awfy.Havlak | go | ok | 6 | 1.1729 | — | — | — | — |
| awfy.Havlak | lucifer | ok | 1 | 0 | 448.3 | 444.77 | 133 | 132 |
| awfy.Havlak | pypy | ok | 6 | 3.3033 | 218.15 | 4.8778 | — | — |
| awfy.Json | cpython | unsettled | — | — | — | — | — | — |
| awfy.Json | go | unsettled | — | — | — | — | — | — |
| awfy.Json | lucifer | unsettled | — | — | 210.85 | — | — | — |
| awfy.Json | pypy | unsettled | — | — | 440.62 | — | — | — |
| awfy.List | cpython | unsettled | — | — | — | — | — | — |
| awfy.List | go | unsettled | — | — | — | — | — | — |
| awfy.List | lucifer | ok | 2 | 0.31476 | 6.095 | 0 | 0 | 0 |
| awfy.List | pypy | unsettled | — | — | 25.826 | — | — | — |
| awfy.Mandelbrot | cpython | ok | 1 | 0 | — | — | — | — |
| awfy.Mandelbrot | go | ok | 1 | 0 | — | — | — | — |
| awfy.Mandelbrot | lucifer | ok | 2 | 0.079782 | 7.214 | 0.566 | 1 | 0 |
| awfy.Mandelbrot | pypy | ok | 2 | 0.064584 | 7.1626 | 0 | — | — |
| awfy.NBody | cpython | ok | 1 | 0 | — | — | — | — |
| awfy.NBody | go | ok | 1 | 0 | — | — | — | — |
| awfy.NBody | lucifer | ok | 2 | 0.39972 | 17.799 | 1.563 | 1 | 1 |
| awfy.NBody | pypy | unsettled | — | — | 7.616 | — | — | — |
| awfy.Permute | cpython | ok | 1 | 0 | — | — | — | — |
| awfy.Permute | go | ok | 1 | 0 | — | — | — | — |
| awfy.Permute | lucifer | ok | 2 | 0.99872 | 16.124 | 8.152 | 3 | 3 |
| awfy.Permute | pypy | unsettled | — | — | 47.173 | — | — | — |
| awfy.Queens | cpython | unsettled | — | — | — | — | — | — |
| awfy.Queens | go | unsettled | — | — | — | — | — | — |
| awfy.Queens | lucifer | unsettled | — | — | 20.959 | — | — | — |
| awfy.Queens | pypy | ok | 3 | 0.24869 | 16.895 | 0 | — | — |
| awfy.Richards | cpython | ok | 1 | 0 | — | — | — | — |
| awfy.Richards | go | ok | 3 | 0.16024 | — | — | — | — |
| awfy.Richards | lucifer | ok | 2 | 1.1945 | 20.332 | 5.019 | 13 | 13 |
| awfy.Richards | pypy | unsettled | — | — | 61.851 | — | — | — |
| awfy.Sieve | cpython | ok | 1 | 0 | — | — | — | — |
| awfy.Sieve | go | unsettled | — | — | — | — | — | — |
| awfy.Sieve | lucifer | unsettled | — | — | 5.475 | — | — | — |
| awfy.Sieve | pypy | ok | 6 | 0.71245 | 7.3376 | 0 | — | — |
| awfy.Storage | cpython | ok | 2 | 1.9401 | — | — | — | — |
| awfy.Storage | go | unsettled | — | — | — | — | — | — |
| awfy.Storage | lucifer | unsettled | — | — | 10.221 | — | — | — |
| awfy.Storage | pypy | unsettled | — | — | 22.527 | — | — | — |
| awfy.Towers | cpython | ok | 2 | 1.5232 | — | — | — | — |
| awfy.Towers | go | unsettled | — | — | — | — | — | — |
| awfy.Towers | lucifer | unsettled | — | — | 7.169 | — | — | — |
| awfy.Towers | pypy | unsettled | — | — | 8.6625 | — | — | — |
| flask.1 | cpython | ok | 1 | 0.20192 | — | — | — | — |
| flask.1 | lucifer | unsettled | — | — | 4817 | — | — | — |
| flask.1 | pypy | ok | 42380 | 11.092 | 1098.3 | 19.609 | — | — |
| flask.8 | cpython | ok | 1 | 0.177 | — | — | — | — |
| flask.8 | lucifer | unsettled | — | — | 5664.3 | — | — | — |
| flask.8 | pypy | unsettled | — | — | 1992.7 | — | — | — |
| pyperformance.argparse_subparsers | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.argparse_subparsers | lucifer | unsettled | — | — | 259.88 | — | — | — |
| pyperformance.argparse_subparsers | pypy | unsettled | — | — | 74.933 | — | — | — |
| pyperformance.async_generators | cpython | ok | 4 | 1.3415 | — | — | — | — |
| pyperformance.async_generators | lucifer | unsettled | — | — | 88.143 | — | — | — |
| pyperformance.async_generators | pypy | unsettled | — | — | 39.334 | — | — | — |
| pyperformance.base64/ascii85_large | cpython | ok | 3 | 2.934 | — | — | — | — |
| pyperformance.base64/ascii85_large | lucifer | ok | 4 | 3.7474 | 366.02 | 0 | 0 | 0 |
| pyperformance.base64/ascii85_large | pypy | unsettled | — | — | 75.137 | — | — | — |
| pyperformance.base64/ascii85_small | cpython | ok | 1 | 0 | — | — | — | — |
| pyperformance.base64/ascii85_small | lucifer | unsettled | — | — | 352.8 | — | — | — |
| pyperformance.base64/ascii85_small | pypy | unsettled | — | — | 75.137 | — | — | — |
| pyperformance.base64/base16_large | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.base64/base16_large | lucifer | unsettled | — | — | 268.92 | — | — | — |
| pyperformance.base64/base16_large | pypy | unsettled | — | — | 49.963 | — | — | — |
| pyperformance.base64/base16_small | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.base64/base16_small | lucifer | unsettled | — | — | 264.44 | — | — | — |
| pyperformance.base64/base16_small | pypy | unsettled | — | — | 49.778 | — | — | — |
| pyperformance.base64/base32_large | cpython | ok | 4 | 1.9659 | — | — | — | — |
| pyperformance.base64/base32_large | lucifer | ok | 1 | 0 | 233.64 | 0 | 0 | 0 |
| pyperformance.base64/base32_large | pypy | unsettled | — | — | 46.464 | — | — | — |
| pyperformance.base64/base32_small | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.base64/base32_small | lucifer | unsettled | — | — | 229.97 | — | — | — |
| pyperformance.base64/base32_small | pypy | unsettled | — | — | 43.857 | — | — | — |
| pyperformance.base64/base64_large | cpython | ok | 2 | 0.0071154 | — | — | — | — |
| pyperformance.base64/base64_large | lucifer | unsettled | — | — | 176.82 | — | — | — |
| pyperformance.base64/base64_large | pypy | unsettled | — | — | 28.912 | — | — | — |
| pyperformance.base64/base64_small | cpython | ok | 4 | 0.0012844 | — | — | — | — |
| pyperformance.base64/base64_small | lucifer | unsettled | — | — | 77.692 | — | — | — |
| pyperformance.base64/base64_small | pypy | unsettled | — | — | 28.29 | — | — | — |
| pyperformance.base64/base85_large | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.base64/base85_large | lucifer | unsettled | — | — | 399.52 | — | — | — |
| pyperformance.base64/base85_large | pypy | ok | 2 | 0.14553 | 83.688 | 0 | — | — |
| pyperformance.base64/base85_small | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.base64/base85_small | lucifer | unsettled | — | — | 394.47 | — | — | — |
| pyperformance.base64/base85_small | pypy | unsettled | — | — | 83.688 | — | — | — |
| pyperformance.base64/urlsafe_base64_small | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.base64/urlsafe_base64_small | lucifer | unsettled | — | — | 202.17 | — | — | — |
| pyperformance.base64/urlsafe_base64_small | pypy | unsettled | — | — | 31.433 | — | — | — |
| pyperformance.bpe_tokeniser | cpython | ok | 1 | 0 | — | — | — | — |
| pyperformance.bpe_tokeniser | lucifer | ok | 2 | 4.6073 | 289.52 | 102.68 | 36 | 32 |
| pyperformance.bpe_tokeniser | pypy | ok | 2 | 2.09 | 77.229 | 5.899 | — | — |
| pyperformance.chameleon | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.chameleon | lucifer | unsettled | — | — | 413.19 | — | — | — |
| pyperformance.chameleon | pypy | unsettled | — | — | 104.9 | — | — | — |
| pyperformance.chaos | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.chaos | lucifer | unsettled | — | — | 118.2 | — | — | — |
| pyperformance.chaos | pypy | unsettled | — | — | 72.658 | — | — | — |
| pyperformance.comprehensions | cpython | ok | 1 | 0 | — | — | — | — |
| pyperformance.comprehensions | lucifer | unsettled | — | — | 106.44 | — | — | — |
| pyperformance.comprehensions | pypy | ok | 8527 | 0.11634 | 34.396 | 0 | — | — |
| pyperformance.coroutines | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.coroutines | lucifer | unsettled | — | — | 47.516 | — | — | — |
| pyperformance.coroutines | pypy | ok | 5 | 0.092694 | 33.225 | 0 | — | — |
| pyperformance.crypto_pyaes | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.crypto_pyaes | lucifer | unsettled | — | — | 100.48 | — | — | — |
| pyperformance.crypto_pyaes | pypy | unsettled | — | — | 24.036 | — | — | — |
| pyperformance.deepcopy/deepcopy | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.deepcopy/deepcopy | lucifer | unsettled | — | — | 142.68 | — | — | — |
| pyperformance.deepcopy/deepcopy | pypy | unsettled | — | — | 66.489 | — | — | — |
| pyperformance.deepcopy/deepcopy_memo | cpython | ok | 1 | 0 | — | — | — | — |
| pyperformance.deepcopy/deepcopy_memo | lucifer | unsettled | — | — | 280.86 | — | — | — |
| pyperformance.deepcopy/deepcopy_memo | pypy | unsettled | — | — | 136.3 | — | — | — |
| pyperformance.deepcopy/deepcopy_reduce | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.deepcopy/deepcopy_reduce | lucifer | unsettled | — | — | 254.05 | — | — | — |
| pyperformance.deepcopy/deepcopy_reduce | pypy | unsettled | — | — | 114.35 | — | — | — |
| pyperformance.deltablue | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.deltablue | lucifer | unsettled | — | — | 248.39 | — | — | — |
| pyperformance.deltablue | pypy | unsettled | — | — | 181.42 | — | — | — |
| pyperformance.django_template | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.django_template | lucifer | unsettled | — | — | 228.73 | — | — | — |
| pyperformance.django_template | pypy | unsettled | — | — | 51.748 | — | — | — |
| pyperformance.docutils | cpython | ok | 1 | 0 | — | — | — | — |
| pyperformance.docutils | lucifer | ok | 3 | 20.824 | 5068.6 | 2142.4 | 635 | 616 |
| pyperformance.docutils | pypy | unsettled | — | — | 5378.6 | — | — | — |
| pyperformance.fannkuch | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.fannkuch | lucifer | ok | 3 | 1.1256 | 53.941 | 0 | 0 | 0 |
| pyperformance.fannkuch | pypy | ok | 2 | 0.094724 | 20.078 | 0 | — | — |
| pyperformance.fastapi | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.fastapi | lucifer | unsettled | — | — | 2117.4 | — | — | — |
| pyperformance.fastapi | pypy | pyperf worker exit 1 | — | — | — | — | — | — |
| pyperformance.generators | cpython | ok | 10 | 0.57286 | — | — | — | — |
| pyperformance.generators | lucifer | ok | 4 | 0.30201 | 51.749 | 0 | 0 | 0 |
| pyperformance.generators | pypy | unsettled | — | — | 140.4 | — | — | — |
| pyperformance.genshi/genshi_text | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.genshi/genshi_text | lucifer | unsettled | — | — | 154.26 | — | — | — |
| pyperformance.genshi/genshi_text | pypy | unsettled | — | — | 195.28 | — | — | — |
| pyperformance.genshi/genshi_xml | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.genshi/genshi_xml | lucifer | unsettled | — | — | 394.68 | — | — | — |
| pyperformance.genshi/genshi_xml | pypy | unsettled | — | — | 317.99 | — | — | — |
| pyperformance.go | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.go | lucifer | unsettled | — | — | 159.39 | — | — | — |
| pyperformance.go | pypy | unsettled | — | — | 632.75 | — | — | — |
| pyperformance.hexiom | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.hexiom | lucifer | unsettled | — | — | 233.56 | — | — | — |
| pyperformance.hexiom | pypy | unsettled | — | — | 136.2 | — | — | — |
| pyperformance.html5lib | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.html5lib | lucifer | unsettled | — | — | 329.71 | — | — | — |
| pyperformance.html5lib | pypy | unsettled | — | — | 207.14 | — | — | — |
| pyperformance.mako | cpython | ok | 13 | 0.2535 | — | — | — | — |
| pyperformance.mako | lucifer | unsettled | — | — | 178.03 | — | — | — |
| pyperformance.mako | pypy | unsettled | — | — | 47.323 | — | — | — |
| pyperformance.mdp | cpython | ok | 1 | 0 | — | — | — | — |
| pyperformance.mdp | lucifer | ok | 2 | 7.5997 | 539.63 | 192.78 | 45 | 44 |
| pyperformance.mdp | pypy | ok | 2 | 6.0473 | 309.21 | 21.245 | — | — |
| pyperformance.meteor_contest | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.meteor_contest | lucifer | unsettled | — | — | 96.172 | — | — | — |
| pyperformance.meteor_contest | pypy | ok | 2 | 0.072324 | 28.198 | 4.0288 | — | — |
| pyperformance.nbody | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.nbody | lucifer | ok | 2 | 0.12106 | 52.46 | 1.063 | 1 | 0 |
| pyperformance.nbody | pypy | unsettled | — | — | 13.596 | — | — | — |
| pyperformance.networkx | cpython | ok | 1 | 0 | — | — | — | — |
| pyperformance.networkx | lucifer | ok | 3 | 1.4692 | 238.68 | 0 | 0 | 0 |
| pyperformance.networkx | pypy | unsettled | — | — | 78.645 | — | — | — |
| pyperformance.networkx_connected_components | cpython | ok | 1 | 0 | — | — | — | — |
| pyperformance.networkx_connected_components | lucifer | ok | 3 | 1.3282 | 229.32 | 0 | 0 | 0 |
| pyperformance.networkx_connected_components | pypy | unsettled | — | — | 62.974 | — | — | — |
| pyperformance.networkx_k_core | cpython | ok | 1 | 0 | — | — | — | — |
| pyperformance.networkx_k_core | lucifer | ok | 1 | 0 | 398.96 | 175.14 | 132 | 119 |
| pyperformance.networkx_k_core | pypy | unsettled | — | — | 166.74 | — | — | — |
| pyperformance.nqueens | cpython | ok | 3 | 0.24573 | — | — | — | — |
| pyperformance.nqueens | lucifer | unsettled | — | — | 63.645 | — | — | — |
| pyperformance.nqueens | pypy | ok | 4 | 0.1108 | 35.676 | 1.302 | — | — |
| pyperformance.pidigits | cpython | ok | 4 | 0.6206 | — | — | — | — |
| pyperformance.pidigits | lucifer | unsettled | — | — | 84.89 | — | — | — |
| pyperformance.pidigits | pypy | unsettled | — | — | 21.97 | — | — | — |
| pyperformance.pprint/pprint_pformat | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.pprint/pprint_pformat | lucifer | ok | 3 | 10.3 | 781.33 | 149.84 | 25 | 25 |
| pyperformance.pprint/pprint_pformat | pypy | unsettled | — | — | 41.497 | — | — | — |
| pyperformance.pprint/pprint_safe_repr | cpython | ok | 4 | 3.3581 | — | — | — | — |
| pyperformance.pprint/pprint_safe_repr | lucifer | unsettled | — | — | 309.47 | — | — | — |
| pyperformance.pprint/pprint_safe_repr | pypy | unsettled | — | — | 35.302 | — | — | — |
| pyperformance.pyflate | cpython | ok | 1 | 0 | — | — | — | — |
| pyperformance.pyflate | lucifer | unsettled | — | — | 260.1 | — | — | — |
| pyperformance.pyflate | pypy | ok | 3 | 0.41653 | 93.614 | 16.92 | — | — |
| pyperformance.raytrace | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.raytrace | lucifer | unsettled | — | — | 250.66 | — | — | — |
| pyperformance.raytrace | pypy | ok | 4 | 0.29285 | 145.57 | 3.3488 | — | — |
| pyperformance.regex_compile | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.regex_compile | lucifer | unsettled | — | — | 824.89 | — | — | — |
| pyperformance.regex_compile | pypy | pyperf stderr differs from oracle | — | — | — | — | — | — |
| pyperformance.richards | cpython | ok | 7 | 0.39507 | — | — | — | — |
| pyperformance.richards | lucifer | unsettled | — | — | 93.284 | — | — | — |
| pyperformance.richards | pypy | ok | 7 | 0.086666 | 51.158 | 0 | — | — |
| pyperformance.richards_super | cpython | ok | 7 | 0.48795 | — | — | — | — |
| pyperformance.richards_super | lucifer | ok | 7 | 0.16107 | 96.703 | 0 | 0 | 1 |
| pyperformance.richards_super | pypy | unsettled | — | — | 51.693 | — | — | — |
| pyperformance.spectral_norm | cpython | ok | 4 | 0.41424 | — | — | — | — |
| pyperformance.spectral_norm | lucifer | ok | 2 | 0.062136 | 55.768 | 0.536 | 0 | 0 |
| pyperformance.spectral_norm | pypy | unsettled | — | — | 21.402 | — | — | — |
| pyperformance.sphinx | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.sphinx | lucifer | ok | 4 | 15.092 | 3575.1 | 1154.3 | 516 | 508 |
| pyperformance.sphinx | pypy | unsettled | — | — | 5158.4 | — | — | — |
| pyperformance.sqlglot_v2 | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.sqlglot_v2 | lucifer | unsettled | — | — | 575.78 | — | — | — |
| pyperformance.sqlglot_v2 | pypy | unsettled | — | — | 550.35 | — | — | — |
| pyperformance.sqlglot_v2_optimize | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.sqlglot_v2_optimize | lucifer | unsettled | — | — | 740.67 | — | — | — |
| pyperformance.sqlglot_v2_optimize | pypy | unsettled | — | — | 833.13 | — | — | — |
| pyperformance.sqlglot_v2_parse | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.sqlglot_v2_parse | lucifer | unsettled | — | — | 280.49 | — | — | — |
| pyperformance.sqlglot_v2_parse | pypy | unsettled | — | — | 359.58 | — | — | — |
| pyperformance.sqlglot_v2_transpile | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.sqlglot_v2_transpile | lucifer | unsettled | — | — | 337.79 | — | — | — |
| pyperformance.sqlglot_v2_transpile | pypy | unsettled | — | — | 430.45 | — | — | — |
| pyperformance.tomli_loads | cpython | ok | 1 | 0 | — | — | — | — |
| pyperformance.tomli_loads | lucifer | ok | 3 | 6.0579 | 332.49 | 76.311 | 12 | 13 |
| pyperformance.tomli_loads | pypy | ok | 2 | 1.6463 | 221.75 | 0 | — | — |
| pyperformance.tornado_http | cpython | ok | 4 | 0.51875 | — | — | — | — |
| pyperformance.tornado_http | lucifer | unsettled | — | — | 851.06 | — | — | — |
| pyperformance.tornado_http | pypy | unsettled | — | — | 660.6 | — | — | — |
| pyperformance.unpack_sequence | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.unpack_sequence | lucifer | unsettled | — | — | 161.81 | — | — | — |
| pyperformance.unpack_sequence | pypy | unsettled | — | — | 55.149 | — | — | — |
| pyperformance.xdsl | cpython | unsettled | — | — | — | — | — | — |
| pyperformance.xdsl | lucifer | unsettled | — | — | 606.81 | — | — | — |
| pyperformance.xdsl | pypy | unsettled | — | — | 443.81 | — | — | — |

## Gates

| program | status | score | counts | date | detail |
|---|---|---|---|---|---|
| check-fast-float | pass |  |  | 2026-11-27 |  |
| check-blocking | pass |  |  | 2026-11-27 |  |
| check-globals | pass |  |  | 2026-11-27 |  |
| check-capi | pass |  |  | 2026-11-27 |  |
| check-kernel | pass |  |  | 2026-11-27 |  |
| check-kernel-jit | pass |  |  | 2026-11-27 |  |
| check-embed | pass |  |  | 2026-11-27 |  |
| check-jit | pass |  |  | 2026-11-27 |  |
| kernel | measured | 100.0% | 972 of 972, 3 modes (worst lane) | 2026-11-27 | plain 972/972, jit 972/972, jit-deopt 972/972 |
| differential | measured | 100.0% | 193 of 193, 2 modes (worst lane) | 2026-11-27 | always 193/193, deopt 193/193 |
| scoreboard | pass |  |  | 2026-11-27 |  |
| libtest | measured | 79.8% | 40,528 of CPython's 50,816 | 2026-11-21 |  |
| numpy | measured | 97.1% | 50,745 of 52,273 collected | 2026-11-15 |  |
| abi-audit | pass |  |  | 2026-11-27 |  |
| capi | measured | 70.8% | 1,153 of 1,628 | 2026-11-27 |  |

## Problems

- Card 1, cpython, unsettled: awfy.DeltaBlue, awfy.Json, awfy.List, awfy.Queens, pyperformance.argparse_subparsers, pyperformance.base64/base16_large, pyperformance.base64/base16_small, pyperformance.base64/base32_small, pyperformance.base64/base85_large, pyperformance.base64/base85_small, pyperformance.base64/urlsafe_base64_small, pyperformance.chaos, pyperformance.coroutines, pyperformance.crypto_pyaes, pyperformance.deepcopy/deepcopy, pyperformance.deepcopy/deepcopy_reduce, pyperformance.deltablue, pyperformance.fannkuch, pyperformance.go, pyperformance.hexiom, pyperformance.meteor_contest, pyperformance.nbody, pyperformance.pprint/pprint_pformat, pyperformance.raytrace, pyperformance.regex_compile, pyperformance.sqlglot_v2, pyperformance.sqlglot_v2_optimize, pyperformance.sqlglot_v2_parse, pyperformance.sqlglot_v2_transpile, pyperformance.unpack_sequence, pyperformance.xdsl
- Card 1, go, unsettled: awfy.Bounce, awfy.CD, awfy.DeltaBlue, awfy.Json, awfy.List, awfy.Queens, awfy.Sieve, awfy.Storage, awfy.Towers
- Card 1, lucifer, unsettled: awfy.CD, awfy.DeltaBlue, awfy.Json, awfy.Queens, awfy.Sieve, awfy.Storage, awfy.Towers, pyperformance.argparse_subparsers, pyperformance.async_generators, pyperformance.base64/ascii85_small, pyperformance.base64/base16_large, pyperformance.base64/base16_small, pyperformance.base64/base32_small, pyperformance.base64/base64_large, pyperformance.base64/base64_small, pyperformance.base64/base85_large, pyperformance.base64/base85_small, pyperformance.base64/urlsafe_base64_small, pyperformance.chaos, pyperformance.comprehensions, pyperformance.coroutines, pyperformance.crypto_pyaes, pyperformance.deepcopy/deepcopy, pyperformance.deepcopy/deepcopy_memo, pyperformance.deepcopy/deepcopy_reduce, pyperformance.deltablue, pyperformance.go, pyperformance.hexiom, pyperformance.meteor_contest, pyperformance.nqueens, pyperformance.pidigits, pyperformance.pprint/pprint_safe_repr, pyperformance.pyflate, pyperformance.raytrace, pyperformance.regex_compile, pyperformance.richards, pyperformance.sqlglot_v2, pyperformance.sqlglot_v2_optimize, pyperformance.sqlglot_v2_parse, pyperformance.sqlglot_v2_transpile, pyperformance.unpack_sequence, pyperformance.xdsl
- Card 1, pypy, pyperf stderr differs from oracle: pyperformance.regex_compile
- Card 1, pypy, unsettled: awfy.CD, awfy.DeltaBlue, awfy.Json, awfy.List, awfy.NBody, awfy.Permute, awfy.Richards, awfy.Storage, awfy.Towers, pyperformance.argparse_subparsers, pyperformance.async_generators, pyperformance.base64/ascii85_large, pyperformance.base64/ascii85_small, pyperformance.base64/base16_large, pyperformance.base64/base16_small, pyperformance.base64/base32_large, pyperformance.base64/base32_small, pyperformance.base64/base64_large, pyperformance.base64/base64_small, pyperformance.base64/base85_small, pyperformance.base64/urlsafe_base64_small, pyperformance.chaos, pyperformance.crypto_pyaes, pyperformance.deepcopy/deepcopy, pyperformance.deepcopy/deepcopy_memo, pyperformance.deepcopy/deepcopy_reduce, pyperformance.deltablue, pyperformance.generators, pyperformance.go, pyperformance.hexiom, pyperformance.nbody, pyperformance.networkx, pyperformance.networkx_connected_components, pyperformance.networkx_k_core, pyperformance.pidigits, pyperformance.pprint/pprint_pformat, pyperformance.pprint/pprint_safe_repr, pyperformance.richards_super, pyperformance.spectral_norm, pyperformance.sqlglot_v2, pyperformance.sqlglot_v2_optimize, pyperformance.sqlglot_v2_parse, pyperformance.sqlglot_v2_transpile, pyperformance.unpack_sequence, pyperformance.xdsl
- Card 2, cpython, unsettled: pyperformance.chameleon, pyperformance.django_template, pyperformance.fastapi, pyperformance.genshi/genshi_text, pyperformance.genshi/genshi_xml, pyperformance.html5lib, pyperformance.sphinx
- Card 2, lucifer, unsettled: pyperformance.chameleon, pyperformance.django_template, pyperformance.fastapi, pyperformance.genshi/genshi_text, pyperformance.genshi/genshi_xml, pyperformance.html5lib, pyperformance.mako, pyperformance.tornado_http
- Card 2, pypy, hardware counts unavailable: flask.8.profile
- Card 2, pypy, pyperf worker exit 1: pyperformance.fastapi
- Card 2, pypy, unsettled: pyperformance.chameleon, pyperformance.django_template, pyperformance.docutils, pyperformance.genshi/genshi_text, pyperformance.genshi/genshi_xml, pyperformance.html5lib, pyperformance.mako, pyperformance.sphinx, pyperformance.tornado_http
- Card 4, cpython, excluded by corpus lock or missing oracle check: pyperformance.dask
- Card 4, lucifer, excluded by corpus lock or missing oracle check: pyperformance.dask
- Card 4, pypy, excluded by corpus lock or missing oracle check: pyperformance.dask
- Card 4, pypy, pyperf benchmark names differ from oracle: pyperformance.xml_etree
- Card 4, pypy, pyperf worker exit 1: pyperformance.sqlalchemy_declarative
- Card 5, cpython, unsettled: awfy.DeltaBlue, awfy.Json, awfy.List, awfy.Queens, pyperformance.argparse_subparsers, pyperformance.base64/base16_large, pyperformance.base64/base16_small, pyperformance.base64/base32_small, pyperformance.base64/base85_large, pyperformance.base64/base85_small, pyperformance.base64/urlsafe_base64_small, pyperformance.chameleon, pyperformance.chaos, pyperformance.coroutines, pyperformance.crypto_pyaes, pyperformance.deepcopy/deepcopy, pyperformance.deepcopy/deepcopy_reduce, pyperformance.deltablue, pyperformance.django_template, pyperformance.fannkuch, pyperformance.fastapi, pyperformance.genshi/genshi_text, pyperformance.genshi/genshi_xml, pyperformance.go, pyperformance.hexiom, pyperformance.html5lib, pyperformance.meteor_contest, pyperformance.nbody, pyperformance.pprint/pprint_pformat, pyperformance.raytrace, pyperformance.regex_compile, pyperformance.sphinx, pyperformance.sqlglot_v2, pyperformance.sqlglot_v2_optimize, pyperformance.sqlglot_v2_parse, pyperformance.sqlglot_v2_transpile, pyperformance.unpack_sequence, pyperformance.xdsl
- Card 5, go, unsettled: awfy.Bounce, awfy.CD, awfy.DeltaBlue, awfy.Json, awfy.List, awfy.Queens, awfy.Sieve, awfy.Storage, awfy.Towers
- Card 5, lucifer, unsettled: awfy.CD, awfy.DeltaBlue, awfy.Json, awfy.Queens, awfy.Sieve, awfy.Storage, awfy.Towers, flask.1, flask.8, pyperformance.argparse_subparsers, pyperformance.async_generators, pyperformance.base64/ascii85_small, pyperformance.base64/base16_large, pyperformance.base64/base16_small, pyperformance.base64/base32_small, pyperformance.base64/base64_large, pyperformance.base64/base64_small, pyperformance.base64/base85_large, pyperformance.base64/base85_small, pyperformance.base64/urlsafe_base64_small, pyperformance.chameleon, pyperformance.chaos, pyperformance.comprehensions, pyperformance.coroutines, pyperformance.crypto_pyaes, pyperformance.deepcopy/deepcopy, pyperformance.deepcopy/deepcopy_memo, pyperformance.deepcopy/deepcopy_reduce, pyperformance.deltablue, pyperformance.django_template, pyperformance.fastapi, pyperformance.genshi/genshi_text, pyperformance.genshi/genshi_xml, pyperformance.go, pyperformance.hexiom, pyperformance.html5lib, pyperformance.mako, pyperformance.meteor_contest, pyperformance.nqueens, pyperformance.pidigits, pyperformance.pprint/pprint_safe_repr, pyperformance.pyflate, pyperformance.raytrace, pyperformance.regex_compile, pyperformance.richards, pyperformance.sqlglot_v2, pyperformance.sqlglot_v2_optimize, pyperformance.sqlglot_v2_parse, pyperformance.sqlglot_v2_transpile, pyperformance.tornado_http, pyperformance.unpack_sequence, pyperformance.xdsl
- Card 5, pypy, pyperf stderr differs from oracle: pyperformance.regex_compile
- Card 5, pypy, pyperf worker exit 1: pyperformance.fastapi
- Card 5, pypy, unsettled: awfy.CD, awfy.DeltaBlue, awfy.Json, awfy.List, awfy.NBody, awfy.Permute, awfy.Richards, awfy.Storage, awfy.Towers, flask.8, pyperformance.argparse_subparsers, pyperformance.async_generators, pyperformance.base64/ascii85_large, pyperformance.base64/ascii85_small, pyperformance.base64/base16_large, pyperformance.base64/base16_small, pyperformance.base64/base32_large, pyperformance.base64/base32_small, pyperformance.base64/base64_large, pyperformance.base64/base64_small, pyperformance.base64/base85_small, pyperformance.base64/urlsafe_base64_small, pyperformance.chameleon, pyperformance.chaos, pyperformance.crypto_pyaes, pyperformance.deepcopy/deepcopy, pyperformance.deepcopy/deepcopy_memo, pyperformance.deepcopy/deepcopy_reduce, pyperformance.deltablue, pyperformance.django_template, pyperformance.docutils, pyperformance.generators, pyperformance.genshi/genshi_text, pyperformance.genshi/genshi_xml, pyperformance.go, pyperformance.hexiom, pyperformance.html5lib, pyperformance.mako, pyperformance.nbody, pyperformance.networkx, pyperformance.networkx_connected_components, pyperformance.networkx_k_core, pyperformance.pidigits, pyperformance.pprint/pprint_pformat, pyperformance.pprint/pprint_safe_repr, pyperformance.richards_super, pyperformance.spectral_norm, pyperformance.sphinx, pyperformance.sqlglot_v2, pyperformance.sqlglot_v2_optimize, pyperformance.sqlglot_v2_parse, pyperformance.sqlglot_v2_transpile, pyperformance.tornado_http, pyperformance.unpack_sequence, pyperformance.xdsl

The 9 measurement arms took 71.9 minutes including queue waits.
