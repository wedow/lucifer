# PyErr_GivenExceptionMatches on a deeply nested tuple reports a
# RecursionError as unraisable and does not match, instead of overflowing
# the C stack (gh-156204); shallow nesting matches as before.
import sys
import _testcapi

seen = []
sys.unraisablehook = lambda u: seen.append(type(u.exc_value).__name__)
tup = (ValueError,)
for _ in range(50):
    tup = (tup,)
print(_testcapi.err_givenexceptionmatches(ValueError(), tup), seen)
for _ in range(500_000):
    tup = (tup,)
print(_testcapi.err_givenexceptionmatches(ValueError(), tup), seen)
print(_testcapi.err_givenexceptionmatches(KeyError(), (ValueError, (LookupError,))))
