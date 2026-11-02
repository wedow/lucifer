"""metaclass prepare order."""
from support import loops


def run():
    log = []
    class Meta(type):
        @classmethod
        def __prepare__(meta, name, bases): log.append(('prepare', name)); return {}
        def __new__(meta, name, bases, ns):
            log.append(('new', name, [k for k in ns if not k.startswith('__')]))
            return super().__new__(meta, name, bases, ns)
    class C(metaclass=Meta):
        a = 1
        b = 2
    return log, C.a, C.b


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
