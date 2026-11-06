"""_sre (Modules/_sre/sre.c, sre_lib.h): the engine behind Lib/re, as Python.

A compiled pattern is the list of code words Lib/re/_compiler.py emits. The
matcher runs them on a sequence of integers: a bytes object for bytes, and for
str a Latin-1 encoding of it, or a wrapper that reads code points where the
text has wider characters. It keeps CPython's explicit context stack, so
nesting costs heap, never Python frames: _match saves the caller's locals at a
jump and resumes it by label, as sre_lib.h resumes by goto."""

import sys
from _types import GenericAlias, MappingProxyType
from _lucifer import str_ord, str_slice

MAGIC = 20230612
CODESIZE = 4
MAXREPEAT = 0xFFFFFFFF
MAXGROUPS = 0x3FFFFFFF
copyright = ' SRE 2.2.2 Copyright (c) 1997-2002 by Secret Labs AB '

# Opcodes, positions, categories and flags: Modules/_sre/sre_constants.h,
# which Lib/re/_constants.py generates.
FAILURE = 0
SUCCESS = 1
ANY = 2
ANY_ALL = 3
ASSERT = 4
ASSERT_NOT = 5
AT = 6
BRANCH = 7
CATEGORY = 8
CHARSET = 9
BIGCHARSET = 10
GROUPREF = 11
GROUPREF_EXISTS = 12
IN = 13
INFO = 14
JUMP = 15
LITERAL = 16
MARK = 17
MAX_UNTIL = 18
MIN_UNTIL = 19
NOT_LITERAL = 20
NEGATE = 21
RANGE = 22
REPEAT = 23
REPEAT_ONE = 24
SUBPATTERN = 25
MIN_REPEAT_ONE = 26
ATOMIC_GROUP = 27
POSSESSIVE_REPEAT = 28
POSSESSIVE_REPEAT_ONE = 29
GROUPREF_IGNORE = 30
IN_IGNORE = 31
LITERAL_IGNORE = 32
NOT_LITERAL_IGNORE = 33
GROUPREF_LOC_IGNORE = 34
IN_LOC_IGNORE = 35
LITERAL_LOC_IGNORE = 36
NOT_LITERAL_LOC_IGNORE = 37
GROUPREF_UNI_IGNORE = 38
IN_UNI_IGNORE = 39
LITERAL_UNI_IGNORE = 40
NOT_LITERAL_UNI_IGNORE = 41
RANGE_UNI_IGNORE = 42

AT_BEGINNING = 0
AT_BEGINNING_LINE = 1
AT_BEGINNING_STRING = 2
AT_BOUNDARY = 3
AT_NON_BOUNDARY = 4
AT_END = 5
AT_END_LINE = 6
AT_END_STRING = 7
AT_LOC_BOUNDARY = 8
AT_LOC_NON_BOUNDARY = 9
AT_UNI_BOUNDARY = 10
AT_UNI_NON_BOUNDARY = 11

FLAG_IGNORECASE = 2
FLAG_LOCALE = 4
FLAG_MULTILINE = 8
FLAG_DOTALL = 16
FLAG_UNICODE = 32
FLAG_VERBOSE = 64
FLAG_DEBUG = 128
FLAG_ASCII = 256

INFO_PREFIX = 1
INFO_LITERAL = 2
INFO_CHARSET = 4

_SSIZE_MAX = sys.maxsize

# ---------------------------------------------------------------------------
# Character predicates. The locale ones are those of the C locale.

_ASCII_LOWER = bytes([c + 32 if 65 <= c <= 90 else c for c in range(128)])
_ASCII_UPPER = bytes([c - 32 if 97 <= c <= 122 else c for c in range(128)])
_ASCII_WORD = bytes([1 if (48 <= c <= 57 or 65 <= c <= 90 or 97 <= c <= 122 or c == 95) else 0 for c in range(128)])
_ASCII_ALNUM = bytes([1 if (48 <= c <= 57 or 65 <= c <= 90 or 97 <= c <= 122) else 0 for c in range(128)])
_ASCII_SPACE = bytes([1 if (9 <= c <= 13 or c == 32) else 0 for c in range(128)])
_LINEBREAKS = (10, 11, 12, 13, 0x1c, 0x1d, 0x1e, 0x85, 0x2028, 0x2029)

_lower_cache = {}
_upper_cache = {}


def _lower_ascii(ch):
    return _ASCII_LOWER[ch] if ch < 128 else ch


def _lower_unicode(ch):
    """Py_UNICODE_TOLOWER: the first code point of the full lower mapping."""
    if ch < 128:
        return _ASCII_LOWER[ch]
    r = _lower_cache.get(ch)
    if r is None:
        r = _lower_cache[ch] = ord(chr(ch).lower()[0])
    return r


def _upper_unicode(ch):
    if ch < 128:
        return _ASCII_UPPER[ch]
    r = _upper_cache.get(ch)
    if r is None:
        r = _upper_cache[ch] = ord(chr(ch).upper()[0])
    return r


def _lower_locale(ch):
    return _ASCII_LOWER[ch] if ch < 128 else ch


def _upper_locale(ch):
    return _ASCII_UPPER[ch] if ch < 128 else ch


def _char_loc_ignore(pattern, ch):
    return ch == pattern or _lower_locale(ch) == pattern or _upper_locale(ch) == pattern


def _uni_word(ch):
    if ch < 128:
        return _ASCII_WORD[ch] != 0
    return chr(ch).isalnum()


def _category(cat, ch):
    if cat == 0:
        return 48 <= ch <= 57
    if cat == 1:
        return not 48 <= ch <= 57
    if cat == 2:
        return ch < 128 and _ASCII_SPACE[ch] != 0
    if cat == 3:
        return not (ch < 128 and _ASCII_SPACE[ch] != 0)
    if cat == 4:
        return ch < 128 and _ASCII_WORD[ch] != 0
    if cat == 5:
        return not (ch < 128 and _ASCII_WORD[ch] != 0)
    if cat == 6:
        return ch == 10
    if cat == 7:
        return ch != 10
    if cat == 8:
        return ch < 128 and _ASCII_WORD[ch] != 0
    if cat == 9:
        return not (ch < 128 and _ASCII_WORD[ch] != 0)
    if cat == 10:
        return chr(ch).isdecimal()
    if cat == 11:
        return not chr(ch).isdecimal()
    if cat == 12:
        return chr(ch).isspace()
    if cat == 13:
        return not chr(ch).isspace()
    if cat == 14:
        return _uni_word(ch)
    if cat == 15:
        return not _uni_word(ch)
    if cat == 16:
        return ch in _LINEBREAKS
    if cat == 17:
        return ch not in _LINEBREAKS
    return False


def _word_at(text, i, kind):
    ch = text[i]
    if kind == 0:
        return ch < 128 and _ASCII_WORD[ch] != 0
    return _uni_word(ch)


def _at(text, ptr, end, at):
    """SRE(at): is ptr at the position `at` of the text [0, end)?"""
    if at == 0 or at == 2:
        return ptr == 0
    if at == 1:
        return ptr == 0 or text[ptr - 1] == 10
    if at == 5:
        return (end - ptr == 1 and text[ptr] == 10) or ptr == end
    if at == 6:
        return ptr == end or (ptr < len(text) and text[ptr] == 10)     # past end, if pos > endpos
    if at == 7:
        return ptr == end
    kind = 1 if at >= 10 else 0     # the locale words are the ASCII ones in the C locale
    that = _word_at(text, ptr - 1, kind) if ptr > 0 else False
    this = _word_at(text, ptr, kind) if ptr < end else False
    if at == 3 or at == 8 or at == 10:
        return this != that
    if at == 4 or at == 9 or at == 11:
        return this == that
    return False


def _charset(code, s, ch):
    """SRE(charset): is ch a member of the set whose code starts at s?"""
    ok = True
    while True:
        op = code[s]
        s += 1
        if op == FAILURE:
            return not ok
        if op == LITERAL:
            if ch == code[s]:
                return ok
            s += 1
        elif op == RANGE:
            if code[s] <= ch <= code[s + 1]:
                return ok
            s += 2
        elif op == CATEGORY:
            if _category(code[s], ch):
                return ok
            s += 1
        elif op == CHARSET:
            if ch < 256 and (code[s + (ch >> 5)] >> (ch & 31)) & 1:
                return ok
            s += 8
        elif op == NEGATE:
            ok = not ok
        elif op == BIGCHARSET:
            count = code[s]
            s += 1
            block = -1
            if ch < 0x10000:
                block = (code[s + (ch >> 10)] >> (((ch >> 8) & 3) * 8)) & 255
            s += 64
            if block >= 0 and (code[s + block * 8 + ((ch & 255) >> 5)] >> (ch & 31)) & 1:
                return ok
            s += count * 8
        elif op == RANGE_UNI_IGNORE:
            if code[s] <= ch <= code[s + 1]:
                return ok
            up = _upper_unicode(ch)
            if code[s] <= up <= code[s + 1]:
                return ok
            s += 2
        else:
            return False


