# A for loop over a generator run in line is peeled: its first turn starts
# the generator, and the loop proper is built from the state that turn
# leaves. The first turn's header still tests for every yield the generator
# was seen to stand at, though only the start can reach it there, and on
# those arms a local the generator binds before its first yield (f) is
# unbound. Those arms are dead code that must not reach the backend
# (pyperformance dulwich_log's _parse_message crashed the compiler).
from io import BytesIO


def fields(data):
    f = BytesIO(data)
    k = None
    v = ""
    eof = False
    for line in f:
        if line.startswith(b" "):
            v += line[1:]
        else:
            if k is not None:
                yield (k, v)
            if line == b"\n":
                break
            (k, v) = line.split(b" ", 1)
    else:
        eof = True
        if k is not None:
            yield (k, v)
        yield (None, None)
    if not eof:
        yield (None, f.read())
    f.close()


def parse(data):
    named = []
    body = None
    for field, value in fields(data):
        if field is None:
            body = value
        else:
            named.append((field, value))
    return named, body


texts = [
    b"tree abc\nparent def\nauthor x y\n\nmessage body\n",
    b"tree abc\nparent def\nparent ghi\n continued\nauthor x y\n\nmsg\n",
    b"tree abc\nauthor x y",
]
total = 0
for i in range(20000):
    named, body = parse(texts[i % 3])
    total += len(named) + (len(body) if body else 0)
print(total)
