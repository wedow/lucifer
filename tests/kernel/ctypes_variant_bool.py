# ctypes' 'v' type code (VARIANT_BOOL), which ctypes.wintypes defines on every platform, and the
# _ctypes constants test_ctypes imports at load time.
import ctypes
from ctypes import wintypes, sizeof

vb = wintypes.VARIANT_BOOL
print(vb._type_, sizeof(vb), ctypes.alignment(vb))
for value in (0, 1, -1, 2, True, False):
    v = vb(value)
    print(repr(value), v.value, bytes(v).hex())
print(wintypes.BYTE, wintypes.DWORD, wintypes.WORD)
import _ctypes
print(_ctypes.CTYPES_MAX_ARGCOUNT)
