"""pyexpat (Modules/pyexpat.c): a non-validating XML parser in Python with
Expat's handler interface: elements, attributes, character data, comments,
processing instructions, CDATA sections, the XML declaration, a doctype
declaration (its internal subset is skipped but entities declared there are
recognised), and namespaces. What it reports for well-formed documents is what
Expat reports; errors carry Expat's codes."""

import re
import sys
import types

EXPAT_VERSION = 'expat_2.7.1'
version_info = (2, 7, 1)
native_encoding = 'UTF-8'
XML_PARAM_ENTITY_PARSING_NEVER, XML_PARAM_ENTITY_PARSING_UNLESS_STANDALONE, XML_PARAM_ENTITY_PARSING_ALWAYS = 0, 1, 2
features = [('sizeof(XML_Char)', 1), ('sizeof(XML_LChar)', 1), ('XML_DTD', 0), ('XML_CONTEXT_BYTES', 1024), ('XML_NS', 0)]

_MESSAGES = {
    1: 'out of memory', 2: 'syntax error', 3: 'no element found', 4: 'not well-formed (invalid token)',
    5: 'unclosed token', 6: 'partial character', 7: 'mismatched tag', 8: 'duplicate attribute',
    9: 'junk after document element', 10: 'illegal parameter entity reference', 11: 'undefined entity',
    12: 'recursive entity reference', 13: 'asynchronous entity', 14: 'reference to invalid character number',
    15: 'reference to binary entity', 16: 'reference to external entity in attribute',
    17: 'XML or text declaration not at start of entity', 18: 'unknown encoding',
    19: 'encoding specified in XML declaration is incorrect', 20: 'unclosed CDATA section',
    21: 'error in processing external entity reference', 22: 'document is not standalone',
    23: 'unexpected parser state - please send a bug report', 24: 'entity declared in parameter entity',
    25: 'requested feature requires XML_DTD support in Expat', 26: 'cannot change setting once parsing has begun',
    27: 'unbound prefix', 28: 'must not undeclare prefix', 29: 'incomplete markup in parameter entity',
    30: 'XML declaration not well-formed', 31: 'text declaration not well-formed',
    32: 'illegal character(s) in public id', 33: 'parser suspended', 34: 'parser not suspended',
    35: 'parsing aborted', 36: 'parsing finished', 37: 'cannot suspend in external parameter entity',
    38: 'reserved prefix (xml) must not be undeclared or bound to another namespace name',
    39: 'reserved prefix (xmlns) must not be declared or undeclared',
    40: 'prefix must not be bound to one of the reserved namespace names',
}


class ExpatError(Exception):
    pass


error = ExpatError
ExpatError.__module__ = 'pyexpat'


def ErrorString(code, /):
    """Returns string error for given number."""
    return _MESSAGES.get(code)


errors = types.ModuleType('pyexpat.errors')
errors.codes = {message: code for code, message in _MESSAGES.items()}
errors.messages = dict(_MESSAGES)
for _code, _message in _MESSAGES.items():
    pass
for _name, _code in (('XML_ERROR_NO_MEMORY', 1), ('XML_ERROR_SYNTAX', 2), ('XML_ERROR_NO_ELEMENTS', 3),
                     ('XML_ERROR_INVALID_TOKEN', 4), ('XML_ERROR_UNCLOSED_TOKEN', 5),
                     ('XML_ERROR_PARTIAL_CHAR', 6), ('XML_ERROR_TAG_MISMATCH', 7),
                     ('XML_ERROR_DUPLICATE_ATTRIBUTE', 8), ('XML_ERROR_JUNK_AFTER_DOC_ELEMENT', 9),
                     ('XML_ERROR_PARAM_ENTITY_REF', 10), ('XML_ERROR_UNDEFINED_ENTITY', 11),
                     ('XML_ERROR_RECURSIVE_ENTITY_REF', 12), ('XML_ERROR_BAD_CHAR_REF', 14),
                     ('XML_ERROR_UNCLOSED_CDATA_SECTION', 20), ('XML_ERROR_UNBOUND_PREFIX', 27),
                     ('XML_ERROR_FINISHED', 36)):
    setattr(errors, _name, _MESSAGES[_code])
model = types.ModuleType('pyexpat.model')
sys.modules['pyexpat.errors'] = errors
sys.modules['pyexpat.model'] = model

