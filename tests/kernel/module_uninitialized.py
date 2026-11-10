# A module made by ModuleType.__new__ without __init__ has an empty dict,
# and attribute lookups on it (loops that warm the attribute caches) raise.
from types import ModuleType

for i in range(50):
    m = ModuleType.__new__(ModuleType)
    print(type(m.__dict__).__name__, m.__dict__, dir(m)) if i == 0 else None
    try:
        m.missing
    except AttributeError as e:
        if i == 0:
            print(e)
    try:
        m.__name__
    except AttributeError as e:
        if i == 0:
            print('no name:', e)
    if i == 0:
        print(m.__doc__ == (ModuleType.__doc__ or ''))
        print(getattr(m, 'x', 'default'), hasattr(m, 'x'))
m.x = 1
print(m.x, m.__dict__)
