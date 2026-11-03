import _codecs
import codecs


def show(label, thunk):
    try:
        print(label, repr(thunk()))
    except BaseException as e:
        print(label, type(e).__name__, e)


# The registry.
for name in ("utf-8", "UTF8", "utf_8", "U8", "latin-1", "ISO-8859-1", "ascii", "us-ascii", "utf-16", "utf-16-le",
             "utf_32_be", " utf  8 ", "utf-8-sig", "no such", "", "a.b"):
    show("lookup " + name, lambda: (lambda c: (c.name, type(c).__name__, c._is_text_encoding))(codecs.lookup(name)))
show("lookup type", lambda: codecs.lookup(3))
show("lookup null", lambda: codecs.lookup("a\0b"))
show("register type", lambda: codecs.register(3))
found = []


def search(name):
    found.append(name)
    if name == "mine":
        return codecs.CodecInfo(codecs.utf_8_encode, codecs.utf_8_decode, name="mine")
    if name == "bad":
        return (1, 2, 3)
    if name == "binary":
        return codecs.CodecInfo(lambda s, e="strict": (b"B", len(s)), lambda b, e="strict": ("D", len(b)),
                                name="binary", _is_text_encoding=False)


codecs.register(search)
show("mine", lambda: ("x\u00e9".encode("mine"), b"x\xc3\xa9".decode("mine"), codecs.lookup("MINE").name))
show("mine again", lambda: (codecs.lookup("Mine").name, found))
show("bad", lambda: codecs.lookup("bad"))
show("binary str", lambda: "x".encode("binary"))
show("binary bytes", lambda: b"x".decode("binary"))
show("binary codecs", lambda: (codecs.encode("xy", "binary"), codecs.decode(b"xy", "binary")))
show("open binary", lambda: open("stdio_codecs.tmp", "w", encoding="binary"))
codecs.unregister(search)
show("after unregister", lambda: codecs.lookup("mine2"))
show("encode", lambda: (codecs.encode("a\u20ac", "utf-8"), codecs.decode(b"a\xe2\x82\xac", "utf-8")))
show("encode errors", lambda: codecs.encode("a\u20ac", "ascii", "xmlcharrefreplace"))
show("encode bad errors type", lambda: codecs.encode("a", "ascii", 3))
show("str.encode odd", lambda: ("a\u20ac".encode("utf-16"), "a\u20ac".encode("utf_32_le"), b"\xff\xfea\x00".decode("utf-16")))

# Error handlers.
show("lookup_error", lambda: sorted(n for n in ("strict", "ignore", "replace", "xmlcharrefreplace",
                                                "backslashreplace", "namereplace", "surrogateescape",
                                                "surrogatepass") if codecs.lookup_error(n)))
show("lookup_error missing", lambda: codecs.lookup_error("nope"))
show("register_error", lambda: codecs.register_error("x", 3))


def custom(exc):
    print("custom", type(exc).__name__, exc.start, exc.end, exc.reason, exc.encoding, repr(exc.object))
    if isinstance(exc, UnicodeEncodeError):
        return ("<%d>" % ord(exc.object[exc.start]), exc.end)
    return ("?" * (exc.end - exc.start), exc.end)


codecs.register_error("custom", custom)
show("custom enc", lambda: "a\u20acb\U0001f600".encode("ascii", "custom"))
show("custom dec", lambda: b"a\xffb\xc3".decode("utf-8", "custom"))
show("custom dec16", lambda: b"a\x00\x00\xd8".decode("utf-16-le", "custom"))
show("custom latin", lambda: "\u0100x".encode("latin-1", "custom"))
codecs.register_error("back", lambda exc: ("", 0))
show("loop? no, pos back is fine", lambda: "x".encode("ascii", "back"))
codecs.register_error("bytes", lambda exc: (b"[B]", exc.end))
show("bytes repl", lambda: "a\u20acb".encode("ascii", "bytes"))
codecs.register_error("bad1", lambda exc: 3)
show("bad handler", lambda: "\u20ac".encode("ascii", "bad1"))
codecs.register_error("bad2", lambda exc: ("x", 99))
show("bad pos", lambda: "\u20ac".encode("ascii", "bad2"))
codecs.register_error("bad3", lambda exc: ("\u20ac", exc.end))
show("unencodable repl", lambda: "\u20ac".encode("ascii", "bad3"))
codecs.register_error("neg", lambda exc: ("-", -2))
show("neg pos", lambda: "\u20acab".encode("ascii", "neg"))
show("unknown handler on error", lambda: "\u20ac".encode("ascii", "nonexistent"))
show("unknown handler no error", lambda: "abc".encode("ascii", "nonexistent"))
show("namereplace", lambda: "a\x01\x00".encode("ascii", "namereplace"))
show("surrogatepass 16", lambda: ("\ud800".encode("utf-16-le", "surrogatepass"), b"\x00\xd8".decode("utf-16-le", "surrogatepass")))
show("surrogatepass 32", lambda: ("\udfff".encode("utf-32-be", "surrogatepass"), b"\x00\x00\xdf\xff".decode("utf-32-be", "surrogatepass")))
show("surrogatepass 8", lambda: ("\ud800".encode("utf-8", "surrogatepass"), b"\xed\xa0\x80".decode("utf-8", "surrogatepass")))
show("surrogateescape", lambda: ("\udc80\udcff".encode("utf-8", "surrogateescape"), b"\x80\xffabc".decode("ascii", "surrogateescape")))
show("handlers direct", lambda: (codecs.ignore_errors(UnicodeEncodeError("a", "xyz", 1, 2, "r")),
                                 codecs.replace_errors(UnicodeDecodeError("a", b"xyz", 1, 2, "r")),
                                 codecs.xmlcharrefreplace_errors(UnicodeEncodeError("a", "x\u20ac", 1, 2, "r")),
                                 codecs.backslashreplace_errors(UnicodeDecodeError("a", b"x\xff", 1, 2, "r")),
                                 codecs.replace_errors(UnicodeTranslateError("xyz", 1, 3, "r"))))
