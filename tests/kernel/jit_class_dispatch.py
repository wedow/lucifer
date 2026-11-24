# luc-1kz0: a scheduler's loop over four tasks of four classes, which share
# their step method and differ in the work it calls. The loop meets the
# classes one after another as tasks wake up, so the method load's place
# learns each class in turn. Its guard's exits while the place is learning
# pay for builds that take what it learned, and the loop ends with the
# shared step in line: tools/jitcheck runs this at two sizes and requires
# that compiled calls do not grow with the iterations.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Task:
    def __init__(self, ident, link):
        self.ident = ident
        self.link = link
        self.count = 0
        self.held = True

    def step(self, wake):
        self.count += 1
        if self.count & 7 == 0:
            return self.link
        return self.work(wake)

    def is_held(self):
        return self.held


class Idle(Task):
    def work(self, wake):
        return self.link


class Worker(Task):
    def work(self, wake):
        self.ident = (self.ident * 5 + wake) & 1023
        return self.link


class Handler(Task):
    def work(self, wake):
        return self if self.count & 1 else self.link


class Device(Task):
    def work(self, wake):
        self.ident ^= wake
        return self.link


def schedule(first, steps, wake):
    t = first
    done = 0
    while done < steps:
        if t.is_held():
            t = t.link
        else:
            t = t.step(wake)
        done += 1
    return done


def run(n):
    d = Device(4, None)
    h = Handler(3, d)
    w = Worker(2, h)
    i = Idle(1, w)
    d.link = i
    tasks = [i, w, h, d]
    s = 0
    for k in range(n):
        if k < 4 * 400:
            tasks[k // 400].held = False    # a class at a time
        s += schedule(i, 40, k & 15)
    return s + sum(t.ident for t in tasks)


print(run(N))
