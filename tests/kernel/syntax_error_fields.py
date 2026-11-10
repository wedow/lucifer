# SyntaxError's fields from the constructor and from the compiler land in
# the C layout's words (msg, filename, lineno, offset, end_lineno,
# end_offset, text), so attributes and tracebacks agree with CPython.
import traceback

e = SyntaxError("m", ("f", 1, 2, "t", 3, 4))
print(e.filename, e.lineno, e.offset, e.text, e.end_lineno, e.end_offset)
for src in (" 1", "x = (", "1 +* 2"):
    try:
        compile(src, "<console>", "single")
    except SyntaxError as e:
        print(type(e).__name__, repr(e.text), e.lineno, e.offset, e.end_lineno, e.end_offset)
        print(traceback.format_exception_only(e))