def _charset_loc_ignore(code, s, ch):
    """Both locale cases of ch against every member, complementing the set last."""
    lo = _lower_locale(ch)
    up = _upper_locale(ch)
    if up == lo:
        return _charset(code, s, lo)
    ok = True
    while True:
        op = code[s]
        s += 1
        if op == FAILURE:
            return not ok
        if op == LITERAL:
            if lo == code[s] or up == code[s]:
                return ok
            s += 1
        elif op == CATEGORY:
            if _category(code[s], lo) or _category(code[s], up):
                return ok
            s += 1
        elif op == CHARSET:
            if (lo < 256 and (code[s + (lo >> 5)] >> (lo & 31)) & 1) or \
                    (up < 256 and (code[s + (up >> 5)] >> (up & 31)) & 1):
                return ok
            s += 8
        elif op == RANGE:
            if code[s] <= lo <= code[s + 1] or code[s] <= up <= code[s + 1]:
                return ok
            s += 2
        elif op == NEGATE:
            ok = not ok
        else:
            return False


# ---------------------------------------------------------------------------
# The subject text.

class _Wide:
    """The code points of a str that has characters beyond Latin-1."""

    __slots__ = ('s', 'n')

    def __init__(self, s):
        self.s = s
        self.n = len(s)

    def __len__(self):
        return self.n

    def __getitem__(self, i):
        return str_ord(self.s, i)

    def find(self, ch, start, end):
        s = self.s
        while start < end:
            if str_ord(s, start) == ch:
                return start
            start += 1
        return -1


_last_text = (None, None)


def _text_of(string):
    """The integer sequence of a str; the last long one is remembered, so that
    a scanner that calls match(string, pos) in a loop converts it once."""
    global _last_text
    long = len(string) > 1024
    if long:
        last = _last_text
        if last[0] is string:
            return last[1]
    try:
        text = str.encode(string, 'latin-1')
    except UnicodeEncodeError:
        text = _Wide(string)
    if long:
        _last_text = (string, text)
    return text


def _getstring(string):
    """getstring: (isbytes, data), the str itself or the bytes of a buffer."""
    if isinstance(string, str):
        return False, string
    if type(string) is bytes:
        return True, string
    try:
        return True, bytes(memoryview(string))
    except TypeError:
        pass
    raise TypeError("expected string or bytes-like object, got '%s'" % type(string).__name__) from None


def _ssize(value):
    if not isinstance(value, int):
        try:
            method = type(value).__index__
        except AttributeError:
            raise TypeError("'%s' object cannot be interpreted as an integer" % type(value).__name__) from None
        value = method(value)
    value = int.__index__(value)    # the value, as C reads it: no subclass's methods run
    if value > _SSIZE_MAX or value < -_SSIZE_MAX - 1:
        raise OverflowError("Python int too large to convert to C ssize_t")
    return value


class _Rep:
    """SRE_REPEAT: the state of one active REPEAT."""

    __slots__ = ('count', 'pattern', 'prev', 'last_ptr')


class _State:
    """SRE_STATE."""

    __slots__ = ('code', 'first', 'text', 'string', 'data', 'isbytes', 'start', 'end', 'ptr', 'pos', 'endpos',
                 'lastmark', 'lastindex', 'mark', 'match_all', 'must_advance')


def _state_init(pattern, string, start, end):
    isbytes, data = _getstring(string)
    if isbytes and pattern.isbytes == 0:
        raise TypeError("cannot use a string pattern on a bytes-like object")
    if not isbytes and pattern.isbytes > 0:
        raise TypeError("cannot use a bytes pattern on a string-like object")
    length = len(data)
    start = 0 if start < 0 else length if start > length else start
    end = 0 if end < 0 else length if end > length else end
    st = _State()
    st.code = pattern.code
    st.first = pattern._first
    st.text = data if isbytes else _text_of(data)
    st.string = string
    st.data = data
    st.isbytes = isbytes
    st.start = start
    st.end = end
    st.ptr = start
    st.pos = start
    st.endpos = end
    st.lastmark = -1
    st.lastindex = -1
    st.mark = [None] * (2 * pattern.groups)
    st.match_all = False
    st.must_advance = False
    return st


def _state_reset(st):
    st.lastmark = -1
    st.lastindex = -1


def _getslice(st, a, b):
    if st.isbytes:
        return st.data[a:b]
    return str_slice(st.data, a, b)


def _state_getslice(st, index, empty):
    index = (index - 1) * 2
    mark = st.mark
    if index >= st.lastmark or mark[index] is None or mark[index + 1] is None:
        if empty:
            return _getslice(st, 0, 0)
        return None
    i = mark[index]
    j = mark[index + 1]
    if i > j:
        raise SystemError("The span of capturing group is wrong, please report a bug for the re module.")
    return _getslice(st, i, j)


# ---------------------------------------------------------------------------
# The matcher.

# Labels of _match's state machine; the ones a jump resumes at double as the
# jump values of sre_lib.h.
_ENTER = 0
_OPS = 1
_EXIT = 2
_BRANCH_LOOP = 3
_BRANCH_RET = 4
_REPEAT_ONE_LOOP = 5
_REPEAT_ONE_RET = 6
_MIN_REPEAT_ONE_LOOP = 7
_MIN_REPEAT_ONE_RET = 8
_REPEAT_RET = 9
_MAX_UNTIL_1 = 10
_MAX_UNTIL_TRY = 11
_MAX_UNTIL_2 = 12
_MAX_UNTIL_TAIL = 13
_MAX_UNTIL_3 = 14
_MIN_UNTIL_1 = 15
_MIN_UNTIL_TAIL = 16
_MIN_UNTIL_2 = 17
_MIN_UNTIL_3 = 18
_POSS_MIN_LOOP = 19
_POSS_MIN_RET = 20
_POSS_LOOP = 21
_POSS_RET = 22
_POSS_DONE = 23
_ATOMIC_RET = 24
_ASSERT_RET = 25
_ASSERT_NOT_RET = 26


def _internal_error():
    raise RuntimeError("internal error in regular expression engine")


def _count(st, code, text, pattern, ptr, end, maxcount):
    """SRE(count): how many times the one-character item at code[pattern:]
    matches from ptr, at most maxcount."""
    start = ptr
    if maxcount < end - ptr and maxcount != MAXREPEAT:
        end = ptr + maxcount
    op = code[pattern]
    if op == IN:
        s = pattern + 2
        while ptr < end and _charset(code, s, text[ptr]):
            ptr += 1
    elif op == ANY:
        stop = text.find(10, ptr, end) if ptr < end else -1
        ptr = end if stop < 0 else stop
    elif op == ANY_ALL:
        ptr = end if end > ptr else ptr
    elif op == LITERAL:
        chr_ = code[pattern + 1]
        while ptr < end and text[ptr] == chr_:
            ptr += 1
    elif op == LITERAL_IGNORE:
        chr_ = code[pattern + 1]
        while ptr < end and _lower_ascii(text[ptr]) == chr_:
            ptr += 1
    elif op == LITERAL_UNI_IGNORE:
        chr_ = code[pattern + 1]
        while ptr < end and _lower_unicode(text[ptr]) == chr_:
            ptr += 1
    elif op == LITERAL_LOC_IGNORE:
        chr_ = code[pattern + 1]
        while ptr < end and _char_loc_ignore(chr_, text[ptr]):
            ptr += 1
    elif op == NOT_LITERAL:
        chr_ = code[pattern + 1]
        while ptr < end and text[ptr] != chr_:
            ptr += 1
    elif op == NOT_LITERAL_IGNORE:
        chr_ = code[pattern + 1]
        while ptr < end and _lower_ascii(text[ptr]) != chr_:
            ptr += 1
    elif op == NOT_LITERAL_UNI_IGNORE:
        chr_ = code[pattern + 1]
        while ptr < end and _lower_unicode(text[ptr]) != chr_:
            ptr += 1
    elif op == NOT_LITERAL_LOC_IGNORE:
        chr_ = code[pattern + 1]
        while ptr < end and not _char_loc_ignore(chr_, text[ptr]):
            ptr += 1
    elif op == IN_IGNORE:
        s = pattern + 2
        while ptr < end and _charset(code, s, _lower_ascii(text[ptr])):
            ptr += 1
    elif op == IN_UNI_IGNORE:
        s = pattern + 2
        while ptr < end and _charset(code, s, _lower_unicode(text[ptr])):
            ptr += 1
    elif op == IN_LOC_IGNORE:
        s = pattern + 2
        while ptr < end and _charset_loc_ignore(code, s, text[ptr]):
            ptr += 1
    else:
        st.ptr = ptr
        while st.ptr < end:
            if not _match(st, pattern, False):
                break
        return st.ptr - start
    return ptr - start


