"""mmap (Modules/mmapmodule.c): memory-mapped files and anonymous memory, over
libc's mmap, munmap, msync, mremap and madvise, called through the foreign
call primitive. The mapping is raw memory: indexing and slicing read and write
it through the same primitives ctypes uses, and the buffer protocol exports a
memoryview over it."""

import os
import sys
import _lucifer

PROT_NONE, PROT_READ, PROT_WRITE, PROT_EXEC = 0, 1, 2, 4
MAP_SHARED, MAP_PRIVATE = 1, 2
MAP_ANON = MAP_ANONYMOUS = 0x20
MAP_DENYWRITE = 0x800
MAP_EXECUTABLE = 0x1000
MAP_POPULATE = 0x8000
MAP_STACK = 0x20000
MAP_NORESERVE = 0x4000
ACCESS_DEFAULT, ACCESS_READ, ACCESS_WRITE, ACCESS_COPY = 0, 1, 2, 3
PAGESIZE = ALLOCATIONGRANULARITY = os.sysconf('SC_PAGE_SIZE')
(MADV_NORMAL, MADV_RANDOM, MADV_SEQUENTIAL, MADV_WILLNEED, MADV_DONTNEED, MADV_REMOVE, MADV_DONTFORK,
 MADV_DOFORK) = 0, 1, 2, 3, 4, 9, 10, 11
MADV_HWPOISON, MADV_MERGEABLE, MADV_UNMERGEABLE, MADV_HUGEPAGE, MADV_NOHUGEPAGE = 100, 12, 13, 14, 15
MADV_DONTDUMP, MADV_DODUMP, MADV_FREE = 16, 17, 8
MS_ASYNC, MS_SYNC, MS_INVALIDATE = 1, 4, 2
error = OSError

_P, _I, _Q, _V = ord('P'), ord('i'), ord('q'), ord('v')
_libc = _lucifer.ct_dlopen(None, 2)
_mmap = _lucifer.ct_dlsym(_libc, 'mmap')
_munmap = _lucifer.ct_dlsym(_libc, 'munmap')
_msync = _lucifer.ct_dlsym(_libc, 'msync')
_mremap = _lucifer.ct_dlsym(_libc, 'mremap')
_madvise = _lucifer.ct_dlsym(_libc, 'madvise')
_MAP_FAILED = 2**64 - 1


def _call(function, rtype, types, args):
    result = _lucifer.ct_call(function, rtype, types, args, True)
    return result


def _fail():
    errno = _lucifer.ct_errno(None)
    raise OSError(errno, os.strerror(errno))


def _index(value):
    try:
        return value.__index__()
    except AttributeError:
        raise TypeError("'%s' object cannot be interpreted as an integer" % type(value).__name__) from None


