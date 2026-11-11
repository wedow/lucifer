"""Classify one case from the [case, result] pairs of the two interpreters."""
import json
import re

# Errors from names the 3.15 generators use that 3.14 lacks: such a case cannot run on the oracle.
FUTURE = re.compile(r"name 'frozendict' is not defined|has no attribute 'get_theme'"
                    r"|unexpected keyword argument 'array_hook'")
# Results the runner records for a process that died or ran out of time on a case, and the
# worker's record of an exception that escaped the generator's own handling.
FAILED = ('crash', 'timeout', 'worker_error')
ADDRESS = re.compile(r' at 0x[0-9a-f]+')


def canon(value):
    return ADDRESS.sub(' at 0x', json.dumps(value, sort_keys=True))


def agree(want, again, got):
    """Equal apart from the parts that differ between two oracle runs (ids, timings)."""
    if canon(want) == canon(again):
        return canon(want) == canon(got)
    if isinstance(want, dict) and isinstance(again, dict) and want.keys() == again.keys():
        return isinstance(got, dict) and got.keys() == want.keys() and all(
            agree(want[key], again[key], got[key]) for key in want)
    if isinstance(want, list) and isinstance(again, list) and len(want) == len(again):
        return isinstance(got, list) and len(got) == len(want) and all(map(agree, want, again, got))
    return True


def compare(want, got, again=None):
    """'skip' when the oracle needs 3.15, 'error' when it has no result, else 'match' or 'mismatch'
    (without `again`, a second oracle run, a difference between runs counts against the target)."""
    expected = canon(want)
    if FUTURE.search(expected):
        return 'skip'
    if isinstance(want[1], dict) and any(key in want[1] for key in FAILED):
        return 'error'
    if expected == canon(got) or again is not None and agree(want, again, got):
        return 'match'
    return 'mismatch'