def _match(st, pattern, toplevel):
    """SRE(match): does the code at `pattern` match at st.ptr? Returns 1 and
    leaves the end in st.ptr, or 0."""
    code = st.code
    text = st.text
    end = st.end
    start = st.start
    mark = st.mark
    match_all = st.match_all
    must_advance = st.must_advance
    lastmark = st.lastmark
    lastindex = st.lastindex
    sptr = st.ptr                  # state->ptr
    repeat = None                  # state->repeat
    stack = []                     # the callers' contexts
    dstack = []                    # saved marks and last_ptr values
    ret = 0
    # The current context: toplevel, count, u (a literal or a _Rep), and the
    # lastmark and lastindex it saved.
    count = 0
    u = None
    clm = clx = 0
    ptr = 0
    label = _ENTER

    while True:
        if label == _ENTER:
            ptr = sptr
            if code[pattern] == INFO:
                need = code[pattern + 3]
                if need and 0 <= end - ptr < need:
                    ret = 0
                    label = _EXIT
                else:
                    pattern += code[pattern + 1] + 1
                    label = _OPS
            else:
                label = _OPS

        if label == _OPS:
            while True:
                op = code[pattern]
                pattern += 1
                if op == LITERAL:
                    if ptr >= end or text[ptr] != code[pattern]:
                        ret = 0
                        label = _EXIT
                        break
                    pattern += 1
                    ptr += 1
                elif op == MARK:
                    i = code[pattern]
                    if i & 1:
                        lastindex = i // 2 + 1
                    if i > lastmark:
                        j = lastmark + 1
                        while j < i:
                            mark[j] = None
                            j += 1
                        lastmark = i
                    mark[i] = ptr
                    pattern += 1
                elif op == IN:
                    if ptr >= end or not _charset(code, pattern + 1, text[ptr]):
                        ret = 0
                        label = _EXIT
                        break
                    pattern += code[pattern]
                    ptr += 1
                elif op == ANY:
                    if ptr >= end or text[ptr] == 10:
                        ret = 0
                        label = _EXIT
                        break
                    ptr += 1
                elif op == SUCCESS:
                    if toplevel and ((match_all and ptr != end) or (must_advance and ptr == start)):
                        ret = 0
                    else:
                        sptr = ptr
                        ret = 1
                    label = _EXIT
                    break
                elif op == NOT_LITERAL:
                    if ptr >= end or text[ptr] == code[pattern]:
                        ret = 0
                        label = _EXIT
                        break
                    pattern += 1
                    ptr += 1
                elif op == AT:
                    if not _at(text, ptr, end, code[pattern]):
                        ret = 0
                        label = _EXIT
                        break
                    pattern += 1
                elif op == CATEGORY:
                    if ptr >= end or not _category(code[pattern], text[ptr]):
                        ret = 0
                        label = _EXIT
                        break
                    pattern += 1
                    ptr += 1
                elif op == ANY_ALL:
                    if ptr >= end:
                        ret = 0
                        label = _EXIT
                        break
                    ptr += 1
                elif op == JUMP or op == INFO:
                    pattern += code[pattern]
                elif op == LITERAL_IGNORE:
                    if ptr >= end or _lower_ascii(text[ptr]) != code[pattern]:
                        ret = 0
                        label = _EXIT
                        break
                    pattern += 1
                    ptr += 1
                elif op == LITERAL_UNI_IGNORE:
                    if ptr >= end or _lower_unicode(text[ptr]) != code[pattern]:
                        ret = 0
                        label = _EXIT
                        break
                    pattern += 1
                    ptr += 1
                elif op == LITERAL_LOC_IGNORE:
                    if ptr >= end or not _char_loc_ignore(code[pattern], text[ptr]):
                        ret = 0
                        label = _EXIT
                        break
                    pattern += 1
                    ptr += 1
                elif op == NOT_LITERAL_IGNORE:
                    if ptr >= end or _lower_ascii(text[ptr]) == code[pattern]:
                        ret = 0
                        label = _EXIT
                        break
                    pattern += 1
                    ptr += 1
                elif op == NOT_LITERAL_UNI_IGNORE:
                    if ptr >= end or _lower_unicode(text[ptr]) == code[pattern]:
                        ret = 0
                        label = _EXIT
                        break
                    pattern += 1
                    ptr += 1
                elif op == NOT_LITERAL_LOC_IGNORE:
                    if ptr >= end or _char_loc_ignore(code[pattern], text[ptr]):
                        ret = 0
                        label = _EXIT
                        break
                    pattern += 1
                    ptr += 1
                elif op == IN_IGNORE:
                    if ptr >= end or not _charset(code, pattern + 1, _lower_ascii(text[ptr])):
                        ret = 0
                        label = _EXIT
                        break
                    pattern += code[pattern]
                    ptr += 1
                elif op == IN_UNI_IGNORE:
                    if ptr >= end or not _charset(code, pattern + 1, _lower_unicode(text[ptr])):
                        ret = 0
                        label = _EXIT
                        break
                    pattern += code[pattern]
                    ptr += 1
                elif op == IN_LOC_IGNORE:
                    if ptr >= end or not _charset_loc_ignore(code, pattern + 1, text[ptr]):
                        ret = 0
                        label = _EXIT
                        break
                    pattern += code[pattern]
                    ptr += 1
                elif op == BRANCH:
                    clm = lastmark
                    clx = lastindex
                    if repeat is not None and clm >= 0:
                        dstack.append(mark[:clm + 1])
                    label = _BRANCH_LOOP
                    break
                elif op == REPEAT_ONE:
                    # <REPEAT_ONE> <skip> <1=min> <2=max> item <SUCCESS> tail
                    if code[pattern + 1] > end - ptr:
                        ret = 0
                        label = _EXIT
                        break
                    count = _count(st, code, text, pattern + 3, ptr, end, code[pattern + 2])
                    ptr += count
                    if count < code[pattern + 1]:
                        ret = 0
                        label = _EXIT
                        break
                    tail = pattern + code[pattern]
                    if code[tail] == SUCCESS and ptr == end and not (toplevel and must_advance and ptr == start):
                        sptr = ptr
                        ret = 1
                        label = _EXIT
                        break
                    clm = lastmark
                    clx = lastindex
                    if repeat is not None and clm >= 0:
                        dstack.append(mark[:clm + 1])
                    u = code[tail + 1] if code[tail] == LITERAL else None
                    label = _REPEAT_ONE_LOOP
                    break
                elif op == MIN_REPEAT_ONE:
                    # <MIN_REPEAT_ONE> <skip> <1=min> <2=max> item <SUCCESS> tail
                    if code[pattern + 1] > end - ptr:
                        ret = 0
                        label = _EXIT
                        break
                    if code[pattern + 1] == 0:
                        count = 0
                    else:
                        count = _count(st, code, text, pattern + 3, ptr, end, code[pattern + 1])
                        if count < code[pattern + 1]:
                            ret = 0
                            label = _EXIT
                            break
                        ptr += count
                    if code[pattern + code[pattern]] == SUCCESS and not (
                            toplevel and ((match_all and ptr != end) or (must_advance and ptr == start))):
                        sptr = ptr
                        ret = 1
                        label = _EXIT
                        break
                    clm = lastmark
                    clx = lastindex
                    if repeat is not None and clm >= 0:
                        dstack.append(mark[:clm + 1])
                    label = _MIN_REPEAT_ONE_LOOP
                    break
                elif op == POSSESSIVE_REPEAT_ONE:
                    if ptr + code[pattern + 1] > end:
                        ret = 0
                        label = _EXIT
                        break
                    count = _count(st, code, text, pattern + 3, ptr, end, code[pattern + 2])
                    ptr += count
                    if count < code[pattern + 1]:
                        ret = 0
                        label = _EXIT
                        break
                    pattern += code[pattern]
                    if code[pattern] == SUCCESS and ptr == end and not (toplevel and must_advance and ptr == start):
                        sptr = ptr
                        ret = 1
                        label = _EXIT
                        break
                elif op == REPEAT:
                    # <REPEAT> <skip> <1=min> <2=max> <3=repeat_index> item <UNTIL> tail
                    u = _Rep()
                    u.count = -1
                    u.pattern = pattern
                    u.prev = repeat
                    u.last_ptr = None
                    repeat = u
                    sptr = ptr
                    stack.append((_REPEAT_RET, pattern, ptr, toplevel, count, u, clm, clx))
                    pattern += code[pattern]
                    label = _ENTER
                    break
                elif op == MAX_UNTIL:
                    u = repeat
                    if u is None:
                        _internal_error()
                    sptr = ptr
                    count = u.count + 1
                    if count < code[u.pattern + 1]:
                        u.count = count
                        stack.append((_MAX_UNTIL_1, pattern, ptr, toplevel, count, u, clm, clx))
                        pattern = u.pattern + 3
                        label = _ENTER
                    else:
                        label = _MAX_UNTIL_TRY
                    break
                elif op == MIN_UNTIL:
                    u = repeat
                    if u is None:
                        _internal_error()
                    sptr = ptr
                    count = u.count + 1
                    if count < code[u.pattern + 1]:
                        u.count = count
                        stack.append((_MIN_UNTIL_1, pattern, ptr, toplevel, count, u, clm, clx))
                        pattern = u.pattern + 3
                        label = _ENTER
                    else:
                        label = _MIN_UNTIL_TAIL
                    break
                elif op == POSSESSIVE_REPEAT:
                    # <POSSESSIVE_REPEAT> <skip> <1=min> <2=max> pattern <SUCCESS> tail
                    sptr = ptr
                    u = _Rep()
                    u.count = -1
                    u.pattern = None
                    u.prev = repeat
                    u.last_ptr = None
                    repeat = u
                    count = 0
                    label = _POSS_MIN_LOOP
                    break
                elif op == ATOMIC_GROUP:
                    # <ATOMIC_GROUP> <skip> pattern <SUCCESS> tail
                    sptr = ptr
                    stack.append((_ATOMIC_RET, pattern, ptr, toplevel, count, u, clm, clx))
                    pattern += 1
                    toplevel = False
                    label = _ENTER
                    break
                elif op == GROUPREF or op == GROUPREF_IGNORE or op == GROUPREF_UNI_IGNORE or op == GROUPREF_LOC_IGNORE:
                    g = code[pattern] * 2
                    if g >= lastmark:
                        ret = 0
                        label = _EXIT
                        break
                    p = mark[g]
                    e = mark[g + 1]
                    if p is None or e is None or e < p:
                        ret = 0
                        label = _EXIT
                        break
                    if op == GROUPREF:
                        while p < e:
                            if ptr >= end or text[ptr] != text[p]:
                                break
                            p += 1
                            ptr += 1
                    elif op == GROUPREF_IGNORE:
                        while p < e:
                            if ptr >= end or _lower_ascii(text[ptr]) != _lower_ascii(text[p]):
                                break
                            p += 1
                            ptr += 1
                    elif op == GROUPREF_UNI_IGNORE:
                        while p < e:
                            if ptr >= end or _lower_unicode(text[ptr]) != _lower_unicode(text[p]):
                                break
                            p += 1
                            ptr += 1
                    else:
                        while p < e:
                            if ptr >= end or _lower_locale(text[ptr]) != _lower_locale(text[p]):
                                break
                            p += 1
                            ptr += 1
                    if p < e:
                        ret = 0
                        label = _EXIT
                        break
                    pattern += 1
                elif op == GROUPREF_EXISTS:
                    # <GROUPREF_EXISTS> <group> <skip> codeyes <JUMP> codeno ...
                    g = code[pattern] * 2
                    if g >= lastmark:
                        pattern += code[pattern + 1]
                    else:
                        p = mark[g]
                        e = mark[g + 1]
                        if p is None or e is None or e < p:
                            pattern += code[pattern + 1]
                        else:
                            pattern += 2
                elif op == ASSERT:
                    # <ASSERT> <skip> <back> <pattern>
                    if ptr < code[pattern + 1]:
                        ret = 0
                        label = _EXIT
                        break
                    sptr = ptr - code[pattern + 1]
                    stack.append((_ASSERT_RET, pattern, ptr, toplevel, count, u, clm, clx))
                    pattern += 2
                    toplevel = False
                    label = _ENTER
                    break
                elif op == ASSERT_NOT:
                    if ptr >= code[pattern + 1]:
                        sptr = ptr - code[pattern + 1]
                        clm = lastmark
                        clx = lastindex
                        if repeat is not None and clm >= 0:
                            dstack.append(mark[:clm + 1])
                        stack.append((_ASSERT_NOT_RET, pattern, ptr, toplevel, count, u, clm, clx))
                        pattern += 2
                        toplevel = False
                        label = _ENTER
                        break
                    pattern += code[pattern]
                elif op == FAILURE:
                    ret = 0
                    label = _EXIT
                    break
                else:
                    _internal_error()

        if label == _BRANCH_LOOP:
            # pattern is at the <skip> of the next alternative
            while code[pattern]:
                first = code[pattern + 1]
                if first == LITERAL and (ptr >= end or text[ptr] != code[pattern + 2]):
                    pattern += code[pattern]
                    continue
                if first == IN and (ptr >= end or not _charset(code, pattern + 3, text[ptr])):
                    pattern += code[pattern]
                    continue
                sptr = ptr
                stack.append((_BRANCH_RET, pattern, ptr, toplevel, count, u, clm, clx))
                pattern += 1
                label = _ENTER
                break
            else:
                if repeat is not None and clm >= 0:
                    dstack.pop()
                ret = 0
                label = _EXIT

        if label == _BRANCH_RET:
            if ret:
                if repeat is not None and clm >= 0:
                    dstack.pop()
                label = _EXIT
            else:
                if repeat is not None and clm >= 0:
                    mark[:clm + 1] = dstack[-1]
                lastmark = clm
                lastindex = clx
                pattern += code[pattern]
                label = _BRANCH_LOOP
                continue

        if label == _REPEAT_ONE_LOOP:
            # count matches of the item are taken; u is a literal the tail starts with, or None
            minimum = code[pattern + 1]
            tail = pattern + code[pattern]
            if u is not None:
                while count >= minimum and (ptr >= end or text[ptr] != u):
                    ptr -= 1
                    count -= 1
            if count >= minimum:
                sptr = ptr
                stack.append((_REPEAT_ONE_RET, pattern, ptr, toplevel, count, u, clm, clx))
                pattern = tail
                label = _ENTER
            else:
                if repeat is not None and clm >= 0:
                    dstack.pop()
                ret = 0
                label = _EXIT

        if label == _REPEAT_ONE_RET:
            if ret:
                if repeat is not None and clm >= 0:
                    dstack.pop()
                label = _EXIT
            else:
                if repeat is not None and clm >= 0:
                    mark[:clm + 1] = dstack[-1]
                lastmark = clm
                lastindex = clx
                ptr -= 1
                count -= 1
                label = _REPEAT_ONE_LOOP
                continue

        if label == _MIN_REPEAT_ONE_LOOP:
            maximum = code[pattern + 2]
            if maximum == MAXREPEAT or count <= maximum:
                sptr = ptr
                stack.append((_MIN_REPEAT_ONE_RET, pattern, ptr, toplevel, count, u, clm, clx))
                pattern += code[pattern]
                label = _ENTER
            else:
                if repeat is not None and clm >= 0:
                    dstack.pop()
                ret = 0
                label = _EXIT

        if label == _MIN_REPEAT_ONE_RET:
            if ret:
                if repeat is not None and clm >= 0:
                    dstack.pop()
                label = _EXIT
            else:
                if repeat is not None and clm >= 0:
                    mark[:clm + 1] = dstack[-1]
                lastmark = clm
                lastindex = clx
                if _count(st, code, text, pattern + 3, ptr, end, 1) == 0:
                    if repeat is not None and clm >= 0:
                        dstack.pop()
                    ret = 0
                    label = _EXIT
                else:
                    ptr += 1
                    count += 1
                    label = _MIN_REPEAT_ONE_LOOP
                    continue

        if label == _REPEAT_RET:
            repeat = u.prev
            label = _EXIT

        if label == _MAX_UNTIL_1:
            if ret:
                label = _EXIT
            else:
                u.count = count - 1
                sptr = ptr
                ret = 0
                label = _EXIT

        if label == _MAX_UNTIL_TRY:
            rp = u.pattern
            maximum = code[rp + 2]
            if (count < maximum or maximum == MAXREPEAT) and sptr != u.last_ptr:
                # we may have enough matches, but if we can match another item, do so
                u.count = count
                clm = lastmark
                clx = lastindex
                if clm >= 0:
                    dstack.append(mark[:clm + 1])
                dstack.append(u.last_ptr)
                u.last_ptr = sptr
                stack.append((_MAX_UNTIL_2, pattern, ptr, toplevel, count, u, clm, clx))
                pattern = rp + 3
                label = _ENTER
            else:
                label = _MAX_UNTIL_TAIL

        if label == _MAX_UNTIL_2:
            u.last_ptr = dstack.pop()
            if ret:
                if clm >= 0:
                    dstack.pop()
                label = _EXIT
            else:
                if clm >= 0:
                    mark[:clm + 1] = dstack.pop()
                lastmark = clm
                lastindex = clx
                u.count = count - 1
                sptr = ptr
                label = _MAX_UNTIL_TAIL

        if label == _MAX_UNTIL_TAIL:
            # cannot match more repeated items here; make sure the tail matches
            repeat = u.prev
            stack.append((_MAX_UNTIL_3, pattern, ptr, toplevel, count, u, clm, clx))
            label = _ENTER

        if label == _MAX_UNTIL_3:
            repeat = u
            if not ret:
                sptr = ptr
            label = _EXIT

        if label == _MIN_UNTIL_1:
            if not ret:
                u.count = count - 1
                sptr = ptr
            label = _EXIT

        if label == _MIN_UNTIL_TAIL:
            # see if the tail matches
            repeat = u.prev
            clm = lastmark
            clx = lastindex
            if repeat is not None and clm >= 0:
                dstack.append(mark[:clm + 1])
            stack.append((_MIN_UNTIL_2, pattern, ptr, toplevel, count, u, clm, clx))
            label = _ENTER

        if label == _MIN_UNTIL_2:
            repeat_of_tail = repeat
            repeat = u
            if ret:
                if repeat_of_tail is not None and clm >= 0:
                    dstack.pop()
                label = _EXIT
            else:
                if repeat_of_tail is not None and clm >= 0:
                    mark[:clm + 1] = dstack.pop()
                lastmark = clm
                lastindex = clx
                sptr = ptr
                rp = u.pattern
                maximum = code[rp + 2]
                if (count >= maximum and maximum != MAXREPEAT) or sptr == u.last_ptr:
                    ret = 0
                    label = _EXIT
                else:
                    u.count = count
                    dstack.append(u.last_ptr)
                    u.last_ptr = sptr
                    stack.append((_MIN_UNTIL_3, pattern, ptr, toplevel, count, u, clm, clx))
                    pattern = rp + 3
                    label = _ENTER

        if label == _MIN_UNTIL_3:
            u.last_ptr = dstack.pop()
            if not ret:
                u.count = count - 1
                sptr = ptr
            label = _EXIT

        if label == _POSS_MIN_LOOP:
            if count < code[pattern + 1]:
                stack.append((_POSS_MIN_RET, pattern, ptr, toplevel, count, u, clm, clx))
                pattern += 3
                toplevel = False
                label = _ENTER
            else:
                # clear ptr so that the loop below is entered
                ptr = -1
                label = _POSS_LOOP

        if label == _POSS_MIN_RET:
            if ret:
                count += 1
                label = _POSS_MIN_LOOP
                continue
            sptr = ptr
            repeat = u.prev
            ret = 0
            label = _EXIT

        if label == _POSS_LOOP:
            maximum = code[pattern + 2]
            if (count < maximum or maximum == MAXREPEAT) and sptr != ptr:
                clm = lastmark
                clx = lastindex
                if clm >= 0:
                    dstack.append(mark[:clm + 1])
                # zero-width match protection: ptr is where this attempt starts
                ptr = sptr
                stack.append((_POSS_RET, pattern, ptr, toplevel, count, u, clm, clx))
                pattern += 3
                toplevel = False
                label = _ENTER
            else:
                label = _POSS_DONE

        if label == _POSS_RET:
            if ret:
                if clm >= 0:
                    dstack.pop()
                count += 1
                label = _POSS_LOOP
                continue
            if clm >= 0:
                mark[:clm + 1] = dstack.pop()
            lastmark = clm
            lastindex = clx
            sptr = ptr
            label = _POSS_DONE

        if label == _POSS_DONE:
            repeat = u.prev
            # evaluate the tail: skip the SUCCESS that follows the item
            pattern += code[pattern] + 1
            ptr = sptr
            label = _OPS
            continue

        if label == _ATOMIC_RET:
            if not ret:
                sptr = ptr
                label = _EXIT
            else:
                pattern += code[pattern]
                ptr = sptr
                label = _OPS
                continue

        if label == _ASSERT_RET:
            if not ret:
                label = _EXIT
            else:
                pattern += code[pattern]
                label = _OPS
                continue

        if label == _ASSERT_NOT_RET:
            if ret:
                if repeat is not None and clm >= 0:
                    dstack.pop()
                ret = 0
                label = _EXIT
            else:
                if repeat is not None and clm >= 0:
                    mark[:clm + 1] = dstack.pop()
                lastmark = clm
                lastindex = clx
                pattern += code[pattern]
                label = _OPS
                continue

        if label == _EXIT:
            if not stack:
                st.ptr = sptr
                st.lastmark = lastmark
                st.lastindex = lastindex
                return ret
            label, pattern, ptr, toplevel, count, u, clm, clx = stack.pop()


