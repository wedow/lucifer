"""_sre (Modules/_sre/sre.c, sre_lib.h): the engine behind Lib/re, as Python.

A compiled pattern is the list of code words Lib/re/_compiler.py emits. The
matcher runs them on a sequence of integers: a bytes object for bytes, and for
str a Latin-1 encoding of it, or a wrapper that reads code points where the
text has wider characters. It keeps CPython's explicit context stack, so
nesting costs heap, never Python frames: _match saves the caller's locals at a
jump and resumes it by label, as sre_lib.h resumes by goto."""

import sys
from _types import GenericAlias, MappingProxyType
from _lucifer import str_encode, str_ord, str_slice, b_find, str_find

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
_SSIZE_MIN = -sys.maxsize - 1

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


_last_text = [(None, None)]    # a list item, not a global: a store to a global costs compiled code its facts


def _text_of(string):
    """The integer sequence of a str; the last long one is remembered, so that
    a scanner that calls match(string, pos) in a loop converts it once."""
    long = len(string) > 1024
    if long:
        last = _last_text[0]
        if last[0] is string:
            return last[1]
    text = str_encode(string, 'latin-1', 'strict')     # bytes, or where it fails
    if type(text) is not bytes:
        text = _Wide(string)
    if long:
        _last_text[0] = (string, text)
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
    if type(value) is not int:
        if not isinstance(value, int):
            try:
                method = type(value).__index__
            except AttributeError:
                raise TypeError("'%s' object cannot be interpreted as an integer" % type(value).__name__) from None
            value = method(value)
        value = int.__index__(value)    # the value, as C reads it: no subclass's methods run
    if value > _SSIZE_MAX or value < _SSIZE_MIN:
        raise OverflowError("Python int too large to convert to C ssize_t")
    return value


class _Rep:
    """SRE_REPEAT: the state of one active REPEAT."""

    __slots__ = ('count', 'pattern', 'prev', 'last_ptr')


