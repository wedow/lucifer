# A loop carries a value unchanged in one local while another local takes
# it on every turn: each local has its reference, and the object lives
# until the last of them lets go, in compiled code as in the interpreter.
log = []


class Noisy:
    def __init__(self, name):
        self.name = name

    def __del__(self):
        log.append(('del', self.name))


def carry(a, b, n):
    i = 0
    while i < n:
        a = b
        if i < 0:
            b = None
        i += 1
    return a


def carry_two(a, b, c, n):
    i = 0
    while i < n:
        a = c
        b = c
        if i < 0:
            c = None
        i += 1
    return a, b


for k in range(40):
    b = Noisy(k)
    r = carry(None, b, 10)
    log.append(('back', k, r is b))
    del r, b
    log.append(('dropped', k))
    c = Noisy(-k - 1)
    r = carry_two(None, None, c, 10)
    log.append(('back', -k - 1, r[0] is c, r[1] is c))
    del r, c
    log.append(('dropped', -k - 1))

print(len(log))
for entry in log[:8] + log[-8:]:
    print(entry)
print(all(log[i:i + 3] == [('back', k, True), ('del', k), ('dropped', k)]
          for i, k in zip(range(0, len(log), 6), range(40))))
