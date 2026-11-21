import io
import marshal


def show(label, read, *args, result=True, **kwargs):
    try:
        value = read(*args, **kwargs)
    except Exception as exc:
        print(label, type(exc).__name__, str(exc))
    else:
        print(label, value if result else "accepted")


class Disabled:
    def __bool__(self):
        return False


class BrokenFlag:
    def __bool__(self):
        raise RuntimeError("cannot check allow_code")


def sample():
    pass


code = sample.__code__
for read, source in ((marshal.loads, memoryview), (marshal.load, io.BytesIO)):
    for value in (code, {"nested": [code]}):
        for flag in (False, Disabled()):
            show(read.__name__, read, source(marshal.dumps(value)), result=False, allow_code=flag)
    show(read.__name__, read, source(marshal.dumps([42])), allow_code=False)
    show(read.__name__, read, source(marshal.dumps([42])), allow_code=BrokenFlag())
    print(read.__name__, read(source(marshal.dumps(code)), allow_code=True).co_name)


class RecordingReader(io.BytesIO):
    def __init__(self, data, overreport=False):
        super().__init__(data)
        self.sizes = []
        self.overreport = overreport

    def readinto(self, buffer):
        self.sizes.append(len(buffer))
        count = super().readinto(buffer)
        return count + self.overreport


stream = RecordingReader(marshal.dumps(b"") + b"gap" + marshal.dumps("") + marshal.dumps(42))
print(marshal.load(stream), stream.read(3), repr(marshal.load(stream)), marshal.load(stream), stream.sizes)
show("oversized count", marshal.load, RecordingReader(marshal.dumps(42), overreport=True))

show("flag before read", marshal.load, io.StringIO(), allow_code=BrokenFlag())

show("not a buffer", marshal.loads, [ord("N")], allow_code=False)
show("noncontiguous", marshal.loads, memoryview(b"Nxxy")[::2], allow_code=False)
