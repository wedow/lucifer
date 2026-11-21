"""C string precision in error messages counts whole UTF-8 bytes."""


def report(call, *args, **kwargs):
    try:
        call(*args, **kwargs)
    except Exception as exc:
        print(type(exc).__name__, str(exc))
        if isinstance(exc, NameError):
            print(exc.name == args[0])


for name in ("short", "é" * 100, "L" * 300, "é" * 150, "o" + "é" * 150, "界" * 67, "a" * 198 + "𐐀"):
    cls = type(name, (), {"__hash__": None})
    report(cls, x=1)
    report(object.__init__, cls(), 1)
    report(hash, cls())
    report(eval, name, {})
