# A polymorphic attribute site whose guard went generic (it failed for
# instances of several classes), where an instance now and then lacks the
# attribute its class's cache entry reads in line (deleted, so the class
# attribute answers). The entry's check still fails for that instance after
# the site went generic: no recompile can remove it, so it must cost an exit
# and nothing more. tools/jitcheck runs this at two sizes and requires that
# compiles and entries left to the interpreter do not grow with the
# iterations.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class F:
    state = 'P'

    def __init__(self):
        self.state = 'P'

    def done(self):
        return self.state != 'P'


class T(F):
    pass


class G(F):
    pass


def run(n):
    c = 0
    objs = [F(), T(), G()]
    odd = T()
    del odd.state
    for i in range(n):
        o = odd if i % 100 == 99 else objs[i % 3]
        c += o.done()
        if i % 1000 == 0:
            objs[i % 3].state = 'D'
            c += o.done()
            objs[i % 3].state = 'P'
    return c


print(run(N))
