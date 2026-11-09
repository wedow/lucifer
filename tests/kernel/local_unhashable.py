# A threading.local subclass may be unhashable, as a dataclass with eq is.
import dataclasses
import threading


@dataclasses.dataclass
class Builder(threading.local):
    builder: object = None


b = Builder()
b.builder = 1
print(b.builder, Builder.__hash__)
seen = []
t = threading.Thread(target=lambda: seen.append(b.builder))
t.start()
t.join()
print(seen)