def _search_prefix(text, prefix, ptr, end):
    """The first position in [ptr, end) where the code points of prefix start."""
    first = prefix[0]
    n = len(prefix)
    if first > 255 and type(text) is bytes:
        return -1               # the literal does not fit the character width
    while True:
        i = text.find(first, ptr, end - n + 1)
        if i < 0:
            return -1
        j = 1
        while j < n and text[i + j] == prefix[j]:
            j += 1
        if j == n:
            return i
        ptr = i + 1


def _first_test(code, i):
    """(op, where) for the one-character test at code[i] (past any MARK) that
    every match from there begins with, or None."""
    while code[i] == MARK:
        i += 2
    op = code[i]
    if op == LITERAL or op == NOT_LITERAL:
        return op, i + 1
    if op == IN:
        return op, i + 2
    if (op == REPEAT_ONE or op == MIN_REPEAT_ONE or op == POSSESSIVE_REPEAT_ONE) and code[i + 2] >= 1:
        return _first_test(code, i + 4)
    return None


def _first_tests(code):
    """The one-character tests the first character of every match passes one
    of, where the INFO block names no literal prefix or charset: a branch of
    alternatives that each begin with one, or a repeat of one at least once.
    Lib/re/_compiler.py derives a charset only from a branch of literals, so
    without these _search would try a match at every position. None when
    there are none."""
    if code[0] != INFO or code[2] & (INFO_PREFIX | INFO_CHARSET):
        return None
    i = 1 + code[1]
    while code[i] == MARK:
        i += 2
    if code[i] != BRANCH:
        test = _first_test(code, i)
        return None if test is None else (test,)
    tests = []
    i += 1
    while code[i]:
        test = _first_test(code, i + 1)
        if test is None:
            return None
        tests.append(test)
        i += code[i]
    return tuple(tests)