_HANDLERS = (
    'StartElementHandler', 'EndElementHandler', 'ProcessingInstructionHandler', 'CharacterDataHandler',
    'UnparsedEntityDeclHandler', 'NotationDeclHandler', 'StartNamespaceDeclHandler',
    'EndNamespaceDeclHandler', 'CommentHandler', 'StartCdataSectionHandler', 'EndCdataSectionHandler',
    'DefaultHandler', 'DefaultHandlerExpand', 'NotStandaloneHandler', 'ExternalEntityRefHandler',
    'StartDoctypeDeclHandler', 'EndDoctypeDeclHandler', 'EntityDeclHandler', 'XmlDeclHandler',
    'ElementDeclHandler', 'AttlistDeclHandler', 'SkippedEntityHandler')

_NAME = r'[^\W\d][\w.\-:]*|:[\w.\-:]*'
_NAME_RE = re.compile(_NAME)
_ATTR_RE = re.compile(r'\s+(' + _NAME + r')\s*=\s*("([^"<]*)"|\'([^\'<]*)\')')
_REF_RE = re.compile(r'&(#x[0-9a-fA-F]+|#[0-9]+|[^;&<\s]*);?')
_PREDEFINED = {'lt': '<', 'gt': '>', 'amp': '&', 'quot': '"', 'apos': "'"}
_CONTROL_RE = re.compile('[\\x00-\\x08\\x0b\\x0c\\x0e-\\x1f]')
_XML_NS = 'http://www.w3.org/XML/1998/namespace'


class _Fail(Exception):
    def __init__(self, code, index):
        self.code, self.index = code, index


