"""Run one upstream generator inside an interpreter.

worker.py DIFFTEST SPEC SEED START STOP OUT

Generates cases [0, STOP) from SEED, as upstream does in one process (some generators keep the
objects behind a case in module state), then runs cases [START, STOP). OUT gets the tally of
dropped generator draws, then a [case, result] JSON line per case, flushed as each finishes.

SPEC is a script with its arguments ("pygrammar_diff --part lexer"), whose `common.main(gen, run)`
call names the generator, or MODULE:GEN:RUN for a script with a main of its own.
"""
import json
import random
import sys
import types

from compare import FUTURE


class Captured(Exception):
    pass


def load(difftest, spec):
    # 3.15's math.integer holds math's integer functions, which 3.14 has in math alone.
    import math
    math.integer = math
    sys.path.insert(0, difftest)
    import common
    if ':' in spec:
        module, gen, run = spec.split(':')
        module = __import__(module)
        return getattr(module, gen), getattr(module, run)
    script, *args = spec.split()

    def capture(gen, run, **_):
        raise Captured(gen, run)
    common.main = capture
    path = f'{difftest}/{script}.py'
    sys.argv = [path] + args
    module = sys.modules['__main__'] = types.ModuleType('__main__')  # the script looks itself up
    module.__file__ = path
    try:
        with open(path) as source:
            exec(compile(source.read(), path, 'exec'), module.__dict__)
    except Captured as captured:
        return captured.args
    raise SystemExit(f'{script} never called common.main')


def generate(gen, seed, count):
    """`count` cases, and how many draws were dropped: None, 3.15-only, or a generator exception."""
    rng = random.Random(seed)
    cases, dropped = [], {'none': 0, 'future': 0, 'error': 0}
    while len(cases) < count:
        try:
            case = gen(rng)
        except Exception as error:
            dropped['future' if FUTURE.search(str(error)) else 'error'] += 1
            if dropped['error'] > count:
                raise
            continue
        if case is None:
            dropped['none'] += 1
        else:
            cases.append(case)
    return cases, dropped


def dumps(value, **options):
    """JSON with no limit on int digits, which some generators set for their own cases."""
    limit = sys.get_int_max_str_digits()
    sys.set_int_max_str_digits(0)
    try:
        return json.dumps(value, **options)
    finally:
        sys.set_int_max_str_digits(limit)


def result(run, case):
    try:
        value = run(case)
    except Exception as error:
        value = {'worker_error': [type(error).__name__, str(error)]}
    try:
        return dumps(value)
    except Exception:
        return json.dumps({'worker_error': ['unencodable', repr(value)[:2000]]})


def encode(case):
    try:
        return dumps(case, default=repr)
    except TypeError:  # a key JSON has no form for
        return json.dumps(repr(case))


def main():
    difftest, spec, seed, start, stop, path = sys.argv[1:]
    gen, run = load(difftest, spec)
    cases, dropped = generate(gen, int(seed), int(stop))
    with open(path, 'w', buffering=1) as out:  # a line at a time, for the runner to read after a crash
        out.write(json.dumps(dropped) + '\n')
        for case in cases[int(start):]:
            out.write(f'[{encode(case)}, {result(run, case)}]\n')


if __name__ == '__main__':
    main()
