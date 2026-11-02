"""exception groups."""
from support import loops


def run():
    out = []
    try: raise ExceptionGroup('group', [ValueError('a'), TypeError('b'), ValueError('c')])
    except* ValueError as e: out.append(('value', tuple(str(x) for x in e.exceptions)))
    except* TypeError as e: out.append(('type', tuple(str(x) for x in e.exceptions)))
    return out


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