def _starts(code, tests, ch):
    """Does ch pass one of the first-character tests?"""
    for op, at in tests:
        if op == IN:
            if _charset(code, at, ch):
                return True
        elif (ch == code[at]) == (op == LITERAL):
            return True
    return False


def _search(st):
    """SRE(search): the leftmost match from st.start; 1 with the span in
    st.start and st.ptr, or 0."""
    code = st.code
    text = st.text
    ptr = st.start
    end = st.end
    pattern = 0
    prefix = None
    charset = -1
    flags = 0
    prefix_skip = 0
    if ptr > end:
        return 0
    if code[0] == INFO:
        flags = code[2]
        need = code[3]
        if need and end - ptr < need:
            return 0
        if need > 1:
            # leave at least one character so that literal search works
            end -= need - 1
            if end <= ptr:
                end = ptr
        if flags & INFO_PREFIX:
            prefix_len = code[5]
            prefix_skip = code[6]
            prefix = code[7:7 + prefix_len]
        elif flags & INFO_CHARSET:
            charset = 5
        pattern = 1 + code[1]
    first = st.first

    if prefix is not None:
        # the pattern starts with a literal prefix
        end = st.end
        st.must_advance = False
        while ptr < end:
            i = _search_prefix(text, prefix, ptr, end)
            if i < 0:
                return 0
            st.start = i
            st.ptr = i + prefix_skip
            if flags & INFO_LITERAL:
                return 1
            if _match(st, pattern + 2 * prefix_skip, False):
                return 1
            ptr = i + 1
            _state_reset(st)
        return 0

    status = 0
    if charset >= 0 or first is not None:
        # the pattern starts with a character from a known set, or one that
        # passes one of the first-character tests
        end = st.end
        st.must_advance = False
        while True:
            if charset >= 0:
                while ptr < end and not _charset(code, charset, text[ptr]):
                    ptr += 1
            else:
                while ptr < end and not _starts(code, first, text[ptr]):
                    ptr += 1
            if ptr >= end:
                return 0
            st.start = ptr
            st.ptr = ptr
            status = _match(st, pattern, False)
            if status:
                break
            ptr += 1
            _state_reset(st)
    else:
        # general case
        st.start = st.ptr = ptr
        status = _match(st, pattern, True)
        st.must_advance = False
        if not status and code[pattern] == AT and (code[pattern + 1] == AT_BEGINNING or code[pattern + 1] == AT_BEGINNING_STRING):
            st.start = st.ptr = end
            return 0
        while not status and ptr < end:
            ptr += 1
            _state_reset(st)
            st.start = st.ptr = ptr
            status = _match(st, pattern, False)
    return status


