# A returning generator is finished before its locals go (clear_gen_frame):
# a finalizer they run sees gi_running False, gi_frame None, and next()
# raising StopIteration, in compiled code as in the interpreter.
class Probe:
    def __del__(self):
        g = gens[-1]
        try:
            next(g)
            r = 'resumed'
        except StopIteration:
            r = 'stop'
        except ValueError as e:
            r = str(e)
        seen.add((g.gi_running, g.gi_frame is None, r))


def gen(n):
    p = Probe()
    yield n


gens = []
seen = set()
for i in range(2000):
    g = gen(i)
    gens.append(g)
    for _ in g:
        pass
print(sorted(seen))
