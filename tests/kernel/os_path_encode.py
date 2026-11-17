# Paths reach the system calls encoded as the filesystem encoding does:
# ASCII, other characters as UTF-8, and lone surrogates (bytes that were not
# UTF-8, decoded with surrogateescape) back to their bytes.
import os
import tempfile

with tempfile.TemporaryDirectory() as d:
    for name in ("plain.txt", "café.txt", "日本.txt", "\U0001f600.txt", "raw\udcff.txt"):
        path = os.path.join(d, name)
        with open(path, "wb") as f:
            f.write(name.encode("utf-8", "surrogateescape"))
        st = os.stat(path)
        with open(path, "rb") as f:
            data = f.read()
        print(ascii(name), st.st_size, data == name.encode("utf-8", "surrogateescape"))
    print(sorted(ascii(n) for n in os.listdir(d)))
    print(sorted(os.listdir(os.fsencode(d))))
    try:
        os.stat(os.path.join(d, "missingé"))
    except FileNotFoundError as e:
        print(type(e).__name__, e.errno, ascii(os.path.basename(e.filename)))
    try:
        os.stat(os.path.join(d, "nul\0"))
    except ValueError as e:
        print(type(e).__name__)