class xmlparser:
    """XML parser"""

    def __init__(self, encoding, separator):
        self.__dict__['_encoding'] = encoding
        self._separator = separator
        self._buffer = b''
        self._pos = 0
        self._started = False
        self._finished = False
        self._stack = []
        self._ns_stack = [{'xml': _XML_NS}]
        self._text = []
        self._entities = {}
        self._seen_root = False
        self._has_dtd = False
        self._line = 1
        self._line_pos = 0
        self.buffer_text = False
        self.buffer_size = 8192
        self.buffer_used = 0
        self.ordered_attributes = False
        self.specified_attributes = False
        self.namespace_prefixes = False
        self.returns_unicode = True
        self.ErrorCode = 0
        self.ErrorLineNumber = 1
        self.ErrorColumnNumber = 0
        self.ErrorByteIndex = 0
        for name in _HANDLERS:
            self.__dict__[name] = None
        self._param = 0

    @property
    def CurrentLineNumber(self):
        return self._line

    @property
    def CurrentColumnNumber(self):
        return self._pos - self._line_pos

    @property
    def CurrentByteIndex(self):
        return self._pos

    def SetParamEntityParsing(self, flag, /):
        """Controls parsing of parameter entities (including the external DTD subset)."""
        self._param = flag
        return 1

    def UseForeignDTD(self, flag=True, /):
        """Allows the application to provide an artificial external subset if one is not specified as part of the document instance."""

    def SetBase(self, base, /):
        """Set the base URL for the parser."""
        self._base = base

    def GetBase(self):
        """Return base URL string for the parser."""
        return getattr(self, '_base', None)

    def GetInputContext(self):
        """Return the untranslated text of the input that caused the current event."""
        return None

    def ExternalEntityParserCreate(self, context, encoding=None, /):
        """Create a parser for parsing an external entity based on the information passed to the ExternalEntityRefHandler."""
        return xmlparser(encoding, self._separator)

    # Parsing

    def Parse(self, data, isfinal=False, /):
        """Parse XML data."""
        if self._finished:
            self._fail(36, self._pos)
        if isinstance(data, str):
            data = data.encode('utf-8')
            self.__dict__['_encoding'] = 'utf-8'
        else:
            data = bytes(memoryview(data))
        self._buffer += data
        try:
            self._parse(isfinal)
        except _Fail as failure:
            self._fail(failure.code, failure.index)
        if isfinal:
            self._finished = True
        return 1

    def ParseFile(self, file, /):
        """Parse XML data from file-like object."""
        while True:
            data = file.read(2048)
            if not data:
                break
            self.Parse(data, False)
        return self.Parse(b'', True)

    def _fail(self, code, index):
        text = self._decoded
        line = text.count('\n', 0, index) + 1
        column = index - (text.rfind('\n', 0, index) + 1)
        self.ErrorCode, self.ErrorLineNumber, self.ErrorColumnNumber = code, line, column
        self.ErrorByteIndex = len(text[:index].encode('utf-8', 'replace'))
        exc = ExpatError('%s: line %d, column %d' % (_MESSAGES[code], line, column))
        exc.code, exc.lineno, exc.offset = code, line, column
        raise exc

    def _call(self, name, *args):
        handler = self.__dict__.get(name)
        if handler is not None:
            return handler(*args)

    def _flush_text(self):
        if self._text:
            text = ''.join(self._text)
            self._text = []
            self._call('CharacterDataHandler', text)

    def _data(self, text):
        if not text:
            return
        if self.buffer_text:
            self._text.append(text)
        else:
            self._call('CharacterDataHandler', text)

    def _decode(self):
        data = self._buffer
        encoding = self._encoding
        if not self._started:
            if data[:3] == b'\xef\xbb\xbf':
                data = data[3:]
            m = re.match(rb'<\?xml[^>]*?encoding\s*=\s*["\']([A-Za-z0-9._\-]+)["\']', data)
            if m and encoding is None:
                encoding = m.group(1).decode('ascii')
            if data[:2] in (b'\xff\xfe', b'\xfe\xff'):
                encoding = 'utf-16'
        self.__dict__['_decoding'] = encoding or 'utf-8'
        try:
            return data.decode(self._decoding)
        except UnicodeDecodeError as e:
            if e.start >= len(data) - 3 and not self._finished:
                return data[:e.start].decode(self._decoding)
            raise _Fail(4, e.start) from None
        except LookupError:
            raise _Fail(18, 0) from None

    def _name(self, name, attribute=False):
        """Expands a qualified name with the namespaces in scope."""
        if self._separator is None:
            return name
        prefix, colon, local = name.partition(':')
        if not colon:
            if attribute:
                return name
            uri = self._ns_stack[-1].get('')
            return name if not uri else uri + self._separator + name
        uri = self._ns_stack[-1].get(prefix)
        if uri is None:
            raise _Fail(27, self._pos)
        full = uri + self._separator + local
        return full + self._separator + prefix if self.namespace_prefixes else full

    def _expand(self, text, index, in_attribute=False):
        def sub(m):
            ref = m.group(1)
            if ref[:1] == '#':
                try:
                    code = int(ref[2:], 16) if ref[1:2] == 'x' else int(ref[1:])
                    return chr(code)
                except (ValueError, OverflowError):
                    raise _Fail(14, index + m.start()) from None
            if ref in _PREDEFINED:
                return _PREDEFINED[ref]
            if ref in self._entities:
                return self._entities[ref]
            raise _Fail(11, index + m.start())
        return _REF_RE.sub(sub, text)

    def _parse(self, final):
        text = self._decoded = self._decode()
        self._started = True
        i = self._pos
        n = len(text)
        while i < n:
            if text[i] != '<':
                j = text.find('<', i)
                if j < 0:
                    if not final and not self._stack:
                        break
                    j = n
                chunk = text[i:j]
                if not self._stack:
                    if chunk.strip():
                        raise _Fail(2 if not self._seen_root else 9, i + len(chunk) - len(chunk.lstrip()))
                    i = j
                    continue
                if not final and j == n:
                    # a reference may continue in the next chunk
                    cut = chunk.rfind('&')
                    if cut >= 0 and ';' not in chunk[cut:]:
                        self._emit_text(chunk[:cut], i)
                        i += cut
                        break
                self._emit_text(chunk, i)
                i = j
                continue
            rest = text[i:i + 12]
            if rest.startswith('<!--'):
                j = text.find('-->', i + 4)
                if j < 0:
                    if final:
                        raise _Fail(5, i)
                    break
                body = text[i + 4:j]
                if '--' in body:
                    raise _Fail(4, i + 4 + body.index('--') + 2)
                self._flush_text()
                self._call('CommentHandler', body)
                i = j + 3
            elif rest.startswith('<![CDATA['):
                j = text.find(']]>', i + 9)
                if j < 0:
                    if final:
                        raise _Fail(20, i)
                    break
                if not self._stack:
                    raise _Fail(2, i)
                self._flush_text()
                self._call('StartCdataSectionHandler')
                self._data(text[i + 9:j])
                self._flush_text()
                self._call('EndCdataSectionHandler')
                i = j + 3
            elif rest.startswith('<?'):
                j = text.find('?>', i + 2)
                if j < 0:
                    if final:
                        raise _Fail(5, i)
                    break
                m = _NAME_RE.match(text, i + 2)
                if m is None:
                    raise _Fail(4, i + 2)
                target, data = m.group(0), text[m.end():j].lstrip()
                if target == 'xml':
                    if i != 0 and not (i == 1 and text[0] == '﻿'):
                        raise _Fail(17, i)
                    self._xml_declaration(data, i)
                else:
                    self._flush_text()
                    self._call('ProcessingInstructionHandler', target, data)
                i = j + 2
            elif rest.startswith('<!DOCTYPE'):
                i = self._doctype(text, i, final)
                if i < 0:
                    i = self._pos
                    break
            elif rest.startswith('</'):
                j = text.find('>', i)
                if j < 0:
                    if final:
                        raise _Fail(5, i)
                    break
                name = text[i + 2:j].rstrip()
                if not self._stack:
                    raise _Fail(7, i + 2)
                if name != self._stack[-1][0]:
                    raise _Fail(7, i + 2)
                self._flush_text()
                raw, expanded, declared = self._stack.pop()
                self._call('EndElementHandler', expanded)
                for prefix in declared:
                    self._call('EndNamespaceDeclHandler', prefix)
                self._ns_stack.pop()
                if not self._stack:
                    self._seen_root = True
                i = j + 1
            else:
                j = self._start_tag_end(text, i)
                if j < 0:
                    if final:
                        raise _Fail(5, i)
                    break
                if self._seen_root and not self._stack:
                    raise _Fail(9, i)
                self._start_tag(text, i, j)
                i = j
        self._pos = i
        if final:
            if i < n and text[i:].strip():
                raise _Fail(3 if not self._seen_root else 5, i)
            if self._stack or not self._seen_root:
                raise _Fail(3, n)
        self._decoded = text

    def _emit_text(self, chunk, index):
        if not chunk:
            return
        bad = _CONTROL_RE.search(chunk)
        if bad is not None:
            raise _Fail(4, index + bad.start())
        if '&' not in chunk:
            self._data(chunk)
            return
        position = 0
        for m in _REF_RE.finditer(chunk):
            self._data(chunk[position:m.start()])
            ref = m.group(1)
            if ref[:1] == '#' or ref in _PREDEFINED:
                self._data(self._expand(m.group(0), index + m.start()))
            elif ref in self._entities:
                self._data(self._entities[ref])
            elif self._has_dtd and (self.DefaultHandlerExpand is not None or self.DefaultHandler is not None):
                self._flush_text()
                self._call('DefaultHandlerExpand' if self.DefaultHandlerExpand is not None else 'DefaultHandler',
                           '&%s;' % ref)
            else:
                raise _Fail(11, index + m.start())
            position = m.end()
        self._data(chunk[position:])

    def _start_tag_end(self, text, i):
        """Index after the '>' of the tag starting at i, or -1 if incomplete."""
        quote = None
        j = i + 1
        n = len(text)
        while j < n:
            c = text[j]
            if quote:
                if c == quote:
                    quote = None
            elif c in '"\'':
                quote = c
            elif c == '>':
                return j + 1
            j += 1
        return -1

    def _start_tag(self, text, i, end):
        m = _NAME_RE.match(text, i + 1)
        if m is None:
            raise _Fail(4, i + 1)
        raw = m.group(0)
        position = m.end()
        attributes = []
        names = set()
        while True:
            am = _ATTR_RE.match(text, position)
            if am is None:
                break
            name = am.group(1)
            if name in names:
                raise _Fail(8, am.start(1))
            names.add(name)
            value = am.group(3) if am.group(3) is not None else am.group(4)
            value = self._expand(re.sub(r'[\t\r\n]', ' ', value), am.start(2) + 1)
            attributes.append((name, value))
            position = am.end()
        tail = text[position:end - 1].strip()
        empty = tail == '/'
        if tail and not empty:
            bad = re.compile(r'\s+' + _NAME + r'\s*=\s*').match(text, position)
            if bad is not None:
                raise _Fail(4, bad.end())
            raise _Fail(4, position + (len(text[position:end - 1]) - len(text[position:end - 1].lstrip())))
        scope = dict(self._ns_stack[-1])
        declared = []
        plain = []
        for name, value in attributes:
            if name == 'xmlns' or name.startswith('xmlns:'):
                if self._separator is not None:
                    prefix = name[6:] if name != 'xmlns' else ''
                    scope[prefix] = value
                    declared.append(prefix)
                    self._flush_text()
                    self._call('StartNamespaceDeclHandler', prefix or None, value or None)
                    continue
            plain.append((name, value))
        self._ns_stack.append(scope)
        self._flush_text()
        expanded = self._name(raw)
        converted = []
        for name, value in plain:
            converted.append((self._name(name, True), value))
        if self.StartElementHandler is not None:
            if self.ordered_attributes:
                flat = []
                for name, value in converted:
                    flat.append(name)
                    flat.append(value)
                self.StartElementHandler(expanded, flat)
            else:
                self.StartElementHandler(expanded, dict(converted))
        self._stack.append((raw, expanded, declared))
        if empty:
            self._flush_text()
            self._stack.pop()
            self._call('EndElementHandler', expanded)
            for prefix in declared:
                self._call('EndNamespaceDeclHandler', prefix)
            self._ns_stack.pop()
            if not self._stack:
                self._seen_root = True

    def _xml_declaration(self, data, index):
        version = re.search(r'version\s*=\s*["\']([^"\']*)["\']', data)
        encoding = re.search(r'encoding\s*=\s*["\']([^"\']*)["\']', data)
        standalone = re.search(r'standalone\s*=\s*["\']([^"\']*)["\']', data)
        if version is None:
            raise _Fail(30, index)
        self._call('XmlDeclHandler', version.group(1), encoding.group(1) if encoding else None,
                   -1 if standalone is None else int(standalone.group(1) == 'yes'))

    def _doctype(self, text, i, final):
        m = re.compile(r'<!DOCTYPE\s+(' + _NAME + r')(?:\s+(?:SYSTEM\s+("[^"]*"|\'[^\']*\')|PUBLIC\s+("[^"]*"|\'[^\']*\')\s+("[^"]*"|\'[^\']*\')))?\s*').match(text, i)
        if m is None:
            if final:
                raise _Fail(2, i)
            return -1
        j = m.end()
        subset = text[j:j + 1] == '['
        end = j
        if subset:
            depth = 0
            quote = None
            k = j
            while k < len(text):
                c = text[k]
                if quote:
                    if c == quote:
                        quote = None
                elif c in '"\'':
                    quote = c
                elif c == ']':
                    break
                k += 1
            else:
                if final:
                    raise _Fail(5, i)
                return -1
            internal = text[j + 1:k]
            end = k + 1
            while end < len(text) and text[end] in ' \t\r\n':
                end += 1
        if end >= len(text) or text[end] != '>':
            if final:
                raise _Fail(2, end)
            return -1
        system = (m.group(2) or m.group(4) or '')[1:-1] or None
        pubid = (m.group(3) or '')[1:-1] or None
        self._has_dtd = True
        self._call('StartDoctypeDeclHandler', m.group(1), system, pubid, int(subset))
        if subset:
            for em in re.finditer(r'<!ENTITY\s+(' + _NAME + r')\s+("([^"]*)"|\'([^\']*)\')\s*>', internal):
                value = em.group(3) if em.group(3) is not None else em.group(4)
                self._entities[em.group(1)] = value
                self._call('EntityDeclHandler', em.group(1), 0, value, None, None, None, None)
        self._call('EndDoctypeDeclHandler')
        return end + 1

    def __setattr__(self, name, value):
        if name in _HANDLERS:
            self.__dict__[name] = value
        else:
            object.__setattr__(self, name, value)


xmlparser.__module__ = 'pyexpat'
XMLParserType = xmlparser


def ParserCreate(encoding=None, namespace_separator=None, intern=None):
    """Return a new XML parser object."""
    if encoding is not None and not isinstance(encoding, str):
        raise TypeError('ParserCreate() argument 1 must be str or None, not %s' % type(encoding).__name__)
    if namespace_separator is not None:
        if not isinstance(namespace_separator, str):
            raise TypeError('ParserCreate() argument 2 must be str or None, not %s'
                            % type(namespace_separator).__name__)
        if len(namespace_separator) > 1:
            raise ValueError('namespace_separator must be at most one character, omitted, or None')
    return xmlparser(encoding, namespace_separator)
