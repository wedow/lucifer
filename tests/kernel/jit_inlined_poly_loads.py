# A hot method, inlined into its caller, loads attributes whose site meets
# instances of several classes (AWFY Richards' tasks): the site's
# polymorphic cache, which the interpreter feeds when the inlined frame's
# generic load leaves, answers it. tools/jitcheck runs this at two sizes
# (lane `inlinedpoly`) and requires that calls compiled code makes and
# exits do not grow with the iterations: the method stays inlined.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Task:
    def __init__(self, k):
        self.k = k
        self.n = 0

    def run(self):
        self.n = self.n + self.k
        return self.n


class Device(Task):
    def __init__(self, k):
        self.extra = 1
        super().__init__(k)


class Handler(Task):
    pass


def hot(n):
    tasks = [Task(1), Device(2), Handler(3)]
    total = 0
    for i in range(n):
        total += tasks[i % 3].run()
    return total


print(hot(N))
print(hot(7))