# ---------------------------------------------------------------------------
# Code validation. The code is position independent: every jump goes forward,
# and the target of a later jump is earlier than that of an earlier one. See
# _compile() in Lib/re/_compiler.py for the patterns checked here.

class _Invalid(Exception):
    pass


def _validate_charset(code, i, end):
    while i < end:
        op = code[i]
        i += 1
        if op == NEGATE:
            pass
        elif op == LITERAL:
            if i >= end:
                raise _Invalid
            i += 1
        elif op == RANGE or op == RANGE_UNI_IGNORE:
            if i + 1 >= end:
                raise _Invalid
            i += 2
        elif op == CHARSET:
            if 8 > end - i:
                raise _Invalid
            i += 8
        elif op == BIGCHARSET:
            if i >= end:
                raise _Invalid
            blocks = code[i]
            i += 1
            if 64 > end - i:
                raise _Invalid
            for k in range(256):
                if (code[i + (k >> 2)] >> ((k & 3) * 8)) & 255 >= blocks:
                    raise _Invalid
            i += 64
            if blocks * 8 > end - i:
                raise _Invalid
            i += blocks * 8
        elif op == CATEGORY:
            if i >= end or code[i] > 17:
                raise _Invalid
            i += 1
        else:
            raise _Invalid


def _validate_inner(code, i, end, groups):
    """Returns 0, or 1 if the last op is a JUMP; raises _Invalid."""
    if i > end:
        raise _Invalid
    while i < end:
        op = code[i]
        i += 1
        if op == MARK:
            if i >= end or code[i] >= 2 * groups:
                raise _Invalid
            i += 1
        elif op in (LITERAL, NOT_LITERAL, LITERAL_IGNORE, NOT_LITERAL_IGNORE, LITERAL_UNI_IGNORE,
                    NOT_LITERAL_UNI_IGNORE, LITERAL_LOC_IGNORE, NOT_LITERAL_LOC_IGNORE):
            if i >= end:
                raise _Invalid
            i += 1
        elif op == SUCCESS or op == FAILURE or op == ANY or op == ANY_ALL:
            pass
        elif op == AT:
            if i >= end or code[i] > 11:
                raise _Invalid
            i += 1
        elif op in (IN, IN_IGNORE, IN_UNI_IGNORE, IN_LOC_IGNORE):
            skip = _get_skip(code, i, end, 0)
            i += 1
            _validate_charset(code, i, i + skip - 2)
            if code[i + skip - 2] != FAILURE:
                raise _Invalid
            i += skip - 1
        elif op == INFO:
            # <INFO> <1=skip> <2=flags> <3=min> <4=max> and a prefix or a charset
            skip = _get_skip(code, i, end, 0)
            i += 1
            newcode = i + skip - 1
            if i + 2 >= end:
                raise _Invalid
            flags = code[i]
            i += 3
            if flags & ~(INFO_PREFIX | INFO_LITERAL | INFO_CHARSET):
                raise _Invalid
            if flags & INFO_PREFIX and flags & INFO_CHARSET:
                raise _Invalid
            if flags & INFO_LITERAL and not flags & INFO_PREFIX:
                raise _Invalid
            if flags & INFO_PREFIX:
                if i + 1 >= end:
                    raise _Invalid
                prefix_len = code[i]
                i += 2
                if prefix_len > newcode - i:
                    raise _Invalid
                i += prefix_len
                if prefix_len > newcode - i:
                    raise _Invalid
                for k in range(prefix_len):
                    if code[i + k] >= prefix_len:
                        raise _Invalid
                i += prefix_len
            if flags & INFO_CHARSET:
                _validate_charset(code, i, newcode - 1)
                if code[newcode - 1] != FAILURE:
                    raise _Invalid
                i = newcode
            elif i != newcode:
                raise _Invalid
        elif op == BRANCH:
            target = None
            while True:
                skip = _get_skip(code, i, end, 0)
                i += 1
                if skip == 0:
                    break
                if _validate_inner(code, i, i + skip - 3, groups):
                    raise _Invalid
                i += skip - 3
                if i >= end or code[i] != JUMP:
                    raise _Invalid
                i += 1
                skip = _get_skip(code, i, end, 0)
                i += 1
                if target is None:
                    target = i + skip - 1
                elif i + skip - 1 != target:
                    raise _Invalid
            if target is None or i != target:
                raise _Invalid
        elif op == REPEAT_ONE or op == MIN_REPEAT_ONE or op == POSSESSIVE_REPEAT_ONE:
            skip = _get_skip(code, i, end, 0)
            i += 1
            if i + 1 >= end:
                raise _Invalid
            low = code[i]
            high = code[i + 1]
            i += 2
            if low > high or high > MAXREPEAT:
                raise _Invalid
            if _validate_inner(code, i, i + skip - 4, groups):
                raise _Invalid
            i += skip - 4
            if i >= end or code[i] != SUCCESS:
                raise _Invalid
            i += 1
        elif op == REPEAT or op == POSSESSIVE_REPEAT:
            skip = _get_skip(code, i, end, 0)
            i += 1
            if i + 1 >= end:
                raise _Invalid
            low = code[i]
            high = code[i + 1]
            i += 2
            if low > high or high > MAXREPEAT:
                raise _Invalid
            if _validate_inner(code, i, i + skip - 3, groups):
                raise _Invalid
            i += skip - 3
            if i >= end:
                raise _Invalid
            last = code[i]
            i += 1
            if op == POSSESSIVE_REPEAT:
                if last != SUCCESS:
                    raise _Invalid
            elif last != MAX_UNTIL and last != MIN_UNTIL:
                raise _Invalid
        elif op == ATOMIC_GROUP:
            skip = _get_skip(code, i, end, 0)
            i += 1
            if _validate_inner(code, i, i + skip - 2, groups):
                raise _Invalid
            i += skip - 2
            if i >= end or code[i] != SUCCESS:
                raise _Invalid
            i += 1
        elif op in (GROUPREF, GROUPREF_IGNORE, GROUPREF_UNI_IGNORE, GROUPREF_LOC_IGNORE):
            if i >= end or code[i] >= groups:
                raise _Invalid
            i += 1
        elif op == GROUPREF_EXISTS:
            # <group> <skip> then-part [<JUMP> <skip> else-part]; the skip is relative to the group
            if i >= end or code[i] >= groups:
                raise _Invalid
            i += 1
            skip = _get_skip(code, i, end, 1)
            target = i + skip - 1
            rc = _validate_inner(code, i + 1, target, groups)
            if rc == 1:
                # the then-part ends with JUMP <skip>: the else-part follows it
                skip = _get_skip(code, target - 1, end, 0)
                rc = _validate_inner(code, target, target - 1 + skip, groups)
                target += skip - 1
            if rc:
                raise _Invalid
            i = target
        elif op == ASSERT or op == ASSERT_NOT:
            skip = _get_skip(code, i, end, 0)
            i += 2
            if i - 1 >= end:
                raise _Invalid
            if _validate_inner(code, i, i + skip - 3, groups):
                raise _Invalid
            i += skip - 3
            if i >= end or code[i] != SUCCESS:
                raise _Invalid
            i += 1
        elif op == JUMP:
            if i + 1 != end:
                raise _Invalid
            return 1
        else:
            raise _Invalid
    return 0


def _get_skip(code, i, end, adjust):
    if i >= end:
        raise _Invalid
    skip = code[i]
    if skip - adjust > end - i or skip < adjust:
        raise _Invalid
    return skip


