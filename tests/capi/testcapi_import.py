# The oracle-built _testcapi and _testlimitedcapi import unmodified (make
# oracle-exts symlinks them beside the stdlib) and expose what the oracle
# exposes.
import _testcapi
import _testlimitedcapi

print("\n".join(sorted(dir(_testcapi))))
print("--- _testlimitedcapi")
print("\n".join(sorted(dir(_testlimitedcapi))))
