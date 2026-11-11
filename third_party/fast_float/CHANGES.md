# fast_float

`runtime/prim/fast_float.c` is a C port of fast_float
(https://github.com/fastfloat/fast_float, commit 8ec59686), used under the MIT
license in `LICENSE` (upstream also offers Apache-2.0 and BSL-1.0).
`tests/fast_float.c` ports its double-parsing tests.

Ported: `decimal_to_binary.h` (Eisel-Lemire `compute_float`,
`compute_error`), `digit_comparison.h` and `bigint.h` (the big-integer slow
path), the Clinger fast path of `parse_number.h`, the `powers_of_five_128`
table, and the decimal grammar of `ascii_number.h`.

Changes:

- binary64 and `char` only; no templates, `float`, `float16`, wide characters,
  `from_chars_result` or `parse_options`.
- The entry point is `lu_fast_float_strtod(s, &end)` with `_Py_dg_strtod`'s
  contract instead of `from_chars`: NUL-terminated input; an optional leading
  `+`; no whitespace, `inf`, `nan` or hex (Python's callers handle those);
  `ERANGE` on overflow only; more than 10**9 significant or fractional digits
  is no number, and exponents saturate at 1,100,000,000, as in dtoa.c.
- Round to nearest is assumed (Python never changes the rounding mode), so the
  `rounds_to_nearest` check and the directed-rounding fast path are dropped.
- Digits are read one byte at a time: the input length is not known, so the
  eight-byte SWAR and SIMD loads are dropped.
- The leading-zero scan for long mantissas stops at the end of the digits.
- Big integers use 64-bit limbs, schoolbook multiplication into a scratch
  buffer, and abort if the fixed 4000-bit capacity were ever exceeded (it
  cannot be for 769 digits).
