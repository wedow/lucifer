No changes to `Lib/`.

Tools/peg_generator/pegen/ is copied from the same tag to generate
lib/compiler/parser.py. Modifications:

- Tools/peg_generator/pegen/python_generator.py: the generated Python parser
  follows the C generator's semantics, so that it parses exactly what the C
  parser does: items fail only when their call returns None (optional items
  always succeed, lookaheads are booleans); only rules marked `(memo)` and
  left-recursive leaders are memoized; alternatives that consist of a single
  `invalid_` rule run only in the second (error) pass, and `*_without_invalid`
  rules switch that pass off; keywords, soft keywords and exact tokens are
  matched by token type as in the C parser; `EXTRA` in an action expands to
  the node location the C parser computes (start of the rule's first token,
  end of the last non-whitespace token); default actions with several items
  return the parser's dummy name; generated methods take the parser as `p`.

Every future modification to this copy of CPython must be listed here, with a
brief summary as required by clause 3 of the PSF License. No files from Lib/
have been excluded.

## include/

`include/` (top level of this repository) is `Include/` from the same tag,
copied verbatim except that `Include/internal/` is excluded. `include/pyconfig.h`
is not part of `Include/`: it is the file `configure --disable-gil` generated
for the cp314t oracle build of this tag (`Py_GIL_DISABLED 1`). It contains no
paths. `include/LICENSE` is a copy of this directory's `LICENSE`.

## runtime/

C under `runtime/` adapted from this tag's sources, under the PSF License
(`LICENSE` in this directory):

- `runtime/interp/marshal.c` from `Python/marshal.c` (`r_object` and its
  helpers): reads from memory only; the reference list is a C array instead of
  a list; ints are built from digits directly instead of through
  `PyLongWriter`; version-0 floats parse with `strtod` instead of `dtoa.c`;
  dict items require exact-str keys until the kernel's dict exists; no audit
  event.
- `runtime/object/hash.c` from `Python/pyhash.c` (SipHash-1-3,
  `Py_HashBuffer`, `Py_HashPointer`, `_Py_HashDouble`), `Python/bootstrap_hash.c`
  (`lcg_urandom`, the seed policy over `getrandom`), `Objects/longobject.c`
  (`long_hash`) and the xxHash lanes of `Objects/tupleobject.c` and
  `Objects/sliceobject.c`: one function serves tuples and slices; no other
  algorithms are built.
- `runtime/object/set.c` from `Objects/setobject.c` (probing, resize policy,
  `frozenset_hash`): growth only, keys compared by layout equality until the
  kernel's rich comparison exists.
- `runtime/object/dict.h` and `runtime/object/dict.c` from
  `Include/internal/pycore_dict.h` and `Objects/dictobject.c` (the keys-table
  layout, unicode-keys lookup, insertion and resize): exact-str keys only.
  Deletion (`delitem_common`'s dummy index) and in-order iteration over the
  entries are added for the namespace primitives.
- `runtime/interp/call.c` from `Python/ceval.c` (`initialize_locals`,
  `missing_arguments`, `too_many_positional`, `positional_only_passed_as_keyword`,
  `_PyEval_FormatKwargsError`, `_Py_Check_ArgsIterable`), `Python/suggestions.c`
  (`levenshtein_distance`, `_Py_CalculateSuggestions`) and `Objects/object.c`
  (`_PyObject_FunctionStr`): messages are formatted with printf into a
  512-byte buffer and long lists of names are truncated; `*` operands are
  tuples and lists and `**` operands exact dicts until iteration and the
  mapping protocol exist.
