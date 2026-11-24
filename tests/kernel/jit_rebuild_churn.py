# luc-1kz0: a mid-size handler, inlined into a hot loop, over objects of
# classes that turn up one after another through the run: the loop's root
# keeps learning types at the handler's sites, and guards there fail now
# and then. A rebuild for what was learned or for a failing guard is made
# when what the unit loses without it has cost what the build does, not
# for each new type. tools/jitcheck runs this with the tier's event log and
# requires that each such rebuild was paid for, and that the IR the run
# builds is at most its first builds and what the losses paid would build.
import sys


class Binding:
    def __init__(self, name, source):
        self.name = name
        self.source = source
        self.used = False

    def redefines(self, other):
        return isinstance(other, Binding) and self.name == other.name


kinds = [Binding]
for k in range(24):
    kinds.append(type('Binding%d' % k, (kinds[k % 6],), {'level': k}))


class Scope(dict):
    pass


class FunctionScope(Scope):
    uses_locals = False


def handle_load(scopes, name, node, seen):
    in_generators = None
    for scope in reversed(scopes):
        if isinstance(scope, FunctionScope) and scope.uses_locals:
            continue
        binding = scope.get(name, None)
        if binding is None:
            in_generators = False
            continue
        if getattr(binding, 'level', -1) > 20 and in_generators is False:
            seen.append(name)
        if isinstance(binding, kinds[3]) or isinstance(binding, kinds[5]):
            binding.used = (scope, node)
        elif binding.redefines(node):
            binding.used = True
        else:
            n = len(seen)
            if n > 8:
                del seen[:4]
            binding.used = n
        return binding
    seen.append(name)
    return None


def main(n):
    scopes = [Scope(), FunctionScope(), Scope()]
    names = ['n%d' % i for i in range(40)]
    seen = []
    total = 0
    for i in range(n):
        # A new class of binding every 600 iterations, cycling through the 25.
        cls = kinds[(i // 600) % len(kinds)]
        scope = scopes[i % 3]
        name = names[i % 40]
        scope[name] = cls(name, i)
        if i % 3 == 0:
            b = handle_load(scopes, names[(i * 7) % 40], scope[name], seen)
            total += 0 if b is None else 1
    return total


print(main(int(sys.argv[1]) if len(sys.argv) > 1 else 3000))
