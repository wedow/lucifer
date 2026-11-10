"""Collections that run while a compiled loop allocates cycles: each
destructor sees the frame that allocated as it stands (its locals agree
with each other, before or after the pass's addition), and every cycle is
collected."""
import gc
import sys
from support import loops

checks = []
collected = [0]


class Node:
    def __init__(self):
        self.me = self

    def __del__(self):
        collected[0] += 1
        f = sys._getframe(1)
        while f is not None and f.f_code.co_name != 'churn':
            f = f.f_back
        if f is not None:
            v = f.f_locals
            i = v['i']          # before or after this pass's addition
            checks.append(v['acc'] in (i * (i - 1) // 2, i * (i + 1) // 2))


def churn(n):
    acc = 0
    for i in range(n):
        Node()
        acc += i
    return acc


# Once: the loop compiles at its back-edges within the call, and enough
# cycles are made that collections are scheduled while it runs.
TOTAL = churn(200000)
gc.collect()
DONE = collected[0] == 200000, bool(checks), all(checks)


def run():
    return TOTAL, DONE


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
