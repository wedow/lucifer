"""float pack bits."""
from support import loops


def run():
    import struct
    return [(x.hex(), struct.pack('>d', x).hex(), struct.unpack('>d', struct.pack('>d', x))[0].hex()) for x in (0.0, -0.0, 0.1, 1e-300)]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
