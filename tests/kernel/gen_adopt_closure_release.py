"""A loop that steps a generator its interpreted part started keeps
nothing of it once the loop is done: compiled code that runs the
generator's body in line, and a closure's generator stepped by it, never
holds the closure, whose cells name the call's objects (luc-4pg3:
SQLAlchemy's weak identity map kept a flushed object for ever)."""
import weakref


class Obj:
    pass


class Proc:
    def __init__(self, seq):
        self.seq = seq

    def _mappers(self, uow):
        return (m for m in self.seq if uow.dep[m])

    def _elements(self, uow):
        for mapper in self._mappers(uow):
            for state in uow.mappers[mapper]:
                yield state

    def execute(self, uow):
        for state in self._elements(uow):
            state.seen = 1


m = Obj()
proc = Proc([m])
states, uows = [], []
for i in range(200):
    st = Obj()
    uow = Obj()
    uow.mappers = {m: [st]}
    uow.dep = {m: True}
    states.append(weakref.ref(st))
    uows.append(weakref.ref(uow))
    proc.execute(uow)
    del uow, st
print("states alive", [i for i, r in enumerate(states) if r() is not None])
print("uows alive", [i for i, r in enumerate(uows) if r() is not None])