def _validate(code, groups):
    if groups < 0 or groups > MAXGROUPS or not code or code[-1] != SUCCESS:
        raise _Invalid
    _validate_inner(code, 0, len(code) - 1, groups)


# ---------------------------------------------------------------------------
# Pattern, Match, Scanner, Template.

def _no_instances(cls):
    raise TypeError("cannot create '%s.%s' instances" % (cls.__module__, cls.__name__))


class Pattern:
    """Compiled regular expression object."""

    __module__ = 're'
    __slots__ = ('pattern', 'flags', 'groups', 'code', '_first', 'isbytes', '_groupindex', '_indexgroup', '__weakref__')

    def __new__(cls, *args, **kwargs):
        _no_instances(cls)

    @property
    def groupindex(self):
        """A dictionary mapping group names to group numbers."""
        if self._groupindex is None:
            return {}
        return MappingProxyType(self._groupindex)

    def match(self, /, string, pos=0, endpos=_SSIZE_MAX):
        """Matches zero or more characters at the beginning of the string."""
        st = _state_init(self, string, _ssize(pos), _ssize(endpos))
        return _new_match(self, st, _match(st, 0, True))

    def fullmatch(self, /, string, pos=0, endpos=_SSIZE_MAX):
        """Matches against all of the string."""
        st = _state_init(self, string, _ssize(pos), _ssize(endpos))
        st.match_all = True
        return _new_match(self, st, _match(st, 0, True))

    def search(self, /, string, pos=0, endpos=_SSIZE_MAX):
        """Scan through string looking for a match, and return a corresponding match object instance.

Return None if no position in the string matches."""
        st = _state_init(self, string, _ssize(pos), _ssize(endpos))
        return _new_match(self, st, _search(st))

    def findall(self, /, string, pos=0, endpos=_SSIZE_MAX):
        """Return a list of all non-overlapping matches of pattern in string."""
        st = _state_init(self, string, _ssize(pos), _ssize(endpos))
        result = []
        groups = self.groups
        while st.start <= st.end:
            _state_reset(st)
            st.ptr = st.start
            if not _search(st):
                break
            if groups == 0:
                item = _getslice(st, st.start, st.ptr)
            elif groups == 1:
                item = _state_getslice(st, 1, True)
            else:
                item = tuple([_state_getslice(st, i + 1, True) for i in range(groups)])
            result.append(item)
            st.must_advance = st.ptr == st.start
            st.start = st.ptr
        return result

    def finditer(self, /, string, pos=0, endpos=_SSIZE_MAX):
        """Return an iterator over all non-overlapping matches for the RE pattern in string.

For each match, the iterator returns a match object."""
        return iter(_scanner(self, string, pos, endpos).search, None)

    def scanner(self, /, string, pos=0, endpos=_SSIZE_MAX):
        return _scanner(self, string, pos, endpos)

    def split(self, /, string, maxsplit=0):
        """Split string by the occurrences of pattern."""
        maxsplit = _ssize(maxsplit)
        st = _state_init(self, string, 0, _SSIZE_MAX)
        result = []
        n = 0
        last = st.start
        groups = self.groups
        while not maxsplit or n < maxsplit:
            _state_reset(st)
            st.ptr = st.start
            if not _search(st):
                break
            result.append(_getslice(st, last, st.start))
            for i in range(groups):
                result.append(_state_getslice(st, i + 1, False))
            n += 1
            st.must_advance = st.ptr == st.start
            last = st.start = st.ptr
        result.append(_getslice(st, last, st.endpos))
        return result

    def sub(self, /, repl, string, count=0):
        """Return the string obtained by replacing the leftmost non-overlapping occurrences of pattern in string by the replacement repl."""
        return _subx(self, repl, string, _ssize(count), False)

    def subn(self, /, repl, string, count=0):
        """Return the tuple (new_string, number_of_subs_made) found by replacing the leftmost non-overlapping occurrences of pattern with the replacement repl."""
        return _subx(self, repl, string, _ssize(count), True)

    def __copy__(self):
        return self

    def __deepcopy__(self, memo, /):
        return self

    def __repr__(self):
        names = ((FLAG_IGNORECASE, 'IGNORECASE'), (FLAG_LOCALE, 'LOCALE'), (FLAG_MULTILINE, 'MULTILINE'),
                 (FLAG_DOTALL, 'DOTALL'), (FLAG_UNICODE, 'UNICODE'), (FLAG_VERBOSE, 'VERBOSE'),
                 (FLAG_DEBUG, 'DEBUG'), (FLAG_ASCII, 'ASCII'))
        flags = self.flags
        # re.UNICODE is the default of a str pattern
        if self.isbytes == 0 and flags & (FLAG_LOCALE | FLAG_UNICODE | FLAG_ASCII) == FLAG_UNICODE:
            flags &= ~FLAG_UNICODE
        items = []
        for value, name in names:
            if flags & value:
                items.append('re.' + name)
                flags &= ~value
        if flags:
            items.append('0x%x' % flags)
        text = repr(self.pattern)[:200]
        if items:
            return 're.compile(%s, %s)' % (text, '|'.join(items))
        return 're.compile(%s)' % text

    def __hash__(self):
        h = hash(self.pattern) ^ hash(tuple(self.code))
        h ^= self.flags
        h ^= self.isbytes
        h ^= len(self.code)
        if h == -1:
            h = -2
        return h

    def __eq__(self, other):
        if type(other) is not Pattern:
            return NotImplemented
        if self is other:
            return True
        return (self.flags == other.flags and self.isbytes == other.isbytes and self.code == other.code
                and self.pattern == other.pattern)

    def __ne__(self, other):
        if type(other) is not Pattern:
            return NotImplemented
        return not self.__eq__(other)

    def __class_getitem__(cls, item, /):
        """Patterns are generic over the type of string they handle (str or bytes)"""
        return GenericAlias(cls, item)


