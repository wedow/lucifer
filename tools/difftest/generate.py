"""Oracle-venv-only Hypothesis generation and shrinking; never imported by the host runner."""
import argparse
import json
from pathlib import Path
import sys
import time

import hypothesis
from hypothesis import given, seed, settings, Phase, strategies as st

from run import command, compare_case, digest, pinned_oracle, positive, probe


def programs():
    leaves = st.one_of(st.integers(-1000, 1000).map(repr), st.sampled_from([
        'x', 'len(xs)', 'len(s)', 'sum(xs)', 'xs[0]', 'd.get(0, 0)',
        str(2**63-1), str(-2**63), str(2**100+1)]))
    expressions = st.recursive(leaves, lambda child: st.one_of(
        st.tuples(child, st.sampled_from(['+', '-', '*', '//', '%', '&', '|', '^']), child)
            .map(lambda row: '(' + ' '.join(row) + ')'),
        st.tuples(st.sampled_from(['abs', 'int', 'bool']), child)
            .map(lambda row: row[0] + '(' + row[1] + ')'),
        st.tuples(child, st.integers(0, 8)).map(lambda row: '(' + row[0] + ' << ' + str(row[1]) + ')')
    ), max_leaves=10)
    expressions = st.one_of(expressions, st.sampled_from([
        's[::-1]', 's.casefold()', 's.split()', 'list(d.items())', 'xs[:]',
        'tuple(xs)', 'str(x)', 'bytes([x & 255])', 'sorted(xs)', 'x / 3']))
    operations = st.tuples(st.sampled_from(['append', 'insert', 'pop', 'reverse',
        'slice', 'set', 'delete', 'default', 'sort', 'extend']), st.integers(-30, 30))
    return st.fixed_dictionaries({
        'xs': st.lists(st.integers(-100, 100), max_size=8),
        's': st.text(alphabet='abéß😀\x00\ud800 ', max_size=12),
        'rare': st.one_of(st.integers(-100, 100), st.sampled_from([2**63, -(2**63)-1, 2**130])),
        'ops': st.lists(operations, min_size=4, max_size=12),
        'exprs': st.lists(expressions, min_size=4, max_size=10)})


def render(spec, rng_seed):
    lines = ['"""Bounded builtin operation trace; replay does not need Hypothesis."""',
             '# difftest-state', '# hypothesis-seed: ' + str(rng_seed),
             'from support import loops', '', '', 'def hot(x):',
             '    xs = ' + ascii(spec['xs']), '    alias = xs',
             '    d = {0: x, 1: -x}', '    s = ' + ascii(spec['s']), '    trace = []']
    for op, n in spec['ops']:
        statement = {'append': f'xs.append({n})', 'insert': f'xs.insert({n}, x)',
            'pop': f'xs.pop({n})', 'reverse': 'xs.reverse()',
            'slice': f'xs[1:3] = [{n}, x]', 'set': f'd[{n}] = x',
            'delete': f'd.pop({n})', 'default': f'd.setdefault({n}, x)',
            'sort': 'xs.sort(key=lambda v: v % 3)', 'extend': f'xs.extend([{n}])'}[op]
        statements = ['        ' + statement, '        result = None'] if op in ('slice', 'set') else ['        result = ' + statement]
        lines += ['    try:', *statements,
                  '        trace.append(("ok", result, list(xs), list(d.items())))',
                  '    except Exception as error:',
                  '        trace.append((type(error).__name__, error.args))']
    lines += ['    values = []']
    for expression in spec['exprs']:
        lines += ['    try:', '        values.append(' + expression + ')',
                  '    except Exception as error:', '        values.append((type(error).__name__, error.args))']
    lines += ['    return {"xs": xs, "alias": alias, "d": d, "trace": trace, "values": values}',
              '', '', 'for _ in range(loops()):', '    STATE = hot(7)',
              'STATE = hot(' + str(spec['rare']) + ')', 'print("state captured")', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['emit', 'fuzz'])
    parser.add_argument('--seed', type=int, required=True)
    parser.add_argument('--count', type=int, default=32)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--oracle')
    parser.add_argument('--python')
    parser.add_argument('--loops', type=int, default=32)
    parser.add_argument('--timeout', type=positive, default=15)
    parser.add_argument('--budget', type=positive, default=300)
    args = parser.parse_args()
    args.output = args.output.resolve()
    if sys.prefix == sys.base_prefix or sys.version_info[:3] != (3, 14, 10) or sys.implementation.name != 'cpython' or not hasattr(sys, '_is_gil_enabled') or sys._is_gil_enabled():
        parser.error('run this generator in the pinned free-threaded oracle venv')
    if hypothesis.__version__ != '6.152.1':
        parser.error('install the hashed requirements.lock')
    if args.count < 1 or args.loops < 1 or args.timeout <= 0:
        parser.error('count, loops and timeout must be positive')
    args.output.mkdir(parents=True, exist_ok=True)
    if any(args.output.glob('*.py')) or (args.output / 'generation.json').exists():
        parser.error('output already contains programs; choose an empty directory')
    if args.action == 'fuzz':
        if not args.oracle or not args.python:
            parser.error('fuzz requires --oracle and --python')
        oracle, target = command(args.oracle), command(args.python)
        for arm, executable in [('oracle', oracle), ('target', target)]:
            result = probe(executable, args.output / ('probe-' + arm), args.timeout)
            if result['kind'] != 'EXIT':
                parser.error(arm + ' cannot start; no fuzzing coverage')
            if arm == 'oracle' and not pinned_oracle(result['info']):
                parser.error('fuzzing requires the pinned free-threaded oracle')
    records = []
    deadline = time.monotonic() + args.budget
    provenance = {'hypothesis': hypothesis.__version__, 'seed': args.seed,
                  'generator_sha256': digest(Path(__file__).read_bytes()), 'cases': records}

    @seed(args.seed)
    @settings(max_examples=args.count, deadline=None, database=None,
              phases=(Phase.generate, Phase.shrink) if args.action == 'fuzz' else (Phase.generate,))
    @given(programs())
    def exercise(spec):
        if time.monotonic() >= deadline:
            raise RuntimeError('generation/fuzz budget exhausted; retain attempted sources and replay the seed')
        source = render(spec, args.seed)
        name = f'seed_{args.seed}_{len(records):04d}.py'
        path = args.output / name
        path.write_text(source, encoding='utf-8')
        records.append({'program': name, 'sha256': digest(source.encode()), 'spec': spec})
        (args.output / 'generation.json').write_text(json.dumps(provenance, ensure_ascii=True, indent=2) + '\n')
        if args.action == 'fuzz':
            status, _, details = compare_case(str(path.resolve()),
                {'sha256': digest(source.encode()), 'state': True, 'timing': False, 'exit': 0},
                oracle, target, args.output / 'runs', args.loops, args.timeout)
            if status in ('ERROR', 'NOTRUN') or details.get('oracle', {}).get('after_imports', {}).get('gil') is True:
                raise RuntimeError('invalid differential observation: ' + repr(details))
            if status != 'PASS':
                # Hypothesis reruns the final shrunk failure; preserve its actual source.
                (args.output / 'minimized.py').write_text(source, encoding='utf-8')
                (args.output / 'minimized.json').write_text(json.dumps(
                    {'seed': args.seed, 'status': status, 'spec': spec, 'program': name}, indent=2))
                raise AssertionError('differential finding: ' + status)

    exercise()
    print(f'{len(records)} generated examples; seed {args.seed}')


if __name__ == '__main__':
    main()
