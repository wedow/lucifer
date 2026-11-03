No changes to `Lib/`.

Every future modification to this copy of CPython must be listed here, with a
brief summary as required by clause 3 of the PSF License. No files from Lib/
have been excluded.

## include/

`include/` (top level of this repository) is `Include/` from the same tag,
copied verbatim except that `Include/internal/` is excluded. `include/pyconfig.h`
is not part of `Include/`: it is the file `configure --disable-gil` generated
for the cp314t oracle build of this tag (`Py_GIL_DISABLED 1`). It contains no
paths. `include/LICENSE` is a copy of this directory's `LICENSE`.
