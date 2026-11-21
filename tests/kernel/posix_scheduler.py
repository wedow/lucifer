import copy
import os
import pickle
import posix


class SelfPid:
    def __index__(self):
        return 0


# Report the relationship to the calling process so output is independent of
# the test runner's scheduling policy and priority.
print(os.sched_getscheduler(SelfPid()) == os.sched_getscheduler(os.getpid()))
param = os.sched_getparam(SelfPid())
print(type(param) is os.sched_param is posix.sched_param,
      type(param.sched_priority) is int,
      param == os.sched_getparam(os.getpid()) == (param.sched_priority,))
print(os.sched_getscheduler.__self__ is posix, os.sched_getparam.__self__ is posix)
print([(name, getattr(os, name)) for name in dir(os) if name.startswith('SCHED_')])

value = [42]
param = os.sched_param(sched_priority=value)
print(param.n_fields, param.n_sequence_fields, param.n_unnamed_fields,
      param.__match_args__, hasattr(param, '__dict__'))
print(param, param[0] is value, param.sched_priority is value, param.__reduce__())
print(copy.copy(param) == param, copy.deepcopy(param) == param,
      copy.replace(param, sched_priority=None))
print([pickle.loads(pickle.dumps(param, protocol)) == param
       for protocol in range(pickle.HIGHEST_PROTOCOL + 1)])


def show_error(func, *args, **kwargs):
    try:
        func(*args, **kwargs)
    except (OSError, TypeError, OverflowError, AttributeError) as exc:
        print(type(exc).__name__, str(exc))


for getter in (os.sched_getscheduler, os.sched_getparam):
    for pid in (-1, 1 << 31, 1 << 100, 1.0):
        show_error(getter, pid)
    show_error(getter)
    show_error(getter, 0, 0)
    show_error(getter, pid=0)

show_error(os.sched_param)
show_error(os.sched_param, priority=1)
show_error(os.sched_param, 1, 2)
show_error(os.sched_param, 1, sched_priority=2)
show_error(setattr, param, 'sched_priority', 0)
show_error(setattr, param, 'other', 0)
show_error(copy.replace, param, priority=0)
