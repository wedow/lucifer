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