show("handler type", lambda: codecs.ignore_errors(ValueError()))
show("strict handler", lambda: codecs.strict_errors(KeyError("k")))

# utf-8 pieces and incremental decoding.
show("utf_8_decode final", lambda: (_codecs.utf_8_decode(b"ab\xc3", None, False), _codecs.utf_8_decode(b"ab\xc3", None, True)))
show("utf_8_decode tails", lambda: [_codecs.utf_8_decode(b"a\xf0\x9f\x98"[:i], "strict", False) for i in range(1, 5)])
show("utf_8_decode bad tail", lambda: _codecs.utf_8_decode(b"a\xe0\x80", "replace", False))
show("utf_8_decode memoryview", lambda: _codecs.utf_8_decode(memoryview(b"xyz"), "strict", True))
show("utf_8_decode str", lambda: _codecs.utf_8_decode("xyz"))
show("utf_8_encode bytes", lambda: _codecs.utf_8_encode(b"xyz"))
show("utf_8_encode", lambda: (_codecs.utf_8_encode("a\u20ac"), _codecs.utf_8_encode("\ud800", "surrogatepass")))
show("latin_1/ascii", lambda: (_codecs.latin_1_encode("\xe9"), _codecs.latin_1_decode(b"\xe9"), _codecs.ascii_decode(b"abc"),
                               _codecs.ascii_encode("abc")))
show("ascii_decode bad", lambda: _codecs.ascii_decode(b"a\x80"))
show("latin_1_encode bad", lambda: _codecs.latin_1_encode("\u0100"))
dec = codecs.getincrementaldecoder("utf-8")()
show("incremental utf-8", lambda: [dec.decode(b) for b in (b"\xe2", b"\x82", b"\xac!", b"\xf0\x9f")] + [dec.decode(b"", True)])
dec = codecs.getincrementaldecoder("utf-8")("replace")
show("incremental replace", lambda: [dec.decode(b) for b in (b"\xe2", b"\x82") ] + [dec.decode(b"", True)])
enc = codecs.getincrementalencoder("utf-16")()
show("incremental utf-16 enc", lambda: [enc.encode("a"), enc.encode("b"), enc.encode("c", True)])
dec = codecs.getincrementaldecoder("utf-16")()
show("incremental utf-16 dec", lambda: [dec.decode(b) for b in (b"\xff", b"\xfea", b"\x00b", b"\x00")] + [dec.getstate()])
dec = codecs.getincrementaldecoder("utf-32")()
show("incremental utf-32 dec", lambda: [dec.decode(b) for b in (b"\xff\xfe\x00", b"\x00a\x00\x00", b"\x00")])

# utf-16 and utf-32.
show("utf16 enc", lambda: (_codecs.utf_16_encode("a\U0001f600"), _codecs.utf_16_encode("a", None, -1), _codecs.utf_16_encode("a", None, 1)))
show("utf16 dec", lambda: (_codecs.utf_16_decode(b"\xff\xfea\x00"), _codecs.utf_16_decode(b"\xfe\xff\x00a"), _codecs.utf_16_decode(b"a\x00")))
show("utf16 ex", lambda: (_codecs.utf_16_ex_decode(b"\xff\xfea\x00"), _codecs.utf_16_ex_decode(b"a\x00", None, 0, True),
                          _codecs.utf_16_ex_decode(b"\x00a", None, 1, True)))
show("utf16 odd", lambda: (_codecs.utf_16_decode(b"a\x00b", "strict", False), _codecs.utf_16_decode(b"a\x00b", "strict", True)))
show("utf16 surrogate", lambda: _codecs.utf_16_le_decode(b"\x00\xd8a\x00", "strict", True))
show("utf16 lone low", lambda: _codecs.utf_16_le_decode(b"\x00\xdca\x00", "replace", True))
show("utf16 pair", lambda: _codecs.utf_16_le_decode(b"=\xd8\x00\xdea\x00", "strict", True))
show("utf16 high at end", lambda: (_codecs.utf_16_le_decode(b"a\x00=\xd8", "strict", False), _codecs.utf_16_le_decode(b"a\x00=\xd8", "strict", True)))
show("utf32", lambda: (_codecs.utf_32_encode("a\U0001f600"), _codecs.utf_32_be_encode("a"), _codecs.utf_32_decode(b"\xff\xfe\x00\x00a\x00\x00\x00"),
                       _codecs.utf_32_ex_decode(b"\x00\x00\xfe\xff\x00\x00\x00a", None, 0, True)))
