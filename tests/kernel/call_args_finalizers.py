"""Builtin calls in a hot loop whose temporary arguments die in the call's
release: their finalizers call functions, rebind globals and raise (each
raise reported to the unraisable hook), and the loop's results stay right."""
import sys

seen = []
sys.unraisablehook = lambda u: seen.append(type(u.exc_value).__name__)
deaths = 0


def note(k):
    global deaths
    deaths += k
    return [k] * 3


class Dying:
    def __init__(self, k):
        self.k = k

    def __len__(self):
        return self.k

    def __del__(self):
        note(1)
        if self.k % 7 == 0:
            raise ValueError(self.k)


total = 0
for k in range(1, 3000):
    total += len(Dying(k)) + isinstance(Dying(k), int) + (id(Dying(k)) > 0)
print(total, deaths, len(seen), set(seen))
