#!/usr/bin/env python3
"""Pinned free-threading corpus, driven by the host's Python standard library."""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shlex
import sys
import time
import subprocess
import signal
import platform
import resource
import shutil
import tempfile
from contextlib import nullcontext

from artifacts import verify
from prepare import stage_tests

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('libtest_driver', ROOT / 'tools/libtest/run.py')
lib = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lib)
lib.BRIDGE = HERE / 'regrtest.py'
lib.PROBE = lib.PROBE.replace("'test':test.__file__", "'venv':sys.prefix != sys.base_prefix,'test':test.__file__")
BAD = {'FAIL', 'ERROR', 'XPASS', 'TIMEOUT', 'CRASH', 'NOTRUN'}


FATAL_SIGNALS = {int(s) for s in (signal.SIGSEGV, signal.SIGABRT, signal.SIGBUS, signal.SIGILL, signal.SIGFPE, signal.SIGKILL)}


def execute(command, env, cwd, timeout, log, stderr_log=None, worker_pid=None):
    """Run a command; a shell wrapper reports a fatal signal as exit 128+N, returned as -N."""
    started = time.monotonic()
    with log.open('wb') as output, (stderr_log.open('wb') if stderr_log else nullcontext(output)) as errors:
        try:
            child = subprocess.Popen(command, env=env, cwd=cwd, stdout=output,
                                     stderr=errors, start_new_session=True)
        except OSError as error:
            output.write(str(error).encode())
            return None, False, time.monotonic() - started
        hung = False
        try:
            child.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            hung = True
        finally:
            # Own both the controller group and regrtest's separate worker group.
            groups = [child.pid]
            if worker_pid and worker_pid.exists():
                try:
                    groups.append(int(worker_pid.read_text()))
                except ValueError:
                    pass
            for group in groups:
                try:
                    os.killpg(group, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            child.wait()
    code = child.returncode
    return 128 - code if code - 128 in FATAL_SIGNALS else code, hung, time.monotonic() - started


lib.execute = execute


def dump(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def identity(args, env, raw):
    info = lib.probe(args.command, env, raw, args.startup_timeout)
    if not info:
        return None
    # Do not force the GIL off: automatic fallback is an invalid no-GIL run.
    if not info.get('free_threaded') or info.get('gil_enabled') is not False:
        return None
    return {k: v for k, v in info.items() if k != 'test'}


def cpython_inventory(args, env, raw):
    import ast
    revision = subprocess.check_output(['git', '-C', str(args.source), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != 'b703b2c5e704341d61e367b9b940b77ed11dded0':
        raise ValueError('CPython source must be the pinned official commit')
    source = args.source / 'Lib/test/libregrtest/tsan.py'
    tree = ast.parse(source.read_text())
    lists = {node.targets[0].id: ast.literal_eval(node.value) for node in tree.body
             if isinstance(node, ast.Assign) and isinstance(node.value, ast.List)}
    # Upstream source is read, never imported by the host.
    baseline = json.loads((ROOT / 'results/libtest/inventory.json').read_text())['free-threaded']
    selectors = {'serial': sorted(set(lists['TSAN_TESTS'] + lists['TSAN_PARALLEL_TESTS'])),
                 'parallel': lists['TSAN_PARALLEL_TESTS']}
    modules = {}
    for lane, names in selectors.items():
        for module in sorted(baseline):
            if any(module.removeprefix('test.') == n or module.removeprefix('test.').startswith(n + '.') for n in names):
                log = raw / (module + '.collect.log')
                code, hung, _ = lib.execute([*args.command, '-m', 'test', '--list-cases', module],
                                            env, raw, 120, log, raw / (module + '.collect.stderr'))
                cases = sorted({line for line in log.read_text().splitlines() if line})
                if code != 0 or hung or (not cases and baseline[module]['cases']):
                    raise ValueError('empty or failed CPython collection: ' + module)
                modules[lane + '/' + module] = cases
    inventory = {'units': modules, 'upstream_selectors': selectors,
                 'source_commit': 'b703b2c5e704341d61e367b9b940b77ed11dded0',
                 'source_files': {p: hashlib.sha256((args.source / p).read_bytes()).hexdigest() for p in
                                  ['Lib/test/libregrtest/tsan.py', '.github/workflows/reusable-san.yml',
                                   'Tools/tsan/suppressions.txt', 'Tools/tsan/suppressions_free_threading.txt']}}
    return inventory


def package_timeout(args, package):
    if args.timeout_override is not None:
        return args.timeout
    return args.lock['packages'][package].get('unit_timeout', args.timeout)


def package_test_timeout(args, package):
    return min(package_timeout(args, package), args.lock['packages'][package].get('test_timeout', 120))


def package_command(args, package, lane, events, collect=False, selectors=None, test_root=None):
    config = args.lock['packages'][package]
    # A private test-only copy avoids importing packages from their source tree.
    tests = test_root or args.work / 'tests' / package
    env = lib.environment('installed')
    env.update(PYTHONPATH=os.pathsep.join([str(HERE), str(args.work / 'venv/lib/python3.14t/site-packages'),
                                                 *(str(tests / entry) for entry in config.get('pythonpath', []))]),
               PYTEST_DISABLE_PLUGIN_AUTOLOAD='1', PIP_NO_INDEX='1', PIP_NO_BUILD_ISOLATION='1', FT_OBSERVATIONS=str(events),
               FT_SEED=str(args.seed),
               FT_SERIAL_FIXTURES=json.dumps(config.get('serial_fixtures', [])),
               FT_SERIAL_GLOBALS=json.dumps(config.get('serial_globals', [])), PYTHONHASHSEED=str(args.seed % 4294967296),
               OPENBLAS_NUM_THREADS='1', OMP_NUM_THREADS='1')
    env['PATH'] = str(args.work / 'venv/bin') + os.pathsep + env.get('PATH', '')
    if package == 'setuptools-rust':
        env['CARGO_HOME'] = str(args.work / 'cargo')
    if package == 'joblib':
        for key in ('OPENBLAS_NUM_THREADS', 'OMP_NUM_THREADS', 'MKL_NUM_THREADS', 'BLIS_NUM_THREADS',
                    'VECLIB_MAXIMUM_THREADS', 'NUMBA_NUM_THREADS', 'NUMEXPR_NUM_THREADS'):
            env.pop(key, None)
    command = [*args.command, '-m', 'pytest', '-p', 'pytest_observer', '-p', 'pytest_run_parallel.plugin', '-p', 'hypothesis.extra.pytestplugin',
               '-p', 'pytest_timeout', '-p', 'pytest_cov', '-p', 'pytest_mock',
               '-p', 'pytest_asyncio.plugin', '-p', 'pytest_benchmark.plugin',
               '-p', 'xdist.plugin', '-p', 'pytest_instafail',
               '-o', 'addopts=', '-o', 'asyncio_default_fixture_loop_scope=function', '--rootdir', str(tests), '--confcutdir', str(tests),
               '--timeout', str(package_test_timeout(args, package)), '--timeout-method=thread', '--benchmark-disable',
               '--hypothesis-seed', str(args.seed), '-v', '--tb=short']
    command += config.get('pytest_args', [])
    if lane == 'parallel':
        command += ['--parallel-threads', str(args.threads or config['threads']), '--iterations', str(config.get('parallel_iterations', 1))]
    if collect:
        command += ['--collect-only']
    if selectors and sum(len(value) for value in selectors) > 16000:
        selection_file = events.with_suffix('.selection')
        selection_file.write_text('\n'.join(selectors) + '\n')
        command += ['@' + str(selection_file)]
    else:
        command += selectors or config['selectors']
    return command, env, tests


def package_inventory(args, env, raw):
    units = {}
    failures = []
    dispositions = {}
    deadline = time.monotonic() + args.budget
    for package in args.lock['packages']:
        for lane in ['serial', 'parallel']:
            if time.monotonic() + args.timeout + 5 > deadline:
                raise ValueError('collection budget reached; no inventory published')
            events = raw / (package + '-' + lane + '.jsonl')
            events.unlink(missing_ok=True)
            command, local, cwd = package_command(args, package, lane, events, collect=True)
            code, hung, _ = lib.execute(command, local, cwd, args.timeout, raw / (package + '-' + lane + '.log'))
            records = lib.json_lines(events)
            cases = sorted({r['collected'] for r in records if 'collected' in r})
            if code != 0 or hung or not cases or not gil_valid(records):
                failures.append(package + '/' + lane)
                print('collection ERROR', package, lane, flush=True)
                continue
            unit = lane + '/' + package
            units[unit] = cases
            dispositions[unit] = [{'node': r['collection_node'], 'status': r['collection_status'],
                                   'reason': (re.search(r"No module named ['\"][^'\"]+['\"]", r.get('reason', '')) or ['upstream collection skip'])[0]}
                                  for r in records if 'collection_node' in r]
            print('collect', lane, package, len(cases), flush=True)
    if failures:
        dump(raw / 'partial-inventory.json', {'units': units, 'failures': failures})
        raise ValueError('failed or empty collections: ' + ', '.join(failures))
    return {'units': units, 'collection_dispositions': dispositions, 'lock_sha256': hashlib.sha256(args.lock_path.read_bytes()).hexdigest()}


def gil_valid(records):
    states = [r for r in records if 'gil_enabled' in r]
    return bool(states) and all(r.get('free_threaded') is True and r['gil_enabled'] is False for r in states)


def package_run(args, name, cases, raw):
    lane, package = name.split('/', 1)
    try:
        artifact = verify(args.lock, args.work, package)
    except (OSError, ValueError) as error:
        return {key: ('ERROR', 0) for key in cases}, {'artifact_error': str(error).split(':')[0]}
    events = raw / 'events.jsonl'
    events.unlink(missing_ok=True)
    test_root = None
    staging = None
    if args.lock['packages'][package].get('test_files'):
        try:
            # Keep the layout independent of runtime labels: dots in a label
            # can change upstream module/cache naming based on the cwd.
            attempts = args.work / 'runs'
            attempts.mkdir(exist_ok=True)
            staging = tempfile.TemporaryDirectory(prefix=package + '-', dir=attempts)
            test_root = Path(staging.name)
            stage_tests(args.lock, args.work, package, test_root)
        except (OSError, ValueError) as error:
            if staging is not None:
                staging.cleanup()
            return {key: ('ERROR', 0) for key in cases}, {'preparation_error': str(error).split(':')[0]}
    command, env, cwd = package_command(args, package, lane, events, selectors=args.selectors, test_root=test_root)
    env.pop('FT_SELECTION', None)
    if args.case_shard:
        selection = raw / 'selection.json'
        dump(selection, sorted(cases))
        env['FT_SELECTION'] = str(selection)
    code, hung, elapsed = lib.execute(command, env, cwd, package_timeout(args, package), raw / 'output.log')
    records = lib.json_lines(events)
    rows = {}
    invalid = any('id' in r and (not isinstance(r['id'], str) or not isinstance(r.get('status'), str) or r['status'] not in lib.RANK or
                  not isinstance(r.get('seconds'), (int, float)) or not math.isfinite(r['seconds']) or r['seconds'] < 0)
                  for r in records)
    for r in records:
        if not invalid and r.get('id') in cases and r.get('status') in lib.RANK:
            lib.merge_case(rows, r['id'], r['status'], r['seconds'])
    timeout_report = re.search(r'(?m)(?:^\++ Timeout \++$|Timeout \(>[0-9.]+s\) from pytest-timeout|^Timeout \([0-9:.]+\)!)', (raw / 'output.log').read_text(errors='replace'))
    if hung or timeout_report or code is not None and code < 0:
        status = 'TIMEOUT' if hung or timeout_report else 'CRASH'
        # A terminated suite cannot certify even its earlier passes.
        rows = {key: (status, 0) for key in cases}
    elif invalid or not gil_valid(records) or any('invalid_record' in r for r in records) or not any('exitstatus' in r for r in records):
        rows = {key: ('ERROR', 0) for key in cases}
    else:
        for key in cases:
            rows.setdefault(key, ('ERROR' if code not in (0, 1, 5) else 'NOTRUN', 0))
    try:
        intact = verify(args.lock, args.work, package) == artifact
    except (OSError, ValueError):
        intact = False
    if not intact:
        rows = {key: ('ERROR', 0) for key in cases}
    unexpected = sorted({r['id'] for r in records if isinstance(r.get('id'), str)} - set(cases))
    if unexpected:
        rows = {key: ('ERROR', 0) for key in cases}
    parallelism = next((r for r in records if 'parallelism' in r), {})
    parallel = len(parallelism.get('parallelism', {}))
    if lane == 'parallel' and args.runtime != args.oracle_runtime:
        # A case the oracle ran parallel-wrapped must not pass serially.
        oracle = json.loads((args.results / (args.oracle_runtime + '.json')).read_text())['units'][name]
        serial = {case for detail in oracle for case in detail['serial_fallback']}
        rows = {key: ('FAIL', seconds) if status == 'PASS' and key not in serial | set(parallelism.get('parallelism', {})) else (status, seconds)
                for key, (status, seconds) in rows.items()}
    detail = {'artifact': artifact, 'unit_timeout': package_timeout(args, package), 'test_timeout': package_test_timeout(args, package), 'timed_out': bool(hung or timeout_report), 'returncode': code, 'seconds': elapsed, 'gil_valid': gil_valid(records),
              'parallel_collected': parallel,
              'parallel_threads': dict(Counter(parallelism.get('parallelism', {}).values())),
              'serial_fallback': parallelism.get('serial_fallback', {}),
              'artifact_intact': intact,
              'counts': dict(Counter(s for s, _ in rows.values())),
              'unexpected_ids': unexpected}
    if staging is not None:
        staging.cleanup()
    print(name, detail['counts'], flush=True)
    return rows, detail


def run(args, env, raw):
    expected = lib.read_manifest(args.results / 'expected.tsv')
    inventory = json.loads((args.results / 'inventory.json').read_text())
    units = inventory['units']
    declared = {unit.split('/')[0] + '/' + (unit.split('/')[1] + '/' if args.suite == 'ft-packages' else '') + case
                for unit, cases in units.items() for case in cases}
    if declared != set(expected):
        raise ValueError('inventory and expected manifest disagree')
    owners = {}
    for unit, cases in units.items():
        prefix = unit.split('/')[0] + '/' + (unit.split('/')[1] + '/' if args.suite == 'ft-packages' else '')
        for case in cases:
            owners.setdefault(prefix + case, []).append(unit)
    shared = {key: names for key, names in owners.items() if len(names) > 1}
    signature = {'expected_sha256': hashlib.sha256((args.results / 'expected.tsv').read_bytes()).hexdigest(),
                 'seed': args.seed, 'iterations': args.iterations, 'threads': args.threads,
                 'timeout': args.timeout_override, 'resources': args.resources,
                 'perturbation': args.perturbation, 'runtime_args': args.runtime_args,
                 'lock_sha256': hashlib.sha256(args.lock_path.read_bytes()).hexdigest() if args.lock else None}
    rows = {key: ('NOTRUN', 0) for key in expected}
    dest = args.results / (args.runtime + '.tsv')
    meta_path = args.results / (args.runtime + '.json')
    metadata = {'units': {}, 'plan': signature}
    info = identity(args, env, raw)
    if info and args.resume and dest.exists():
        previous_meta = json.loads(meta_path.read_text())
        if previous_meta['plan'] != signature:
            raise ValueError('resume requires identical run plan; choose another runtime label')
        metadata = previous_meta
        previous = lib.read_manifest(dest)
        rows.update({k: previous[k] for k in rows if k in previous})
    metadata['probe'] = info
    executable = shutil.which(args.command[0])
    try:
        metadata['binary_sha256'] = hashlib.sha256(Path(executable).read_bytes()).hexdigest() if executable else None
    except OSError:
        metadata['binary_sha256'] = None
    metadata['platform'] = {'os': sys.platform, 'machine': platform.machine(), 'logical_cpus': os.cpu_count(),
                            'affinity_cpus': len(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else None}
    metadata['environment'] = {'PYTHON_JIT': '0', 'LC_ALL': 'C.UTF-8', 'TZ': 'UTC',
                               'PYTHONHASHSEED': str(args.seed % 4294967296) if args.lock else '0',
                               'force_gil_off': any(value == '-Xgil=0' or (value == 'gil=0' and index > 0 and args.command[index - 1] == '-X')
                                                    for index, value in enumerate(args.command))}
    if info:
        selected = sorted(units)
        if args.lane:
            selected = [n for n in selected if n.startswith(args.lane + '/')]
        if args.select:
            selected = [n for n in selected if any(s in n for s in args.select)]
        if args.shard:
            i, n = map(int, args.shard.split('/'))
            if not 0 <= i < n:
                raise ValueError('shard must be zero-based INDEX/TOTAL')
            selected = selected[i::n]
        if not selected:
            raise ValueError('selection matched no units')
        deadline = time.monotonic() + args.budget
        for name in selected:
            unit_timeout = package_timeout(args, name.split('/')[1]) if args.lock else args.timeout
            if time.monotonic() + unit_timeout + 35 > deadline:
                print('budget reached; remaining units retain their recorded status', flush=True)
                break
            raw_unit = raw / name / f'attempt-{time.time_ns()}'
            raw_unit.mkdir(parents=True, exist_ok=True)
            cases = set(units[name])
            if args.suite == 'ft-packages' and args.selectors:
                cases = {k for k in cases if any(k == s or k.startswith(s + '::') or k.startswith(s.rstrip('/') + '/') for s in args.selectors)}
                if not cases:
                    raise ValueError('selectors match no expected cases')
            if args.case_shard:
                index, total = map(int, args.case_shard.split('/'))
                if not 0 <= index < total:
                    raise ValueError('case shard must be zero-based INDEX/TOTAL')
                cases = set(sorted(cases)[index::total])
                if not cases:
                    continue
            outcomes = {}
            details = []
            unit_base = raw_unit
            for iteration in range(args.iterations):
                raw_unit = unit_base / f'iteration-{iteration:04d}'
                raw_unit.mkdir(exist_ok=True)
                if time.monotonic() + unit_timeout + 35 > deadline:
                    for key in cases:
                        lib.merge_case(outcomes, key, 'NOTRUN', 0)
                    break
                env_iteration = dict(env, FT_SEED=str(args.seed + iteration),
                                     FT_THREADS=str((args.threads or 4) if name.startswith('parallel/') else 1),
                                     FT_OBSERVATIONS=str(raw_unit / 'observations.jsonl'))
                observations = raw_unit / 'observations.jsonl'
                observations.unlink(missing_ok=True)
                if args.suite == 'ft-cpython':
                    profile = json.loads((ROOT / 'results/libtest/resource-profile.json').read_text())
                    available = set(profile['free-threaded'].get(name.split('/', 1)[1], {}).get('cases', []))
                    local_rows, detail = lib.run_module(name.split('/', 1)[1], cases, args, args.command,
                                                       env_iteration, raw_unit, cases - available)
                    records = lib.json_lines(observations)
                    if not gil_valid(records):
                        # Whole-module upstream skips have no case execution.
                        if detail['state'] not in ('SKIPPED', 'RESOURCE_DENIED') and not any(s in ('CRASH', 'TIMEOUT') for s, _ in local_rows.values()):
                            local_rows = {key: ('ERROR', 0) for key in cases}
                    detail['gil_valid'] = gil_valid(records)
                    detail['parallel_wrapped'] = len([r for r in records if 'threads' in r])
                    detail['serial_cases'] = sorted({r['id'] for r in records if r.get('serial')})
                else:
                    original_seed = args.seed
                    args.seed += iteration
                    local_rows, detail = package_run(args, name, cases, raw_unit)
                    args.seed = original_seed
                for key, (status, seconds) in local_rows.items():
                    lib.merge_case(outcomes, key, status, seconds)
                requested_threads = 1
                inner_iterations = 1
                if name.startswith('parallel/'):
                    requested_threads = args.threads or (4 if args.suite == 'ft-cpython' else args.lock['packages'][name.split('/')[1]]['threads'])
                    if args.suite == 'ft-packages':
                        inner_iterations = args.lock['packages'][name.split('/')[1]].get('parallel_iterations', 1)
                detail.update(requested_threads=requested_threads, inner_iterations=inner_iterations, iteration=iteration, seed=args.seed + iteration, selectors=args.selectors, case_shard=args.case_shard,
                              counts=dict(Counter(s for s, _ in local_rows.values())))
                details.append(detail)
            lane = name.split('/', 1)[0]
            # Package IDs include package ownership; CPython IDs already do.
            prefix = lane + '/' + (name.split('/', 1)[1] + '/' if args.suite == 'ft-packages' else '')
            for key, value in outcomes.items():
                full_id = prefix + key
                if full_id in shared:
                    observations = metadata.setdefault('shared_outcomes', {})
                    observations.setdefault(name, {})[full_id] = value
                    candidates = [observations.get(owner, {}).get(full_id, ('NOTRUN', 0)) for owner in shared[full_id]]
                    rows[full_id] = max(candidates, key=lambda value: lib.RANK[value[0]])
                else:
                    rows[full_id] = value
            metadata['units'].setdefault(name, []).extend(details)
            lib.write_manifest(dest, rows)
            dump(meta_path, metadata)
    lib.write_manifest(dest, rows)
    dump(meta_path, metadata)
    print(args.suite, args.runtime, dict(Counter(s for s, _ in rows.values())), flush=True)
    return int(any(s in BAD for s, _ in rows.values()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('suite', choices=['ft-cpython', 'ft-packages'])
    parser.add_argument('action', choices=['collect', 'run'])
    parser.add_argument('--python', required=True)
    parser.add_argument('--runtime', default='lucifer')
    parser.add_argument('--oracle-runtime', default='cpython-3.14.10t', help='recorded oracle run the parallel lane is compared with')
    parser.add_argument('--source', type=Path, help='official pinned CPython checkout (read only)')
    parser.add_argument('--work', type=Path, help='external package workspace from prepare.py')
    parser.add_argument('--results', type=Path)
    parser.add_argument('--raw-dir', type=Path)
    parser.add_argument('--lock', dest='lock_path', type=Path, default=HERE / 'packages.lock.json')
    parser.add_argument('--timeout', type=float, help='override controller deadline; otherwise use pinned package/default budget')
    parser.add_argument('--startup-timeout', type=float, default=10)
    parser.add_argument('--budget', type=float, default=1000)
    parser.add_argument('--threads', type=int, help='override upstream threaded counts for stress')
    parser.add_argument('--iterations', type=int, default=1, help='fresh process repetitions; worst result wins')
    parser.add_argument('--seed', type=int, default=0)
    parser.add_argument('--perturbation', default='none', help='recorded runtime perturbation mode')
    parser.add_argument('--runtime-arg', action='append', dest='runtime_args', default=[],
                        help='actual supported interpreter option, appended before test entry point')
    parser.add_argument('--resources', default=lib.RESOURCES)
    parser.add_argument('--lane', choices=['serial', 'parallel'])
    parser.add_argument('--select', nargs='+', help='unit substrings (package or module names)')
    parser.add_argument('--selectors', nargs='+', help='pytest node/file selectors for a diagnostic rerun')
    parser.add_argument('--shard', help='zero-based INDEX/TOTAL over selected units')
    parser.add_argument('--case-shard', help='package cases: zero-based INDEX/TOTAL, stable sorted ID stripes')
    parser.add_argument('--resume', action='store_true')
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    args.timeout_override = args.timeout
    args.timeout = args.timeout if args.timeout is not None else 180
    if args.timeout <= 0 or args.iterations < 1 or (args.threads is not None and args.threads < 1) or args.seed < 0 or not re.fullmatch(r'[\w.-]+', args.runtime):
        parser.error('invalid timeout, iterations, threads, seed, or runtime')
    if args.case_shard and args.suite != 'ft-packages':
        parser.error('--case-shard applies to package tests')
    args.command = shlex.split(args.python)
    if not args.command:
        parser.error('empty --python')
    if '/' in args.command[0]:
        args.command[0] = str(Path(args.command[0]).absolute())
    args.command += args.runtime_args
    args.results = (args.results or ROOT / 'results' / args.suite).resolve()
    args.results.mkdir(parents=True, exist_ok=True)
    raw = (args.raw_dir or args.results / 'raw' / args.runtime).resolve()
    raw.mkdir(parents=True, exist_ok=True)
    args.lock = json.loads(args.lock_path.read_text()) if args.suite == 'ft-packages' else None
    if args.work:
        args.work = args.work.resolve()
    env = lib.environment('installed')
    if args.work:
        env['PYTHONPATH'] = str(args.work / 'venv/lib/python3.14t/site-packages')
    if args.action == 'collect':
        info = identity(args, env, raw)
        if not info or info['version'] != [3, 14, 10] or info['implementation'] != 'cpython':
            raise ValueError('collection requires the pinned free-threaded oracle')
        inventory = cpython_inventory(args, env, raw) if args.suite == 'ft-cpython' else package_inventory(args, env, raw)
        rows = {}
        for unit, cases in inventory['units'].items():
            prefix = unit.split('/')[0] + '/' + (unit.split('/')[1] + '/' if args.suite == 'ft-packages' else '')
            rows.update({prefix + case: ('NOTRUN', 0) for case in cases})
        if not rows:
            raise ValueError('empty corpus')
        dump(args.results / 'inventory.json', inventory)
        lib.write_manifest(args.results / 'expected.tsv', rows)
        print(len(rows), 'expected rows')
        return 0
    return run(args, env, raw)


if __name__ == '__main__':
    sys.exit(main())