class mmap:
    """Windows: mmap(fileno, length[, tagname[, access[, offset]]])

Unix: mmap(fileno, length[, flags[, prot[, access[, offset[, trackfd]]]]])"""

    def __init__(self, fileno, length, flags=MAP_SHARED, prot=PROT_WRITE | PROT_READ, access=ACCESS_DEFAULT,
                 offset=0, *, trackfd=True):
        self._addr = 0
        self._size = 0
        self._pos = 0
        self._fd = -1
        self._closed = True
        fileno, length, offset = _index(fileno), _index(length), _index(offset)
        if length < 0:
            raise OverflowError('memory mapped length must be positive')
        if offset < 0:
            raise OverflowError('memory mapped offset must be positive')
        if access not in (ACCESS_DEFAULT, ACCESS_READ, ACCESS_WRITE, ACCESS_COPY):
            raise ValueError('mmap invalid access parameter.')
        if access != ACCESS_DEFAULT and (flags != MAP_SHARED or prot != PROT_WRITE | PROT_READ):
            raise ValueError("mmap can't specify both access and flags, prot.")
        if access == ACCESS_READ:
            flags, prot = MAP_SHARED, PROT_READ
        elif access == ACCESS_WRITE:
            flags, prot = MAP_SHARED, PROT_READ | PROT_WRITE
        elif access == ACCESS_COPY:
            flags, prot = MAP_PRIVATE, PROT_READ | PROT_WRITE
        elif access != ACCESS_DEFAULT:
            raise ValueError('mmap invalid access parameter.')
        self._readonly = access == ACCESS_READ or (access == ACCESS_DEFAULT and not prot & PROT_WRITE)
        self._copy = access == ACCESS_COPY or (access == ACCESS_DEFAULT and flags & MAP_PRIVATE != 0)
        if offset % ALLOCATIONGRANULARITY:
            raise OSError(22, 'Invalid argument')
        if fileno != -1 and fileno >= 0:
            stat = os.fstat(fileno)
            import stat as statmod
            if statmod.S_ISREG(stat.st_mode):
                if offset > stat.st_size:
                    raise ValueError('mmap offset is greater than file size')
                if length == 0:
                    if stat.st_size == 0:
                        raise ValueError('cannot mmap an empty file')
                    length = stat.st_size - offset
                elif offset + length > stat.st_size:
                    raise ValueError('mmap length is greater than file size')
            self._fd = os.dup(fileno) if trackfd else -1
        elif fileno == -1:
            flags |= MAP_ANONYMOUS
        address = _call(_mmap, _P, (_P, _P, _I, _I, _I, _Q), (0, length, prot, flags, fileno, offset))
        if address == _MAP_FAILED:
            if self._fd >= 0:
                os.close(self._fd)
            _fail()
        self._addr, self._size, self._closed = address, length, False
        self._offset = offset
        self._prot, self._flags = prot, flags

    def _check(self):
        if self._closed:
            raise ValueError('mmap closed or invalid')

    def _writable(self):
        self._check()
        if self._readonly:
            raise TypeError('mmap can\'t modify a readonly memory map.')

    def close(self):
        """Close the mmap.  Subsequent calls to other methods of the object will raise an exception."""
        if not self._closed:
            self._closed = True
            if self._fd >= 0:
                os.close(self._fd)
                self._fd = -1
            _call(_munmap, _I, (_P, _P), (self._addr, self._size))
            self._addr = self._size = 0

    @property
    def closed(self):
        """True if the mmap is closed."""
        return self._closed

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    def __enter__(self):
        self._check()
        return self

    def __exit__(self, *exc):
        self.close()

    def __len__(self):
        self._check()
        return self._size

    def size(self):
        """Return the size of the file."""
        self._check()
        return os.fstat(self._fd).st_size

    def tell(self):
        """Return the current position of the mmap object."""
        self._check()
        return self._pos

    def seek(self, pos, whence=0, /):
        """Set the file's current position."""
        self._check()
        pos = _index(pos)
        if whence == 0:
            new = pos
        elif whence == 1:
            new = self._pos + pos
        elif whence == 2:
            new = self._size + pos
        else:
            raise ValueError('unknown seek type')
        if not 0 <= new <= self._size:
            raise ValueError('seek out of range')
        self._pos = new

    def read(self, n=None, /):
        """Read n bytes from the mmap, or until the end if n is None or negative."""
        self._check()
        remaining = self._size - self._pos
        if n is None or n < 0 or n > remaining:
            n = remaining
        data = _lucifer.ct_read(self._addr + self._pos, n)
        self._pos += n
        return data

    def read_byte(self):
        """Read one byte."""
        self._check()
        if self._pos >= self._size:
            raise ValueError('read byte out of range')
        value = _lucifer.ct_read(self._addr + self._pos, 1)[0]
        self._pos += 1
        return value

    def readline(self):
        """Read until the next newline, or the end."""
        self._check()
        rest = _lucifer.ct_read(self._addr + self._pos, self._size - self._pos)
        end = rest.find(b'\n') + 1 or len(rest)
        self._pos += end
        return rest[:end]

    def write(self, data, /):
        """Write the bytes to the mmap and return the number written."""
        self._writable()
        view = memoryview(data).cast('B') if not isinstance(data, bytes) else data
        n = len(view)
        if self._size - self._pos < n:
            raise ValueError('data out of range')
        _lucifer.ct_write(self._addr + self._pos, view)
        self._pos += n
        return n

    def write_byte(self, byte, /):
        """Write one byte."""
        self._writable()
        if not isinstance(byte, int):
            raise TypeError("write_byte() argument must be int, not %s" % type(byte).__name__)
        if self._pos >= self._size:
            raise ValueError('write byte out of range')
        _lucifer.ct_write(self._addr + self._pos, bytes((byte & 255,)))
        self._pos += 1

    def _find(self, sub, start, end, reverse):
        self._check()
        sub = bytes(memoryview(sub))
        start = 0 if start is None else _index(start)
        end = self._size if end is None else _index(end)
        if start < 0:
            start = max(start + self._size, 0)
        if end < 0:
            end += self._size
        start, end = min(start, self._size), min(end, self._size)
        hay = _lucifer.ct_read(self._addr + start, max(end - start, 0))
        found = hay.rfind(sub) if reverse else hay.find(sub)
        return -1 if found < 0 else found + start

    def find(self, sub, start=None, end=None, /):
        """Return the lowest index in the mmap where the subsection is found."""
        return self._find(sub, start, end, False)

    def rfind(self, sub, start=None, end=None, /):
        """Return the highest index in the mmap where the subsection is found."""
        return self._find(sub, start, end, True)

    def flush(self, offset=0, size=None, /):
        """Flush changes to the file on disk."""
        self._check()
        size = self._size - offset if size is None else size
        if offset < 0 or size < 0 or offset + size > self._size:
            raise ValueError('flush values out of range')
        if self._readonly or self._copy:
            return None
        if _call(_msync, _I, (_P, _P, _I), (self._addr + offset, size, MS_SYNC)) < 0:
            _fail()

    def move(self, dest, src, count, /):
        """Copy count bytes from src to dest within the mmap."""
        self._writable()
        if min(dest, src, count) < 0 or max(dest, src) + count > self._size:
            raise ValueError('source, destination, or count out of range')
        _lucifer.ct_write(self._addr + dest, _lucifer.ct_read(self._addr + src, count))

    def resize(self, newsize, /):
        """Resize the map (and the underlying file, if any)."""
        self._check()
        if self._copy or self._readonly:
            raise TypeError('mmap can\'t resize a readonly or copy-on-write memory map.')
        if self._fd < 0 and self._flags & MAP_SHARED:
            raise ValueError("mmap: can't expand a shared anonymous mapping")
        if self._fd >= 0:
            os.ftruncate(self._fd, self._offset + newsize)
        address = _call(_mremap, _P, (_P, _P, _P, _I), (self._addr, self._size, newsize, 1))
        if address == _MAP_FAILED:
            _fail()
        self._addr, self._size = address, newsize
        self._pos = min(self._pos, newsize)

    def madvise(self, option, start=0, length=None, /):
        """Send advice option to the kernel about the memory region."""
        self._check()
        length = self._size - start if length is None else length
        if start < 0 or start > self._size:
            raise ValueError('madvise start out of bounds')
        if length < 0 or start + length > self._size:
            raise ValueError('madvise length invalid')
        if _call(_madvise, _I, (_P, _P, _I), (self._addr + start, length, option)) < 0:
            _fail()

    def __getitem__(self, index):
        self._check()
        if isinstance(index, slice):
            start, stop, step = index.indices(self._size)
            if step == 1:
                return _lucifer.ct_read(self._addr + start, max(stop - start, 0))
            return bytes(_lucifer.ct_read(self._addr + i, 1)[0] for i in range(start, stop, step))
        index = _index(index)
        if index < 0:
            index += self._size
        if not 0 <= index < self._size:
            raise IndexError('mmap index out of range')
        return _lucifer.ct_read(self._addr + index, 1)[0]

    def __setitem__(self, index, value):
        self._writable()
        if isinstance(index, slice):
            start, stop, step = index.indices(self._size)
            data = bytes(memoryview(value)) if not isinstance(value, int) else (_ for _ in ()).throw(
                TypeError('mmap slice assignment must be a bytes-like object'))
            indices = range(start, stop, step)
            if len(data) != len(indices):
                raise IndexError('mmap slice assignment is wrong size')
            if step == 1:
                _lucifer.ct_write(self._addr + start, data)
            else:
                for i, byte in zip(indices, data):
                    _lucifer.ct_write(self._addr + i, bytes((byte,)))
            return
        index = _index(index)
        if index < 0:
            index += self._size
        if not 0 <= index < self._size:
            raise IndexError('mmap index out of range')
        if not isinstance(value, int):
            raise TypeError('mmap assignment must be integer')
        if not 0 <= value < 256:
            raise ValueError('mmap item value must be in range(0, 256)')
        _lucifer.ct_write(self._addr + index, bytes((value,)))

    def __delitem__(self, index):
        raise TypeError('mmap object doesn\'t support item deletion')

    def __buffer__(self, flags, /):
        self._check()
        return _lucifer.ct_view(self._addr, self._size, self._readonly)

    def __repr__(self):
        if self._closed:
            return '<mmap.mmap closed=True>'
        access = 'ACCESS_READ' if self._readonly else 'ACCESS_COPY' if self._copy else 'ACCESS_WRITE'
        return '<mmap.mmap closed=False, access=%s, length=%d, pos=%d, offset=%d>' % (
            access, self._size, self._pos, self._offset)

    def __reduce__(self):
        raise TypeError('cannot pickle \'mmap.mmap\' object')


mmap.__module__ = 'mmap'
