# compile() with PyCF_ALLOW_INCOMPLETE_INPUT raises _IncompleteInputError,
# which codeop (pdb, code.interact) catches and drops.
import codeop

for src in ["(a **", "a = (", "if 1:", "x = 1", "def f(:", "a = {"]:
    for _ in range(3):
        try:
            r = codeop.compile_command(src)
        except SyntaxError as e:
            r = 'SyntaxError: ' + e.msg
    print(repr(src), r if r is None or isinstance(r, str) else type(r).__name__)

try:
    compile("(a,", "<s>", "single", flags=0x4000)
except SyntaxError as e:
    print(type(e).__name__, issubclass(type(e), SyntaxError), e.msg)
    del e