show("utf32 bad", lambda: _codecs.utf_32_le_decode(b"\x00\x00\x11\x00", "strict", True))
show("utf32 surrogate", lambda: _codecs.utf_32_le_decode(b"\x00\xd8\x00\x00", "replace", True))
show("utf32 short", lambda: (_codecs.utf_32_le_decode(b"a\x00\x00", "strict", False), _codecs.utf_32_le_decode(b"a\x00\x00", "strict", True)))

# Escapes.
show("unicode_escape enc", lambda: _codecs.unicode_escape_encode("a\n\t\\\xe9\u20ac\U0001f600'\"\x7f"))
show("unicode_escape dec", lambda: _codecs.unicode_escape_decode(b"a\\n\\x41\\u20ac\\U0001f600\\101\\\\\\\n!"))
show("unicode_escape bad x", lambda: _codecs.unicode_escape_decode(b"\\x4"))
show("unicode_escape bad x replace", lambda: _codecs.unicode_escape_decode(b"\\x4g", "replace"))
show("unicode_escape bad U", lambda: _codecs.unicode_escape_decode(b"\\U00110000"))
show("unicode_escape trailing", lambda: (_codecs.unicode_escape_decode(b"ab\\"), _codecs.unicode_escape_decode(b"ab\\", None, False)))
show("raw enc", lambda: _codecs.raw_unicode_escape_encode("a\\\xe9\u20ac\U0001f600"))
show("raw dec", lambda: _codecs.raw_unicode_escape_decode(b"a\\\\u20ac\\u20ac\\n\\\\\\u0041"))
show("escape enc", lambda: _codecs.escape_encode(b"a\n\\'\"\xff\x00"))
show("escape dec", lambda: _codecs.escape_decode(b"a\\n\\x41\\101\\\\\\'"))
show("escape dec bad", lambda: _codecs.escape_decode(b"\\xzz"))
show("escape dec str", lambda: _codecs.escape_decode("a\\tb"))
show("codecs escape", lambda: ("a\n\u20ac".encode("unicode_escape"), b"\\u20ac".decode("unicode_escape")))

# charmap.
table = "".join(chr(i) for i in range(256))
show("charmap_build", lambda: sorted(_codecs.charmap_build("abc\ufffe").items()))
show("charmap_decode str", lambda: _codecs.charmap_decode(b"\x01\x02", "strict", "xyz"))
show("charmap_decode undefined", lambda: _codecs.charmap_decode(b"\x01\x05", "strict", "xyz"))
show("charmap_decode undefined replace", lambda: _codecs.charmap_decode(b"\x01\x05", "replace", "xy\ufffe"))
show("charmap_decode dict", lambda: _codecs.charmap_decode(b"\x01\x02\x03", "ignore", {1: "a", 2: 0x20ac, 3: None}))
show("charmap_decode None", lambda: _codecs.charmap_decode(b"\xe9", None, None))
show("charmap_encode dict", lambda: _codecs.charmap_encode("abc", "strict", {97: 1, 98: b"zz", 99: 3}))
show("charmap_encode undefined", lambda: _codecs.charmap_encode("abx", "strict", {97: 1, 98: 2}))
show("charmap_encode replace", lambda: _codecs.charmap_encode("abx", "replace", {97: 1, 98: 2, 63: 9}))
show("charmap_encode build", lambda: _codecs.charmap_encode("ab", "strict", _codecs.charmap_build("xab")))
show("charmap_encode None", lambda: _codecs.charmap_encode("\xe9", "strict", None))
show("charmap_encode bad value", lambda: _codecs.charmap_encode("a", "strict", {97: "str"}))
show("charmap_encode range", lambda: _codecs.charmap_encode("a", "strict", {97: 300}))
show("readbuffer", lambda: _codecs.readbuffer_encode(b"abc"))

# StreamReader and friends over BytesIO need io; exercise codec classes directly.
show("encodings.aliases", lambda: __import__("encodings").normalize_encoding("UTF-8 x"))
show("EncodedFile-ish", lambda: codecs.lookup("utf-8").incrementalencoder().encode("\xe9"))
show("BOM", lambda: (codecs.BOM_UTF8, codecs.BOM_UTF16_LE, codecs.BOM_UTF32_BE))
show("iterencode", lambda: list(codecs.iterencode(iter(["a", "\xe9"]), "utf-8")))
show("iterdecode", lambda: list(codecs.iterdecode(iter([b"\xc3", b"\xa9a"]), "utf-8")))
show("utf-8-sig", lambda: ("a".encode("utf-8-sig"), b"\xef\xbb\xbfa".decode("utf-8-sig"), b"a".decode("utf-8-sig")))
