class Alias:
    def __init__(self, name, error=None):
        self.name = name
        self.error = error

    def __hash__(self):
        return hash(self.name)

    def __eq__(self, other):
        if self.error is not None:
            raise self.error
        return self.name == other


def load_name(globals_, locals_):
    return eval('target', globals_, locals_)


def load_global(globals_, locals_):
    return eval('(lambda: target)()', globals_, locals_)


for load in (load_name, load_global):
    print(load({'__builtins__': {'target': 'builtin'}}, {}))
    for namespace in ('locals', 'globals', 'builtins'):
        if namespace == 'locals' and load is load_global:
            continue
        for error in (None, KeyError('comparison failed')):
            globals_ = {'__builtins__': {'target': 'builtin'}}
            locals_ = {}
            table = {'locals': locals_, 'globals': globals_, 'builtins': globals_['__builtins__']}[namespace]
            if namespace == 'builtins':
                table.clear()
            table[Alias('target', error)] = namespace
            try:
                print(load(globals_, locals_))
            except KeyError as exc:
                print(type(exc).__name__, exc)

# Equal strings can have different identities, and the probe must pass deleted slots.
names = {f'name_{i}': i for i in range(180)}
for i in range(0, 180, 2):
    del names[f'name_{i}']
names[''.join(('café', '_名'))] = 'wide'
print(eval('name_179, café_名', {}, names))
try:
    eval('name_178', {'__builtins__': {}}, names)
except NameError as exc:
    print(type(exc).__name__, str(exc), exc.name)