class Match:
    """The result of re.match() and re.search().
Match objects always have a boolean value of True."""

    __module__ = 're'
    __slots__ = ('re', 'string', 'pos', 'endpos', '_lastindex', '_mark', '_regs', '_isbytes', '_data')

    def __new__(cls, *args, **kwargs):
        _no_instances(cls)
    __iter__ = None
    __contains__ = None

    @property
    def lastindex(self):
        """The integer index of the last matched capturing group."""
        return self._lastindex if self._lastindex >= 0 else None

    @property
    def lastgroup(self):
        """The name of the last matched capturing group."""
        names = self.re._indexgroup
        if names is not None and 0 <= self._lastindex < len(names):
            return names[self._lastindex]
        return None

    @property
    def regs(self):
        regs = self._regs
        if regs is None:
            mark = self._mark
            regs = self._regs = tuple([(mark[2 * i], mark[2 * i + 1]) for i in range(len(mark) // 2)])
        return regs

    def _index(self, index):
        """match_getindex: a group number from a number or a name."""
        if isinstance(index, int):
            i = index
        elif hasattr(type(index), '__index__'):
            i = _ssize(index)
        else:
            i = -1
            names = self.re._groupindex
            if names is not None:
                try:
                    found = names[index]
                except KeyError:
                    found = None
                if isinstance(found, int):
                    i = found
        if i < 0 or i >= len(self._mark) // 2:
            raise IndexError("no such group")
        return i

    def _group(self, i, default):
        """match_getslice_by_index."""
        mark = self._mark
        a = mark[2 * i]
        if a < 0:
            return default
        b = mark[2 * i + 1]
        if self._isbytes:
            return self._data[a:b]
        return str_slice(self._data, a, b)

    def group(self, /, *args):
        """group([group1, ...]) -> str or tuple.
    Return subgroup(s) of the match by indices or names.
    For 0 returns the entire match."""
        if len(args) == 0:
            return self._group(0, None)
        if len(args) == 1:
            return self._group(self._index(args[0]), None)
        return tuple([self._group(self._index(a), None) for a in args])

    def __getitem__(self, name, /):
        return self._group(self._index(name), None)

    def groups(self, /, default=None):
        """Return a tuple containing all the subgroups of the match, from 1.

default
  Is used for groups that did not participate in the match."""
        return tuple([self._group(i, default) for i in range(1, len(self._mark) // 2)])

    def groupdict(self, /, default=None):
        """Return a dictionary containing all the named subgroups of the match, keyed by the subgroup name.

default
  Is used for groups that did not participate in the match."""
        result = {}
        names = self.re._groupindex
        if names is None:
            return result
        for key in list(names):
            result[key] = self._group(self._index(key), default)
        return result

    def start(self, group=0, /):
        """Return index of the start of the substring matched by group."""
        return self._mark[2 * self._index(group)]

    def end(self, group=0, /):
        """Return index of the end of the substring matched by group."""
        return self._mark[2 * self._index(group) + 1]

    def span(self, group=0, /):
        """For match object m, return the 2-tuple (m.start(group), m.end(group))."""
        i = self._index(group)
        return (self._mark[2 * i], self._mark[2 * i + 1])

    def expand(self, /, template):
        """Return the string obtained by doing backslash substitution on the string template, as done by the sub() method."""
        return _expand_template(_compile_template(self.re, template), self)

    def __copy__(self):
        return self

    def __deepcopy__(self, memo, /):
        return self

    def __repr__(self):
        return "<re.Match object; span=(%d, %d), match=%s>" % (self._mark[0], self._mark[1], repr(self._group(0, None))[:50])

    def __class_getitem__(cls, item, /):
        """Matches are generic over the type of string which was matched (str or bytes)"""
        return GenericAlias(cls, item)


class SRE_Scanner:
    __module__ = '_sre'
    __slots__ = ('pattern', '_state', '_executing')

    def __new__(cls, *args, **kwargs):
        _no_instances(cls)

    def _step(self, find):
        if self._executing:
            raise ValueError("regular expression scanner already executing")
        self._executing = True
        try:
            st = self._state
            if st.start is None:
                return None
            _state_reset(st)
            st.ptr = st.start
            status = find(st)
            match = _new_match(self.pattern, st, status)
            if status == 0:
                st.start = None
            else:
                st.must_advance = st.ptr == st.start
                st.start = st.ptr
            return match
        finally:
            self._executing = False

    def match(self):
        return self._step(lambda st: _match(st, 0, True))

    def search(self):
        return self._step(_search)



def _scanner(pattern, string, pos, endpos):
    st = _state_init(pattern, string, _ssize(pos), _ssize(endpos))
    scanner = object.__new__(SRE_Scanner)
    scanner.pattern = pattern
    scanner._state = st
    scanner._executing = False
    return scanner


def _new_match(pattern, st, status):
    """pattern_new_match."""
    if not status:
        return None
    match = object.__new__(Match)
    match.re = pattern
    match.string = st.string
    match._data = st.data
    match._isbytes = st.isbytes
    match._regs = None
    mark = [st.start, st.ptr]
    smark = st.mark
    lastmark = st.lastmark
    for i in range(pattern.groups):
        j = 2 * i
        if j + 1 <= lastmark and smark[j] is not None and smark[j + 1] is not None:
            a = smark[j]
            b = smark[j + 1]
            if a > b:
                raise SystemError("The span of capturing group is wrong, please report a bug for the re module.")
            mark.append(a)
            mark.append(b)
        else:
            mark.append(-1)
            mark.append(-1)
    match._mark = mark
    match.pos = st.pos
    match.endpos = st.endpos
    match._lastindex = st.lastindex
    return match


class SRE_Template:
    __module__ = '_sre'
    __slots__ = ('literal', 'items')

    def __new__(cls, *args, **kwargs):
        _no_instances(cls)



def template(pattern, template, /):
    """template(pattern, template) -> the compiled form of a list of interleaved literals and group indices"""
    if not isinstance(template, list):
        raise TypeError("_sre.template() argument 2 must be list, not %s" % type(template).__name__)
    n = len(template)
    if n & 1 == 0 or n < 1:
        raise TypeError("invalid template")
    result = object.__new__(SRE_Template)
    result.literal = template[0]
    items = []
    for i in range(n // 2):
        index = _ssize(template[2 * i + 1])
        if index < 0:
            raise TypeError("invalid template")
        literal = template[2 * i + 2]
        if len(literal) == 0:
            literal = None
        items.append((index, literal))
    result.items = tuple(items)
    return result


def _is_buffer(obj):
    try:
        memoryview(obj)
    except TypeError:
        return False
    return True


def _compile_template(pattern, template):
    """Delegate to Lib/re, which parses replacement strings."""
    import re
    try:
        result = re._compile_template(pattern, template)
    except TypeError:
        # an unhashable replacement (bytearray, a str subclass) is converted
        if isinstance(template, str) and type(template) is not str:
            template = str_slice(template, 0, len(template))
        elif type(template) is not bytes and not isinstance(template, str) and _is_buffer(template):
            template = bytes(memoryview(template))
        else:
            raise
        result = re._compile_template(pattern, template)
    if type(result) is not SRE_Template:
        raise RuntimeError("the result of compiling a replacement string is %s" % type(result).__name__)
    return result


def _expand_template(tmpl, match):
    items = tmpl.items
    if not items:
        return tmpl.literal
    parts = [tmpl.literal]
    groups = len(match._mark) // 2
    for index, literal in items:
        if index >= groups:
            raise IndexError("no such group")
        item = match._group(index, None)
        if item is not None:
            parts.append(item)
        if literal is not None:
            parts.append(literal)
    return tmpl.literal[:0].join(parts)


def _subx(pattern, ptemplate, string, count, subn):
    if callable(ptemplate):
        filter_ = ptemplate
        kind = 2
    else:
        # a template without a backslash is its own replacement
        if isinstance(ptemplate, str):
            literal = '\\' not in ptemplate
        else:
            try:
                literal = b'\\' not in _getstring(ptemplate)[1]
            except TypeError:
                literal = False
        if literal:
            filter_ = ptemplate
            kind = 0
        else:
            filter_ = _compile_template(pattern, ptemplate)
            if not filter_.items:
                filter_ = filter_.literal
                kind = 0
            else:
                kind = 1
    st = _state_init(pattern, string, 0, _SSIZE_MAX)
    result = []
    n = i = 0
    while not count or n < count:
        _state_reset(st)
        st.ptr = st.start
        if not _search(st):
            break
        b = st.start
        e = st.ptr
        if i < b:
            result.append(_getslice(st, i, b))
        if kind:
            match = _new_match(pattern, st, 1)
            item = _expand_template(filter_, match) if kind == 1 else filter_(match)
        else:
            item = filter_
        if item is not None:
            result.append(item)
        i = e
        n += 1
        st.must_advance = st.ptr == st.start
        st.start = st.ptr
    if i < st.endpos:
        result.append(_getslice(st, i, st.endpos))
    joiner = _getslice(st, 0, 0)
    out = joiner if not result else joiner.join(result)
    if subn:
        return (out, n)
    return out


def getcodesize():
    return CODESIZE


def ascii_iscased(character, /):
    return character < 128 and _ASCII_ALPHA(character)


def _ASCII_ALPHA(ch):
    return 65 <= ch <= 90 or 97 <= ch <= 122


def unicode_iscased(character, /):
    return character != _lower_unicode(character) or character != _upper_unicode(character)


def ascii_tolower(character, /):
    return _lower_ascii(character)


def unicode_tolower(character, /):
    return _lower_unicode(character)


def compile(pattern, flags, code, groups, groupindex, indexgroup):
    if not isinstance(flags, int):
        raise TypeError("compile() argument 'flags' must be int, not %s" % type(flags).__name__)
    if not -0x80000000 <= flags <= 0x7FFFFFFF:
        raise OverflowError("Python int too large to convert to C int")
    if not isinstance(code, list):
        raise TypeError("compile() argument 'code' must be list, not %s" % type(code).__name__)
    if not isinstance(groupindex, dict):
        raise TypeError("compile() argument 'groupindex' must be dict, not %s" % type(groupindex).__name__)
    if not isinstance(indexgroup, tuple):
        raise TypeError("compile() argument 'indexgroup' must be tuple, not %s" % type(indexgroup).__name__)
    groups = _ssize(groups)
    words = []
    for value in code:
        value = _ssize(value)
        if value < 0:
            raise OverflowError("can't convert negative value to unsigned int")
        if value > 0xFFFFFFFF:
            raise OverflowError("regular expression code size limit exceeded")
        words.append(value)
    try:
        _validate(words, groups)
    except (_Invalid, IndexError):
        raise RuntimeError("invalid SRE code") from None
    self = object.__new__(Pattern)
    self.code = words
    self._first = _first_tests(words)
    if pattern is None:
        self.isbytes = -1
    else:
        self.isbytes = 1 if _getstring(pattern)[0] else 0
    self.pattern = pattern
    self.flags = flags
    self.groups = groups
    self._groupindex = None
    self._indexgroup = None
    if len(groupindex) > 0:
        self._groupindex = groupindex
        if len(indexgroup) > 0:
            self._indexgroup = indexgroup
    return self
