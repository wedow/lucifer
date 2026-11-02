"""dict alias graph."""
# difftest-state
from support import loops


def run():
    shared = [1, 2]; d = {'left': shared, 'right': shared}; d['self'] = d
    shared.append(3)
    return d


for _ in range(loops()):
    RESULT = run()
STATE = RESULT
print("state captured")