class _State:
    """SRE_STATE."""

    __slots__ = ('pattern', 'text', 'string', 'data', 'isbytes', 'start', 'end', 'ptr', 'pos', 'endpos',
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
    st.pattern = pattern
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
    st.mark = [None] * (2 * pattern.groups) if pattern.groups else _NO_MARKS
    st.match_all = False
    st.must_advance = False
    return st


_NO_MARKS = ()     # the marks of a pattern without groups, which nothing writes


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
        stop = _find_char(text, 10, ptr, end) if ptr < end else -1
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
    code = st.pattern.code
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


def _needles(prefix):
    """A literal prefix's code points as the needles of the native searches:
    (bytes, or None when one does not fit a byte; str)."""
    return (bytes(prefix) if max(prefix) < 256 else None), ''.join([chr(c) for c in prefix])


def _search_prefix(text, needles, ptr, end):
    """The first position in [ptr, end) where the whole prefix fits and starts."""
    if type(text) is bytes:
        return -1 if needles[0] is None else b_find(text, needles[0], ptr, end, False)
    return str_find(text.s, needles[1], ptr, end, False)


_BYTE = tuple([bytes((i,)) for i in range(256)])


def _find_char(text, ch, ptr, end):
    """text.find(ch, ptr, end) for a code point ch < 256, searched natively."""
    if type(text) is bytes:
        return b_find(text, _BYTE[ch], ptr, end, False)
    return str_find(text.s, chr(ch), ptr, end, False)


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


def _search_generic(st, sp=None):
    """SRE(search): the leftmost match from st.start; 1 with the span in
    st.start and st.ptr, or 0. With a _Spec, each further attempt is charged
    to its budget, and once that is spent the search stops undecided: -1, with
    the next position in st.start."""
    code = st.pattern.code
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
            prefix = _needles(code[7:7 + prefix_len])
        elif flags & INFO_CHARSET:
            charset = 5
        pattern = 1 + code[1]
    first = st.pattern._first

    if prefix is not None:
        # the pattern starts with a literal prefix
        end = st.end
        st.must_advance = False
        while ptr < end:
            i = _search_prefix(text, prefix, ptr, end)
            if i < 0:
                return 0
            if sp is not None and i > st.start:
                sp.budget -= _CALL_COST + i - st.start
                if sp.budget <= 0:
                    st.start = i
                    return -1
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
            if sp is not None and ptr > st.start:
                sp.budget -= _CALL_COST + ptr - st.start
                if sp.budget <= 0:
                    st.start = ptr
                    return -1
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
            if sp is not None:
                sp.budget -= _CALL_COST + 1
                if sp.budget <= 0:
                    st.start = ptr
                    return -1
            st.start = st.ptr = ptr
            status = _match(st, pattern, False)
    return status


# ---------------------------------------------------------------------------
# Specialization. A pattern that has done enough work in the interpreter above
# gets its code compiled to Python source: one matcher function with the
# opcodes unrolled, literals and sets as constants, alternatives as control
# flow and repeats as loops, which the tier then compiles like any function.
# A failure jumps to the innermost choice point: `continue` of the loop that
# tries its next alternative, or the fail statement its caller gave. A success
# returns from the matcher, so the rest of the pattern (the continuation) is
# emitted inside every choice point's loop, except after a repeat whose
# iterations backtrack, which is a function per iteration. The marks, lastmark
# and lastindex follow the interpreter's saves and restores exactly, so the
# generic engine stays the reference: a code word the generator does not handle
# leaves the pattern on it, and -X lucifer_stress=sre runs both and compares.

class _Unsupported(Exception):
    """The code has a construct the generator leaves to the interpreter."""


class _Branchy(Exception):
    """A segment that must not backtrack has a choice point."""


_CHAR_OPS = frozenset((LITERAL, NOT_LITERAL, IN, ANY, ANY_ALL, CATEGORY, LITERAL_IGNORE, NOT_LITERAL_IGNORE,
                       LITERAL_UNI_IGNORE, NOT_LITERAL_UNI_IGNORE, LITERAL_LOC_IGNORE, NOT_LITERAL_LOC_IGNORE,
                       IN_IGNORE, IN_UNI_IGNORE, IN_LOC_IGNORE))
_IN_OPS = frozenset((IN, IN_IGNORE, IN_UNI_IGNORE, IN_LOC_IGNORE))
_ONE_REPEATS = frozenset((REPEAT_ONE, MIN_REPEAT_ONE, POSSESSIVE_REPEAT_ONE))
_UNI_SPACE = bytes([1 if chr(c).isspace() else 0 for c in range(128)])
# Category pairs no character is in both of: each category and its complement,
# then digits, words and line breaks against spaces and each other.
_DISJOINT = set()
for _a, _b in [(2 * k, 2 * k + 1) for k in range(9)] + [(0, 2), (4, 2), (8, 2), (0, 5), (0, 9), (6, 0), (6, 4), (6, 8),
                                                       (6, 3), (10, 12), (14, 12), (10, 15), (16, 10), (16, 14),
                                                       (16, 13)]:
    _DISJOINT.add((_a, _b))
    _DISJOINT.add((_b, _a))
del _a, _b

# The source's free names, beside the constants each pattern adds.
_NAMESPACE = {'_ASCII_LOWER': _ASCII_LOWER, '_ASCII_WORD': _ASCII_WORD, '_ASCII_SPACE': _ASCII_SPACE,
              '_UNI_SPACE': _UNI_SPACE, '_LINEBREAKS': _LINEBREAKS, '_lower_unicode': _lower_unicode,
              '_upper_unicode': _upper_unicode, '_lower_ascii': _lower_ascii, '_lower_locale': _lower_locale,
              '_char_loc_ignore': _char_loc_ignore, '_charset': _charset, '_charset_loc_ignore': _charset_loc_ignore,
              '_search_prefix': _search_prefix, '_find_char': _find_char}


def _char_end(code, pc):
    """The code word after the one-character test at pc."""
    op = code[pc]
    if op == ANY or op == ANY_ALL:
        return pc + 1
    if op in _IN_OPS:
        return pc + 1 + code[pc + 1]
    return pc + 2


def _test(code, pc, ch):
    """Does ch pass the one-character test at pc? As the matcher decides it."""
    op = code[pc]
    if op == ANY:
        return ch != 10
    if op == ANY_ALL:
        return True
    if op == CATEGORY:
        return _category(code[pc + 1], ch)
    if op == IN:
        return _charset(code, pc + 2, ch)
    if op == IN_IGNORE:
        return _charset(code, pc + 2, _lower_ascii(ch))
    if op == IN_UNI_IGNORE:
        return _charset(code, pc + 2, _lower_unicode(ch))
    if op == IN_LOC_IGNORE:
        return _charset_loc_ignore(code, pc + 2, ch)
    v = code[pc + 1]
    if op == LITERAL:
        return ch == v
    if op == NOT_LITERAL:
        return ch != v
    if op == LITERAL_IGNORE or op == NOT_LITERAL_IGNORE:
        return (_lower_ascii(ch) == v) == (op == LITERAL_IGNORE)
    if op == LITERAL_UNI_IGNORE or op == NOT_LITERAL_UNI_IGNORE:
        return (_lower_unicode(ch) == v) == (op == LITERAL_UNI_IGNORE)
    return _char_loc_ignore(v, ch) == (op == LITERAL_LOC_IGNORE)


def _members(code, pc):
    """Every character the test at pc passes, when that is a short list; else None."""
    op = code[pc]
    if op == LITERAL:
        return (code[pc + 1],)
    if op == LITERAL_IGNORE:
        v = code[pc + 1]
        return tuple([c for c in (v, v - 32, v + 32) if c >= 0 and _test(code, pc, c)])
    if op != IN:
        return None
    out = []
    s = pc + 2
    while code[s] != FAILURE:
        op = code[s]
        if op == LITERAL:
            out.append(code[s + 1])
            s += 2
        elif op == RANGE and code[s + 2] - code[s + 1] < 256:
            out.extend(range(code[s + 1], code[s + 2] + 1))
            s += 3
        elif op == CHARSET:
            out.extend([c for c in range(256) if (code[s + 1 + (c >> 5)] >> (c & 31)) & 1])
            s += 9
        else:
            return None
    return out if len(out) <= 512 else None


def _categories(code, pc):
    """The categories of a set made of nothing else, or None."""
    if code[pc] != IN:
        return None
    cats = []
    s = pc + 2
    while code[s] == CATEGORY:
        cats.append(code[s + 1])
        s += 2
    return cats if cats and code[s] == FAILURE else None


def _disjoint(code, a, b):
    """Can no character pass both one-character tests?"""
    for x, y in ((a, b), (b, a)):
        chars = _members(code, x)
        if chars is not None:
            for c in chars:
                if _test(code, y, c):
                    return False
            return True
    ca = _categories(code, a)
    cb = _categories(code, b)
    if ca is None or cb is None:
        return False
    for x in ca:
        for y in cb:
            if (x, y) not in _DISJOINT:
                return False
    return True


class _Gen:
    """Python source for one entry of one pattern's code."""

    def __init__(self, code):
        self.code = code
        self.out = []           # (indent, text), or None for a dropped placeholder
        self.subs = []          # finished sub-matcher functions, as lines
        self.sub_names = {}
        self.consts = {}
        self.memo = {}          # (test pc, test pc) -> disjoint
        self.loops = {}         # a REPEAT's UNTIL pc -> (body, tail)
        self.untils = {}        # the UNTIL of a REPEAT emitted as an optional body -> (tail, its inrep)
        self.live = []          # the repeats emitted as functions whose UNTIL the code being emitted reaches
        self.rec_names = {}
        self.n = 0
        self.limit = 4000 + 400 * len(code)

    def new(self):
        self.n += 1
        return self.n

    def w(self, ind, text):
        self.out.append((ind, text))
        if len(self.out) > self.limit:
            raise _Unsupported

    def const(self, value):
        name = '_k%d' % len(self.consts)
        self.consts[name] = value
        return name

    def try_straight(self, emit, *args):
        """emit(*args) when it emits no choice point; False, with nothing emitted, when it would."""
        mark = len(self.out)
        try:
            emit(*args)
        except _Branchy:
            del self.out[mark:]
            return False
        return True

    # --- one-character tests --------------------------------------------

    def cond(self, pc):
        """(statements, expression): the test at pc of the character in ch."""
        code = self.code
        op = code[pc]
        if op == ANY:
            return (), 'ch != 10'
        if op == ANY_ALL:
            return (), 'True'
        if op == CATEGORY:
            return (), self.category(code[pc + 1], 'ch')
        if op == IN:
            return (), self.charset(pc + 2, 'ch')
        if op == IN_IGNORE:
            return ('la = _ASCII_LOWER[ch] if ch < 128 else ch',), self.charset(pc + 2, 'la')
        if op == IN_UNI_IGNORE:
            return ('lu = _ASCII_LOWER[ch] if ch < 128 else _lower_unicode(ch)',), self.charset(pc + 2, 'lu')
        if op == IN_LOC_IGNORE:
            return (), '_charset_loc_ignore(_code, %d, ch)' % (pc + 2)
        v = code[pc + 1]
        if op == LITERAL:
            return (), 'ch == %d' % v
        if op == NOT_LITERAL:
            return (), 'ch != %d' % v
        if op == LITERAL_IGNORE or op == NOT_LITERAL_IGNORE:
            return (('la = _ASCII_LOWER[ch] if ch < 128 else ch',),
                    'la %s %d' % ('==' if op == LITERAL_IGNORE else '!=', v))
        if op == LITERAL_UNI_IGNORE or op == NOT_LITERAL_UNI_IGNORE:
            return (('lu = _ASCII_LOWER[ch] if ch < 128 else _lower_unicode(ch)',),
                    'lu %s %d' % ('==' if op == LITERAL_UNI_IGNORE else '!=', v))
        return (), '%s_char_loc_ignore(%d, ch)' % ('' if op == LITERAL_LOC_IGNORE else 'not ', v)

    def category(self, cat, c):
        if cat > 17:
            raise _Unsupported
        neg = 'not ' if cat & 1 else ''
        cat &= ~1
        if cat == 0:
            e = '48 <= %s <= 57' % c
        elif cat == 2:
            e = '%s < 128 and _ASCII_SPACE[%s] != 0' % (c, c)
        elif cat == 4 or cat == 8:
            e = '%s < 128 and _ASCII_WORD[%s] != 0' % (c, c)
        elif cat == 6:
            e = '%s == 10' % c
        elif cat == 10:
            e = '48 <= %s <= 57 if %s < 128 else chr(%s).isdecimal()' % (c, c, c)
        elif cat == 12:
            e = '_UNI_SPACE[%s] != 0 if %s < 128 else chr(%s).isspace()' % (c, c, c)
        elif cat == 14:
            e = '_ASCII_WORD[%s] != 0 if %s < 128 else chr(%s).isalnum()' % (c, c, c)
        else:
            e = '%s in _LINEBREAKS' % c
        return '%s(%s)' % (neg, e)

    def charset(self, s, c):
        """The membership of c in the set whose items start at s, as one expression."""
        code = self.code
        parts = []
        neg = False
        if code[s] == NEGATE:
            neg = True
            s += 1
        while True:
            op = code[s]
            if op == FAILURE:
                break
            if op == LITERAL:
                parts.append('%s == %d' % (c, code[s + 1]))
                s += 2
            elif op == RANGE:
                parts.append('%d <= %s <= %d' % (code[s + 1], c, code[s + 2]))
                s += 3
            elif op == RANGE_UNI_IGNORE:
                lo, hi = code[s + 1], code[s + 2]
                parts.append('%d <= %s <= %d or %d <= _upper_unicode(%s) <= %d' % (lo, c, hi, lo, c, hi))
                s += 3
            elif op == CATEGORY:
                parts.append(self.category(code[s + 1], c))
                s += 2
            elif op == CHARSET:
                table = bytes([(code[s + 1 + (i >> 5)] >> (i & 31)) & 1 for i in range(256)])
                parts.append('%s < 256 and %s[%s] != 0' % (c, self.const(table), c))
                s += 9
            elif op == BIGCHARSET:
                parts.append('_charset(_code, %d, %s)' % (s, c))   # it and the items after it
                break
            else:
                raise _Unsupported
        e = ' or '.join(['(%s)' % p for p in parts]) or 'False'
        return 'not (%s)' % e if neg else e

    def test_char(self, pc, ind, at, fail):
        """Fail unless the character at index `at` (already below end) passes the test at pc."""
        prep, e = self.cond(pc)
        self.w(ind, 'ch = text[%s]' % at)
        for line in prep:
            self.w(ind, line)
        self.w(ind, 'if not (%s): %s' % (e, fail))

    def char(self, pc, ind, fail):
        """The one-character test at pc, consuming the character."""
        code = self.code
        op = code[pc]
        if op == LITERAL:
            self.w(ind, 'if ptr >= end or text[ptr] != %d: %s' % (code[pc + 1], fail))
        elif op == NOT_LITERAL:
            self.w(ind, 'if ptr >= end or text[ptr] == %d: %s' % (code[pc + 1], fail))
        elif op == ANY:
            self.w(ind, 'if ptr >= end or text[ptr] == 10: %s' % fail)
        elif op == ANY_ALL:
            self.w(ind, 'if ptr >= end: %s' % fail)
        else:
            self.w(ind, 'if ptr >= end: %s' % fail)
            self.test_char(pc, ind, 'ptr', fail)
        self.w(ind, 'ptr += 1')

    def count(self, item, maximum, ind):
        """SRE(count): advance ptr over at most `maximum` characters that pass the test at item."""
        code = self.code
        op = code[item]
        if maximum == MAXREPEAT:
            lim = 'end'
        else:
            lim = 'cl'
            self.w(ind, 'cl = ptr + %d if %d < end - ptr else end' % (maximum, maximum))
        if op == ANY_ALL:
            self.w(ind, 'if %s > ptr: ptr = %s' % (lim, lim))
        elif op == ANY or (op == NOT_LITERAL and code[item + 1] < 256):
            self.w(ind, 'q = _find_char(text, %d, ptr, %s) if ptr < %s else -1' % (10 if op == ANY else code[item + 1], lim, lim))
            self.w(ind, 'ptr = %s if q < 0 else q' % lim)
        elif op == LITERAL:
            self.w(ind, 'while ptr < %s and text[ptr] == %d: ptr += 1' % (lim, code[item + 1]))
        else:
            self.w(ind, 'while ptr < %s:' % lim)
            self.test_char(item, ind + 1, 'ptr', 'break')
            self.w(ind + 1, 'ptr += 1')

    # --- the walk -------------------------------------------------------

    def first_set(self, pc, follow, depth=0, guarded=False):
        """The one-character tests (their pcs) a match from pc starts with one of, or None when
        unknown. follow: pc is a continuation, so reaching the (sub)match's SUCCESS adds 'end' and
        reaching the end of a REPEAT's body adds what an iteration or the repeat's tail starts with.
        Without follow, a match that may be empty is unknown. The sets serve positions before the
        end: there, $ passes only before a newline ('nl'), and \\Z never. Other zero-width
        assertions are passed over (guarded), except on the way to 'end', whose success they would
        make depend on the position."""
        code = self.code
        out = []
        while depth < 40:
            op = code[pc]
            if op == MARK:
                pc += 2
            elif op == AT and (code[pc + 1] == AT_END or code[pc + 1] == AT_END_LINE):
                out.append('nl')
                return out
            elif op == AT and code[pc + 1] == AT_END_STRING:
                return out
            elif op == AT:
                pc += 2
                guarded = True
            elif op == ASSERT or op == ASSERT_NOT:
                pc += 1 + code[pc + 1]
                guarded = True
            elif op == JUMP:
                pc += 1 + code[pc + 1]
            elif op in _CHAR_OPS:
                out.append(pc)
                return out
            elif op == SUCCESS and follow and not guarded:
                out.append('end')
                return out
            elif op in _ONE_REPEATS:
                out.append(pc + 4)
                if code[pc + 2]:
                    return out
                pc += 1 + code[pc + 1]
            elif op == REPEAT or op == POSSESSIVE_REPEAT or op == ATOMIC_GROUP:
                body = self.first_set(pc + (2 if op == ATOMIC_GROUP else 4), False, depth + 1)
                if body is None:
                    return None
                out.extend(body)
                if op == ATOMIC_GROUP or code[pc + 2]:
                    return out
                pc += 2 + code[pc + 1]
            elif op == BRANCH:
                pc += 1
                while code[pc]:
                    alt = self.first_set(pc + 1, follow, depth + 1, guarded)
                    if alt is None:
                        return None
                    out.extend(alt)
                    pc += code[pc]
                return out
            elif (op == MAX_UNTIL or op == MIN_UNTIL) and follow and pc in self.loops:
                body, tail = self.loops[pc]
                more = self.first_set(body, False, depth + 1)
                after = self.first_set(tail, True, depth + 1, guarded)
                if more is None or after is None:
                    return None
                return out + more + after
            else:
                return None
            depth += 1
        return None

    def excludes(self, items, tests, end_ok):
        """Does no character pass both one of the tests at items and one of tests? 'end' among
        tests is allowed when end_ok."""
        if not items or tests is None or 'end' in items or 'nl' in items:
            return False
        for t in tests:
            if t == 'end':
                if not end_ok:
                    return False
                continue
            if t == 'nl':
                if any([_test(self.code, i, 10) for i in items]):
                    return False
                continue
            for i in items:
                key = (i, t)
                d = self.memo.get(key)
                if d is None:
                    d = self.memo[key] = _disjoint(self.code, i, t)
                if not d:
                    return False
        return True

    def seq(self, pc, stop, ind, fail, inrep, succ):
        """Emit the code from pc. With stop, up to stop, emitting no choice point (else _Branchy);
        without, to the end of the (sub)match: the emitted code then returns or fails.
        inrep: inside a REPEAT, where choice points save the marks; succ: 'match', 'search' or 'sub'."""
        code = self.code
        while pc != stop:
            op = code[pc]
            if op == LITERAL and code[pc + 2] == LITERAL and pc + 2 != stop:
                pc = self.literals(pc, stop, ind, fail)
            elif op in _CHAR_OPS:
                self.char(pc, ind, fail)
                pc = _char_end(code, pc)
            elif op == MARK:
                self.mark(code[pc + 1], ind)
                pc += 2
            elif op == AT:
                self.at(code[pc + 1], ind, fail)
                pc += 2
            elif op == SUCCESS:
                if stop is not None:
                    raise _Unsupported
                self.success(ind, fail, succ)
                return
            elif op == FAILURE:
                self.w(ind, fail)
                return
            elif op == JUMP:
                if stop is not None:
                    raise _Unsupported
                pc += 1 + code[pc + 1]
            elif op == GROUPREF or op == GROUPREF_IGNORE or op == GROUPREF_UNI_IGNORE or op == GROUPREF_LOC_IGNORE:
                self.groupref(op, code[pc + 1], ind, fail)
                pc += 2
            elif op == GROUPREF_EXISTS:
                pc = self.groupref_exists(pc, stop, ind, fail, inrep, succ)
            elif op == BRANCH:
                pc = self.branch(pc, stop, ind, fail, inrep, succ)
            elif op in _ONE_REPEATS:
                pc = self.repeat_one(pc, stop, ind, fail, inrep, succ)
            elif op == REPEAT:
                pc = self.repeat(pc, stop, ind, fail, inrep, succ)
            elif op == POSSESSIVE_REPEAT:
                pc = self.possessive(pc, ind, fail, succ)
            elif op == ATOMIC_GROUP:
                pc = self.atomic(pc, ind, fail, inrep, succ)
            elif op == ASSERT:
                pc = self.assert_(pc, ind, fail, inrep, succ)
            elif op == ASSERT_NOT:
                pc = self.assert_not(pc, ind, fail, inrep)
            elif (op == MAX_UNTIL or op == MIN_UNTIL) and self.live and self.live[-1][1] == pc:
                self.next_iteration(ind, fail, succ)
                return
            elif (op == MAX_UNTIL or op == MIN_UNTIL) and pc in self.untils:
                pc, inrep = self.untils[pc]
            else:
                raise _Unsupported
            if pc is None:
                return

    def literals(self, pc, stop, ind, fail):
        code = self.code
        values = []
        while code[pc] == LITERAL and pc != stop:
            values.append(code[pc + 1])
            pc += 2
        tests = ' or '.join(['text[ptr + %d] != %d' % (i, v) if i else 'text[ptr] != %d' % v
                             for i, v in enumerate(values)])
        self.w(ind, 'if end - ptr < %d or %s: %s' % (len(values), tests, fail))
        self.w(ind, 'ptr += %d' % len(values))
        return pc

    def mark(self, i, ind):
        if i & 1:
            self.w(ind, 'lastindex = %d' % (i // 2 + 1))
        self.w(ind, 'if lastmark < %d:' % i)
        if i > 0:
            self.w(ind + 1, 'j = lastmark + 1')
            self.w(ind + 1, 'while j < %d:' % i)
            self.w(ind + 2, 'mark[j] = None')
            self.w(ind + 2, 'j += 1')
        self.w(ind + 1, 'lastmark = %d' % i)
        self.w(ind, 'mark[%d] = ptr' % i)

    def at(self, at, ind, fail):
        if at == AT_BEGINNING or at == AT_BEGINNING_STRING:
            self.w(ind, 'if ptr != 0: %s' % fail)
        elif at == AT_BEGINNING_LINE:
            self.w(ind, 'if ptr != 0 and text[ptr - 1] != 10: %s' % fail)
        elif at == AT_END:
            self.w(ind, 'if ptr != end and (end - ptr != 1 or text[ptr] != 10): %s' % fail)
        elif at == AT_END_LINE:
            self.w(ind, 'if ptr != end and (ptr >= len(text) or text[ptr] != 10): %s' % fail)
        elif at == AT_END_STRING:
            self.w(ind, 'if ptr != end: %s' % fail)
        elif AT_BOUNDARY <= at <= AT_UNI_NON_BOUNDARY:
            word = self.category(14 if at >= AT_UNI_BOUNDARY else 4, 'ch')
            self.w(ind, 'w0 = False')
            self.w(ind, 'if ptr > 0:')
            self.w(ind + 1, 'ch = text[ptr - 1]')
            self.w(ind + 1, 'w0 = %s' % word)
            self.w(ind, 'w1 = False')
            self.w(ind, 'if ptr < end:')
            self.w(ind + 1, 'ch = text[ptr]')
            self.w(ind + 1, 'w1 = %s' % word)
            boundary = at == AT_BOUNDARY or at == AT_LOC_BOUNDARY or at == AT_UNI_BOUNDARY
            self.w(ind, 'if w0 %s w1: %s' % ('==' if boundary else '!=', fail))
        else:
            raise _Unsupported

    def groupref(self, op, group, ind, fail):
        g = 2 * group
        self.w(ind, 'if %d >= lastmark: %s' % (g, fail))
        self.w(ind, 'gp = mark[%d]' % g)
        self.w(ind, 'ge = mark[%d]' % (g + 1))
        self.w(ind, 'if gp is None or ge is None or ge < gp: %s' % fail)
        if op == GROUPREF:
            differ = 'text[ptr] != text[gp]'
        else:
            low = {GROUPREF_IGNORE: '_lower_ascii', GROUPREF_UNI_IGNORE: '_lower_unicode'}.get(op, '_lower_locale')
            differ = '%s(text[ptr]) != %s(text[gp])' % (low, low)
        self.w(ind, 'while gp < ge:')
        self.w(ind + 1, 'if ptr >= end or %s: break' % differ)
        self.w(ind + 1, 'gp += 1')
        self.w(ind + 1, 'ptr += 1')
        self.w(ind, 'if gp < ge: %s' % fail)

    def success(self, ind, fail, succ):
        if succ == 'sub':
            self.w(ind, 'st.lastmark = lastmark')
            self.w(ind, 'st.lastindex = lastindex')
            self.w(ind, 'return ptr')
            return
        if succ == 'match':
            self.w(ind, 'if (match_all and ptr != end) or (adv and ptr == start): %s' % fail)
        else:
            self.w(ind, 'if adv and ptr == start: %s' % fail)
            self.w(ind, 'st.start = pos')
        self.w(ind, 'st.ptr = ptr')
        self.w(ind, 'st.lastmark = lastmark')
        self.w(ind, 'st.lastindex = lastindex')
        self.w(ind, 'return 1')

    # --- saving and restoring at a choice point ---------------------------

    def save(self, n, ind, snap):
        self.w(ind, 'm%d = lastmark' % n)
        self.w(ind, 'x%d = lastindex' % n)
        if snap:
            self.w(ind, 's%d = mark[:m%d + 1] if m%d >= 0 else None' % (n, n, n))

    def restore(self, n, ind, snap):
        self.w(ind, 'lastmark = m%d' % n)
        self.w(ind, 'lastindex = x%d' % n)
        if snap:
            self.w(ind, 'if m%d >= 0: mark[:m%d + 1] = s%d' % (n, n, n))

    def nonempty(self, start, ind):
        if len(self.out) == start:
            self.w(ind, 'pass')

    # --- constructs -----------------------------------------------------

    def branch(self, pc, stop, ind, fail, inrep, succ):
        code = self.code
        alts = []
        pos = pc + 1
        while code[pos]:
            jump = pos + code[pos] - 2
            if code[jump] != JUMP:
                raise _Unsupported
            alts.append((pos + 1, jump))
            pos += code[pos]
        after = pos + 1
        for body, jump in alts:
            if jump + 1 + code[jump + 1] != after:
                raise _Unsupported
        if not alts:
            self.w(ind, fail)
            return None
        # Alternatives whose first characters exclude each other: at most one can start, so the
        # branch never backtracks into another and is plain control flow.
        firsts = [self.first_set(body, True) for body, jump in alts]
        if all([f and 'end' not in f and 'nl' not in f for f in firsts]) and all(
                [self.excludes(firsts[i], firsts[j], False) for i in range(len(firsts)) for j in range(i)]):
            if self.try_straight(self.dispatch, alts, firsts, ind, fail, inrep, succ):
                return after
        if stop is not None:
            raise _Branchy
        n = self.new()
        self.w(ind, 'p%d = ptr' % n)
        self.save(n, ind, inrep)
        self.w(ind, 'a%d = 0' % n)
        self.w(ind, 'while True:')
        self.w(ind + 1, 'a%d += 1' % n)
        self.w(ind + 1, 'if a%d > 1:' % n)
        self.restore(n, ind + 2, inrep)
        self.w(ind + 2, 'ptr = p%d' % n)
        # Alternatives without choice points of their own share one continuation after them.
        mark = len(self.out)
        shared = True
        for k, (body, jump) in enumerate(alts):
            self.w(ind + 1, '%s a%d == %d:' % ('if' if k == 0 else 'elif', n, k + 1))
            start = len(self.out)
            if not self.try_straight(self.seq, body, jump, ind + 2, 'continue', inrep, succ):
                shared = False
                break
            self.nonempty(start, ind + 2)
        if not shared:
            del self.out[mark:]
            for k, (body, jump) in enumerate(alts):
                self.w(ind + 1, '%s a%d == %d:' % ('if' if k == 0 else 'elif', n, k + 1))
                self.seq(body, None, ind + 2, 'continue', inrep, succ)
        self.w(ind + 1, 'else:')
        self.w(ind + 2, 'break')
        if shared:
            self.seq(after, None, ind + 1, 'continue', inrep, succ)
        self.w(ind, fail)
        return None

    def dispatch(self, alts, firsts, ind, fail, inrep, succ):
        self.w(ind, 'if ptr >= end: %s' % fail)
        self.w(ind, 'ch = text[ptr]')
        preps = []
        conds = []
        for tests in firsts:
            es = []
            for f in tests:
                prep, e = self.cond(f)
                preps.extend([p for p in prep if p not in preps])
                es.append('(%s)' % e)
            conds.append(' or '.join(es))
        for p in preps:
            self.w(ind, p)
        for k, (body, jump) in enumerate(alts):
            self.w(ind, '%s %s:' % ('if' if k == 0 else 'elif', conds[k]))
            start = len(self.out)
            self.seq(body, jump, ind + 1, fail, inrep, succ)
            self.nonempty(start, ind + 1)
        self.w(ind, 'else:')
        self.w(ind + 1, fail)

    def groupref_exists(self, pc, stop, ind, fail, inrep, succ):
        code = self.code
        g = 2 * code[pc + 1]
        yes = pc + 3
        no = pc + 1 + code[pc + 2]
        if code[no - 2] == JUMP and self.reaches(yes, no - 2):
            yes_stop = no - 2
            join = no - 1 + code[no - 1]
        else:
            yes_stop = join = no
        cond = ('%d < lastmark and mark[%d] is not None and mark[%d] is not None and mark[%d] >= mark[%d]'
                % (g, g, g + 1, g + 1, g))
        mark = len(self.out)
        try:
            self.w(ind, 'if %s:' % cond)
            start = len(self.out)
            self.seq(yes, yes_stop, ind + 1, fail, inrep, succ)
            self.nonempty(start, ind + 1)
            self.w(ind, 'else:')
            start = len(self.out)
            self.seq(no, join, ind + 1, fail, inrep, succ)
            self.nonempty(start, ind + 1)
            return join
        except _Branchy:
            del self.out[mark:]
            if stop is not None:
                raise
        self.w(ind, 'if %s:' % cond)
        self.seq(yes, None, ind + 1, fail, inrep, succ)
        self.w(ind, 'else:')
        self.seq(no, None, ind + 1, fail, inrep, succ)
        return None

    def reaches(self, pc, target):
        """Does stepping op by op from pc land on target?"""
        code = self.code
        while pc < target:
            op = code[pc]
            if op in _CHAR_OPS:
                pc = _char_end(code, pc)
            elif op in (MARK, AT, GROUPREF, GROUPREF_IGNORE, GROUPREF_UNI_IGNORE, GROUPREF_LOC_IGNORE):
                pc += 2
            elif op == BRANCH:
                pc += 1
                while code[pc]:
                    pc += code[pc]
                pc += 1
            elif op == REPEAT:
                pc += 2 + code[pc + 1]
            elif op in _ONE_REPEATS or op == ATOMIC_GROUP or op == ASSERT or op == ASSERT_NOT:
                pc += 1 + code[pc + 1]
            elif op == POSSESSIVE_REPEAT:
                pc += 2 + code[pc + 1]
            elif op == GROUPREF_EXISTS:
                pc += 1 + code[pc + 2]
                if code[pc - 2] == JUMP:
                    pc += code[pc - 1] - 1
            else:
                return False
        return pc == target

    def repeat_one(self, pc, stop, ind, fail, inrep, succ):
        code = self.code
        op = code[pc]
        minimum = code[pc + 2]
        maximum = code[pc + 3]
        item = pc + 4
        tail = pc + 1 + code[pc + 1]
        if code[item] not in _CHAR_OPS or _char_end(code, item) != tail - 1 or code[tail - 1] != SUCCESS:
            raise _Unsupported
        n = self.new()
        if op == POSSESSIVE_REPEAT_ONE or minimum == maximum or self.excludes(
                [item], self.first_set(tail, True), op == REPEAT_ONE):
            # Giving back an item never lets the tail match: take as many as there are.
            self.w(ind, 'if %d > end - ptr: %s' % (minimum, fail))
            self.w(ind, 'p%d = ptr' % n)
            self.count(item, maximum, ind)
            if minimum:
                self.w(ind, 'if ptr - p%d < %d: %s' % (n, minimum, fail))
            return tail
        if stop is not None:
            raise _Branchy
        self.w(ind, 'if %d > end - ptr: %s' % (minimum, fail))
        if op == REPEAT_ONE:
            self.w(ind, 'p%d = ptr' % n)
            self.count(item, maximum, ind)
            self.w(ind, 'c%d = ptr - p%d + 1' % (n, n))
            if minimum:
                self.w(ind, 'if c%d <= %d: %s' % (n, minimum, fail))
            self.save(n, ind, inrep)
            self.w(ind, 'while True:')
            self.w(ind + 1, 'c%d -= 1' % n)
            self.w(ind + 1, 'ptr = p%d + c%d' % (n, n))     # the tail moved it
            self.restore(n, ind + 1, inrep)
            if code[tail] == LITERAL:
                self.w(ind + 1, 'while c%d >= %d and (ptr >= end or text[ptr] != %d):' % (n, minimum, code[tail + 1]))
                self.w(ind + 2, 'ptr -= 1')
                self.w(ind + 2, 'c%d -= 1' % n)
            self.w(ind + 1, 'if c%d < %d: break' % (n, minimum))
        else:
            self.w(ind, 'p%d = ptr' % n)
            if minimum:
                self.count(item, minimum, ind)
                self.w(ind, 'if ptr - p%d < %d: %s' % (n, minimum, fail))
            self.w(ind, 'c%d = %d' % (n, minimum))
            self.save(n, ind, inrep)
            self.w(ind, 'b%d = 0' % n)
            self.w(ind, 'while True:')
            self.w(ind + 1, 'if b%d:' % n)
            self.restore(n, ind + 2, inrep)
            self.w(ind + 2, 'ptr = p%d + c%d' % (n, n))
            self.w(ind + 2, 'if ptr >= end: break')
            self.test_char(item, ind + 2, 'ptr', 'break')
            self.w(ind + 2, 'ptr += 1')
            self.w(ind + 2, 'c%d += 1' % n)
            self.w(ind + 1, 'b%d = 1' % n)
            if maximum != MAXREPEAT:
                self.w(ind + 1, 'if c%d > %d: break' % (n, maximum))
        self.seq(tail, None, ind + 1, 'continue', inrep, succ)
        self.w(ind, fail)
        return None

    def loop(self, n, body, minimum, maximum, ind, fail, snap, keep):
        """The iterations of a greedy repeat that never backtracks into an iteration: body(ind, fail)
        emits one. The last attempt that fails is undone; keep: push each iteration's start and saved
        marks on k<n>, for the tail to back off to."""
        counted = maximum != MAXREPEAT
        if minimum or counted:
            self.w(ind, 'c%d = 0' % n)
        if minimum == 1:
            body(ind, fail)
            if counted:
                self.w(ind, 'c%d = 1' % n)
        elif minimum:
            self.w(ind, 'while c%d < %d:' % (n, minimum))
            body(ind + 1, 'break')
            self.w(ind + 1, 'c%d += 1' % n)
            self.w(ind, 'if c%d < %d: %s' % (n, minimum, fail))
        if maximum == minimum:
            return
        if keep:
            self.w(ind, 'k%d = []' % n)
        self.w(ind, 'v%d = -1' % n)
        self.w(ind, 'p%d = -1' % n)
        self.w(ind, 'while ptr != v%d%s:' % (n, ' and c%d < %d' % (n, maximum) if counted else ''))
        if snap:
            self.save(n, ind + 1, True)
        self.w(ind + 1, 'p%d = v%d = ptr' % (n, n))
        body(ind + 1, 'break')
        if keep:
            self.w(ind + 1, 'k%d.append((p%d, m%d, x%d, s%d))' % (n, n, n, n, n) if snap else 'k%d.append(p%d)' % (n, n))
        if counted:
            self.w(ind + 1, 'c%d += 1' % n)
        self.w(ind + 1, 'p%d = -1' % n)
        self.w(ind, 'if p%d >= 0:' % n)
        if snap:
            self.restore(n, ind + 1, True)
        self.w(ind + 1, 'ptr = p%d' % n)

    def writes_marks(self, emit, *args):
        """Does emit(*args) emit a MARK? It emits nothing either way."""
        mark = len(self.out)
        try:
            emit(*args)
        finally:
            text = [line[1] for line in self.out[mark:]]
            del self.out[mark:]
        return any(['lastmark =' in t for t in text])

    def repeat(self, pc, stop, ind, fail, inrep, succ):
        code = self.code
        minimum = code[pc + 2]
        maximum = code[pc + 3]
        body = pc + 4
        until = pc + 1 + code[pc + 1]
        tail = until + 1
        lazy = code[until] == MIN_UNTIL
        if not lazy and code[until] != MAX_UNTIL:
            raise _Unsupported

        self.loops[until] = (body, tail)

        def iteration(i, f):
            self.seq(body, until, i, f, True, succ)
        try:
            snap = self.writes_marks(iteration, ind, 'break')
            plain = True        # no choice point inside an iteration
        except _Branchy:
            plain = False
        straight = plain and (minimum == maximum or not lazy and self.excludes(
            self.first_set(body, False), self.first_set(tail, True), True))
        if not straight and stop is not None:
            raise _Branchy
        n = self.new()
        if not lazy and minimum == 0 and maximum == 1 and not straight:
            # At most one iteration: the iteration and the tail after it, or else the tail.
            self.untils[until] = (tail, inrep)
            self.w(ind, 'p%d = ptr' % n)
            self.save(n, ind, True)
            self.w(ind, 'a%d = 0' % n)
            self.w(ind, 'while True:')
            self.w(ind + 1, 'a%d += 1' % n)
            self.w(ind + 1, 'if a%d == 1:' % n)
            self.seq(body, None, ind + 2, 'continue', True, succ)
            self.w(ind + 1, 'elif a%d == 2:' % n)
            self.restore(n, ind + 2, True)
            self.w(ind + 2, 'ptr = p%d' % n)
            self.seq(tail, None, ind + 2, 'continue', inrep, succ)
            self.w(ind + 1, 'else:')
            self.w(ind + 2, 'break')
            self.w(ind, fail)
            return None
        if not plain:
            self.call_repeat(pc, ind, fail, inrep, succ)
            return None
        if not lazy:
            self.loop(n, iteration, minimum, maximum, ind, fail, snap, not straight)
            if straight:
                return tail
            self.w(ind, 'b%d = 0' % n)
            self.w(ind, 'while True:')
            self.w(ind + 1, 'if b%d:' % n)
            self.w(ind + 2, 'if not k%d: break' % n)
            if snap:
                self.w(ind + 2, 'p%d, m%d, x%d, s%d = k%d.pop()' % (n, n, n, n, n))
                self.restore(n, ind + 2, True)
            else:
                self.w(ind + 2, 'p%d = k%d.pop()' % (n, n))
            self.w(ind + 2, 'ptr = p%d' % n)
            self.w(ind + 1, 'b%d = 1' % n)
            self.seq(tail, None, ind + 1, 'continue', inrep, succ)
            self.w(ind, fail)
            return None
        self.loop(n, iteration, minimum, minimum, ind, fail, snap, False)
        if minimum == maximum:
            return tail
        counted = maximum != MAXREPEAT
        self.w(ind, 'v%d = -1' % n)
        self.w(ind, 'b%d = 0' % n)
        self.w(ind, 'while True:')
        self.w(ind + 1, 'if b%d:' % n)
        self.restore(n, ind + 2, inrep)
        self.w(ind + 2, 'ptr = t%d' % n)    # where the tail failed
        self.w(ind + 2, 'if ptr == v%d%s: break' % (n, ' or c%d >= %d' % (n, maximum) if counted else ''))
        self.w(ind + 2, 'v%d = ptr' % n)
        iteration(ind + 2, 'break')
        if counted:
            self.w(ind + 2, 'c%d += 1' % n)
        self.w(ind + 1, 'b%d = 1' % n)
        self.w(ind + 1, 't%d = ptr' % n)
        self.save(n, ind + 1, inrep)
        self.seq(tail, None, ind + 1, 'continue', inrep, succ)
        self.w(ind, fail)
        return None

    def sub(self, body, inrep):
        """A function for the submatch at body: the new end or -1, its lastmark and lastindex in st."""
        key = (body, inrep)
        name = self.sub_names.get(key)
        if name is None:
            name = self.sub_names[key] = '_sub%d' % self.new()
            saved = self.out, self.live
            self.out = [(0, 'def %s(st, text, end, mark, ptr, lastmark, lastindex):' % name)]
            self.live = []
            self.seq(body, None, 1, 'return -1', inrep, 'sub')
            self.subs.append(self.out)
            self.out, self.live = saved
        return name

    # --- repeats whose iterations backtrack -------------------------------
    # Such a repeat is a function r(..., c, v) for its UNTIL (sre_lib.h's MAX_UNTIL or MIN_UNTIL) after
    # c iterations, the last of which started at v: it tries another iteration, whose end calls r(c + 1)
    # again, and the tail with the rest of the match. Each iteration costs a Python frame, so a match
    # deeper than the recursion limit raises RecursionError, and the entry reruns the interpreter.

    def context(self, succ):
        """The entry's locals the success code of succ reads, as arguments."""
        return {'match': ', start, match_all, adv', 'search': ', start, pos, adv'}.get(succ, '')

    def live_args(self, live):
        return ''.join([', c%d, v%d' % (item[0], item[0]) for item in live])

    def call_repeat(self, pc, ind, fail, inrep, succ):
        key = (pc, inrep, succ, tuple(self.live))
        name = self.rec_names.get(key)
        if name is None:
            name = self.rec_names[key] = self.repeat_function(pc, inrep, succ)
        self.w(ind, 'q = %s(st, text, end, mark, ptr, lastmark, lastindex%s%s, 0, -1)'
               % (name, self.context(succ), self.live_args(self.live)))
        self.w(ind, 'if q >= 0: return q')
        self.w(ind, fail)

    def next_iteration(self, ind, fail, succ):
        """At the UNTIL of the innermost live repeat: an iteration ended."""
        n, until, name = self.live[-1]
        self.w(ind, 'q = %s(st, text, end, mark, ptr, lastmark, lastindex%s%s, c%d + 1, v%d)'
               % (name, self.context(succ), self.live_args(self.live[:-1]), n, n))
        self.w(ind, 'if q >= 0: return q')
        self.w(ind, fail)

    def repeat_function(self, pc, inrep, succ):
        code = self.code
        minimum = code[pc + 2]
        maximum = code[pc + 3]
        body = pc + 4
        until = pc + 1 + code[pc + 1]
        tail = until + 1
        n = self.new()
        name = '_r%d' % n
        outer = self.live
        saved = self.out
        self.out = [(0, 'def %s(st, text, end, mark, ptr, lastmark, lastindex%s%s, c%d, v%d):'
                     % (name, self.context(succ), self.live_args(outer), n, n))]
        inner = outer + [(n, until, name)]

        def iteration(ind, fail):
            self.live = inner
            self.seq(body, None, ind, fail, True, succ)
            self.live = outer

        def tail_(ind, fail):
            self.seq(tail, None, ind, fail, inrep, succ)
        if minimum:
            self.w(1, 'if c%d < %d:' % (n, minimum))
            iteration(2, 'return -1')
        self.w(1, 'p%d = ptr' % n)
        self.save(n, 1, False)
        if code[until] == MAX_UNTIL:
            # another iteration while the last one advanced, else the tail
            self.w(1, 'if %sptr != v%d:' % ('' if maximum == MAXREPEAT else 'c%d < %d and ' % (n, maximum), n))
            self.w(2, 's%d = mark[:lastmark + 1] if lastmark >= 0 else None' % n)
            self.w(2, 'v%d = ptr' % n)
            self.w(2, 'while True:')
            iteration(3, 'break')
            self.w(2, 'if m%d >= 0: mark[:m%d + 1] = s%d' % (n, n, n))
            self.restore(n, 2, False)
            self.w(2, 'ptr = p%d' % n)
            tail_(1, 'return -1')
        else:
            # the tail first, then another iteration
            if inrep:
                self.w(1, 's%d = mark[:lastmark + 1] if lastmark >= 0 else None' % n)
            self.w(1, 'while True:')
            tail_(2, 'break')
            self.restore(n, 1, inrep)
            self.w(1, 'ptr = p%d' % n)
            self.w(1, 'if %sptr == v%d: return -1' % ('' if maximum == MAXREPEAT else 'c%d >= %d or ' % (n, maximum), n))
            self.w(1, 'v%d = ptr' % n)
            iteration(1, 'return -1')
        self.subs.append(self.out)
        self.out = saved
        return name

    def atomic_body(self, body, stop, ind, fail, inrep, succ):
        """Emit a body that never backtracks once matched: in line when it has no choice point, else
        as a call of a submatch function."""
        if self.try_straight(self.seq, body, stop, ind, fail, inrep, succ):
            return
        self.w(ind, 'q = %s(st, text, end, mark, ptr, lastmark, lastindex)' % self.sub(body, inrep))
        self.w(ind, 'if q < 0: %s' % fail)
        self.w(ind, 'ptr = q')
        self.w(ind, 'lastmark = st.lastmark')
        self.w(ind, 'lastindex = st.lastindex')

    def atomic(self, pc, ind, fail, inrep, succ):
        tail = pc + 1 + self.code[pc + 1]
        if self.code[tail - 1] != SUCCESS:
            raise _Unsupported
        self.atomic_body(pc + 2, tail - 1, ind, fail, inrep, succ)
        return tail

    def possessive(self, pc, ind, fail, succ):
        code = self.code
        end = pc + 1 + code[pc + 1]
        if code[end] != SUCCESS:
            raise _Unsupported

        def iteration(i, f):
            self.atomic_body(pc + 4, end, i, f, True, succ)
        snap = self.writes_marks(iteration, ind, 'break')
        self.loop(self.new(), iteration, code[pc + 2], code[pc + 3], ind, fail, snap, False)
        return end + 1

    def assert_(self, pc, ind, fail, inrep, succ):
        code = self.code
        back = code[pc + 2]
        after = pc + 1 + code[pc + 1]
        if code[after - 1] != SUCCESS:
            raise _Unsupported
        if back:
            self.w(ind, 'if ptr < %d: %s' % (back, fail))
        n = self.new()
        self.w(ind, 'p%d = ptr' % n)
        if back:
            self.w(ind, 'ptr -= %d' % back)
        self.atomic_body(pc + 3, after - 1, ind, fail, inrep, succ)
        self.w(ind, 'ptr = p%d' % n)
        return after

    def assert_not(self, pc, ind, fail, inrep):
        code = self.code
        back = code[pc + 2]
        body = pc + 3
        after = pc + 1 + code[pc + 1]
        if code[after - 1] != SUCCESS:
            raise _Unsupported
        self.w(ind, 'if ptr >= %d:' % back)
        if code[body] in _CHAR_OPS and _char_end(code, body) == after - 1:
            self.w(ind + 1, 'q = ptr - %d' % back)
            self.w(ind + 1, 'if q < end:')
            prep, e = self.cond(body)
            self.w(ind + 2, 'ch = text[q]')
            for line in prep:
                self.w(ind + 2, line)
            self.w(ind + 2, 'if %s: %s' % (e, fail))
            return after
        n = self.new()
        if inrep:
            self.w(ind + 1, 's%d = mark[:lastmark + 1] if lastmark >= 0 else None' % n)
        self.w(ind + 1, 'if %s(st, text, end, mark, ptr - %d, lastmark, lastindex) >= 0: %s'
               % (self.sub(body, inrep), back, fail))
        if inrep:
            self.w(ind + 1, 'if lastmark >= 0: mark[:lastmark + 1] = s%d' % n)
        return after

    # --- entries --------------------------------------------------------

    def match_entry(self):
        """_match(st, 0, True)."""
        code = self.code
        w = self.w
        w(0, 'def _entry(st):')
        w(1, 'text = st.text')
        w(1, 'end = st.end')
        w(1, 'mark = st.mark')
        w(1, 'ptr = start = st.ptr')
        w(1, 'match_all = st.match_all')
        w(1, 'adv = st.must_advance')
        w(1, 'lastmark = st.lastmark')
        w(1, 'lastindex = st.lastindex')
        pc = 0
        if code[0] == INFO:
            if code[3]:
                w(1, 'if 0 <= end - ptr < %d: return 0' % code[3])
            pc = 1 + code[1]
        self.seq(pc, None, 1, 'return 0', False, 'match')

    def search_entry(self, first):
        """_search_generic(st)."""
        code = self.code
        w = self.w
        w(0, 'def _entry(st):')
        w(1, 'text = st.text')
        w(1, 'end = st.end')
        w(1, 'mark = st.mark')
        w(1, 'start = st.start')
        w(1, 'if start > end: return 0')
        pc = 0
        flags = need = 0
        if code[0] == INFO:
            flags = code[2]
            need = code[3]
            if need:
                w(1, 'if end - start < %d: return 0' % need)
            pc = 1 + code[1]
        w(1, 'adv = st.must_advance')
        if flags & INFO_PREFIX:
            w(1, 'adv = False')
            skip = code[6]
            w(1, 'nxt = start')
            w(1, 'while nxt < end:')
            w(2, 'pos = _search_prefix(text, %s, nxt, end)' % self.const(_needles(code[7:7 + code[5]])))
            w(2, 'if pos < 0: return 0')
            w(2, 'nxt = pos + 1')
            if flags & INFO_LITERAL:
                w(2, 'st.start = pos')
                w(2, 'st.ptr = pos + %d' % skip)
                w(2, 'return 1')
            else:
                w(2, 'ptr = pos + %d' % skip)
                w(2, 'lastmark = lastindex = -1')
                self.seq(pc + 2 * skip, None, 2, 'continue', False, 'search')
            w(1, 'return 0')
            return
        tests = None
        if flags & INFO_CHARSET:
            tests = self.charset(5, 'ch')
        elif first is not None:
            tests = ' or '.join(['(%s)' % (self.charset(at, 'ch') if op == IN else 'ch %s %d' % (
                '==' if op == LITERAL else '!=', code[at])) for op, at in first])
        if tests is not None:
            w(1, 'adv = False')
            w(1, 'pos = start - 1')
            w(1, 'while True:')
            w(2, 'pos += 1')
            w(2, 'while pos < end:')
            w(3, 'ch = text[pos]')
            w(3, 'if %s: break' % tests)
            w(3, 'pos += 1')
            w(2, 'if pos >= end: return 0')
            w(2, 'ptr = pos')
            w(2, 'lastmark = lastindex = -1')
            self.seq(pc, None, 2, 'continue', False, 'search')
            return
        w(1, 'last = end')
        if need > 1:
            w(1, 'last = end - %d' % (need - 1))
            w(1, 'if last <= start: last = start')
        if code[pc] == AT and (code[pc + 1] == AT_BEGINNING or code[pc + 1] == AT_BEGINNING_STRING):
            # one attempt: a match can only start at the beginning
            w(1, 'pos = ptr = start')
            w(1, 'lastmark = lastindex = -1')
            w(1, 'while True:')
            self.seq(pc, None, 2, 'break', False, 'search')
            w(1, 'st.start = st.ptr = last')
            w(1, 'return 0')
            return
        w(1, 'pos = start - 1')
        w(1, 'while pos < last:')
        w(2, 'pos += 1')
        w(2, 'ptr = pos')
        w(2, 'lastmark = lastindex = -1')
        self.seq(pc, None, 2, 'continue', False, 'search')
        w(1, 'return 0')

    def source(self):
        lines = []
        for block in self.subs + [self.out]:
            for item in block:
                if item is not None:
                    lines.append('    ' * item[0] + item[1])
        return '\n'.join(lines) + '\n'


_compile_source = compile     # the builtin: this module's compile() comes below


def _generate(code, first, kind):
    """The specialized entry for kind 'match' or 'search', or None."""
    g = _Gen(code)
    try:
        if kind == 'match':
            g.match_entry()
        else:
            g.search_entry(first)
        source = g.source()
        namespace = dict(_NAMESPACE)
        namespace.update(g.consts)
        namespace['_code'] = code
        exec(_compile_source(source, '<sre>', 'exec'), namespace)
    except (_Unsupported, SyntaxError, RecursionError, MemoryError):
        return None
    return namespace['_entry']


class _Spec:
    """A pattern's entries: the specialized function, the generic one, or None while the pattern is
    cold. budget: the work the interpreter may still do before the pattern is specialized."""

    __slots__ = ('code', 'first', 'budget', 'match', 'search')

    def __init__(self, code, first):
        self.code = code
        self.first = first
        self.budget = 0 if _EAGER else _BUDGET_PER_WORD * len(code)
        self.match = None
        self.search = None


# The interpreter's work is charged as _CALL_COST per call plus one per character the match or
# search covered; a pattern is specialized once it has used _BUDGET_PER_WORD of that per code word,
# so the cost of generating and compiling its source (linear in the code) stays a fraction of it.
_CALL_COST = 32
_BUDGET_PER_WORD = 64
_CHECK = 'sre' in str(sys._xoptions.get('lucifer_stress', '')).split(',')
_EAGER = _CHECK or sys._xoptions.get('lucifer_jit') == 'always'


def _match_top(st):
    return _match(st, 0, True)


def _specialize(sp, kind):
    generic = _match_top if kind == 'match' else _search_generic
    f = _generate(sp.code, sp.first, kind)
    if f is None:
        f = generic
    elif _CHECK:
        f = _checked(f, generic)
    if kind == 'match':
        sp.match = f
    else:
        sp.search = f
    return f


def _checked(fast, generic):
    """fast, compared on every call with the interpreter on a copy of the state."""
    def run(st):
        twin = _State()
        for name in _State.__slots__:
            setattr(twin, name, getattr(st, name))
        twin.mark = list(st.mark)
        want = generic(twin)
        got = fast(st)
        if want != got or (want and (twin.start, twin.ptr, twin.lastmark, twin.lastindex, twin.mark[:twin.lastmark + 1])
                           != (st.start, st.ptr, st.lastmark, st.lastindex, st.mark[:st.lastmark + 1])):
            raise RuntimeError('sre specialization diverges on %r at %d: %r, generic %r' % (
                st.string, twin.pos, (got, st.start, st.ptr, st.lastmark, st.lastindex, st.mark),
                (want, twin.start, twin.ptr, twin.lastmark, twin.lastindex, twin.mark)))
        return got
    return run


def _rerun(st, generic):
    """generic(st) after a specialized entry ran out of frames. The entries start from a reset state
    (lastmark -1) and store into it only on success, but for the submatches' lastmark."""
    st.lastmark = st.lastindex = -1
    return generic(st)


def _match_entry(st):
    sp = st.pattern._spec
    f = sp.match
    if f is None:
        if sp.budget > 0:
            status = _match(st, 0, True)
            sp.budget -= _CALL_COST + (st.ptr - st.start if status else 0)
            return status
        f = _specialize(sp, 'match')
    try:
        return f(st)
    except RecursionError:
        return _rerun(st, _match_top)


def _search(st):
    sp = st.pattern._spec
    f = sp.search
    if f is None:
        if sp.budget > 0:
            status = _search_generic(st, sp)
            if status >= 0:
                sp.budget -= _CALL_COST + (st.ptr - st.start if status else 0)
                return status
            _state_reset(st)
        f = _specialize(sp, 'search')
    try:
        return f(st)
    except RecursionError:
        return _rerun(st, _search_generic)


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
    __slots__ = ('pattern', 'flags', 'groups', 'code', '_first', '_spec', 'isbytes', '_groupindex', '_indexgroup',
                 '__weakref__')

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
        return _new_match(self, st, _match_entry(st))

    def fullmatch(self, /, string, pos=0, endpos=_SSIZE_MAX):
        """Matches against all of the string."""
        st = _state_init(self, string, _ssize(pos), _ssize(endpos))
        st.match_all = True
        return _new_match(self, st, _match_entry(st))

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
        return self._step(_match_entry)

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
    self._spec = _Spec(words, self._first)
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
