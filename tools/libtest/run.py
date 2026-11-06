#!/usr/bin/env python3
"""Host-side CPython regression collection and execution (stdlib only)."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
from collections import Counter
from contextlib import nullcontext
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import signal
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
BRIDGE = Path(__file__).with_name('bridge.py')
RESULTS = ROOT / 'results' / 'libtest'
RESOURCES = 'all,-network,-urlfetch,tzdata,xpickle'
STATUSES = 'PASS FAIL ERROR SKIP XFAIL XPASS TIMEOUT CRASH NOTRUN'.split()
RANK = {s: i for i, s in enumerate(['PASS', 'SKIP', 'XFAIL', 'NOTRUN', 'XPASS', 'FAIL', 'ERROR', 'CRASH', 'TIMEOUT'])}
PROBE = "import json,sys,sysconfig,test; print(json.dumps({'version':list(sys.version_info[:3]),'implementation':sys.implementation.name,'free_threaded':bool(sysconfig.get_config_var('Py_GIL_DISABLED')),'gil_enabled':getattr(sys,'_is_gil_enabled',lambda:True)(),'dont_write_bytecode':sys.dont_write_bytecode,'test':test.__file__}))"


def write_manifest(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    with temporary.open('w', encoding='utf-8', newline='') as stream:
        writer = csv.writer(stream, delimiter='\t', lineterminator='\n')
        writer.writerow(['id', 'status', 'seconds'])
        for key, (status, seconds) in sorted(rows.items()):
            writer.writerow([key, status, f'{max(0, seconds):.6f}'])
    temporary.replace(path)


def read_manifest(path):
    with path.open(encoding='utf-8', newline='') as stream:
        rows = list(csv.DictReader(stream, delimiter='\t'))
    result = {row['id']: (row['status'], float(row['seconds'])) for row in rows}
    if len(result) != len(rows) or not result:
        raise ValueError('manifest must contain unique, nonempty test IDs')
    return result


def environment(source):
    env = dict(os.environ)
    for name in list(env):
        if name.startswith('PYTHON') or name in ('LIBTEST_EVENTS', 'LIBTEST_STATE', 'LIBTEST_PID'):
            env.pop(name)
    env.update(PYTHONHASHSEED='0', PYTHON_JIT='0',
               LC_ALL='C.UTF-8', TZ='UTC', TERM='dumb', NO_COLOR='1')
    if source == 'vendored':
        env['PYTHONPATH'] = str(ROOT / 'third_party' / 'cpython' / 'Lib')
    return env


def execute(command, env, cwd, timeout, log, stderr_log=None, worker_pid=None):
    started = time.monotonic()
    with log.open('wb') as output, (stderr_log.open('wb') if stderr_log else nullcontext(output)) as error_output:
        try:
            proc = subprocess.Popen(command, cwd=cwd, env=env, stdout=output,
                                    stderr=error_output, start_new_session=True)
        except OSError as error:
            output.write(str(error).encode())
            return None, False, time.monotonic() - started
        try:
            code = proc.wait(timeout=timeout)
            timed_out = False
        except subprocess.TimeoutExpired:
            timed_out = True
            # regrtest workers create separate process groups. Reap that
            # owned group too if the controller itself stopped responding.
            if worker_pid and worker_pid.exists():
                try:
                    os.killpg(int(worker_pid.read_text()), signal.SIGKILL)
                except (ProcessLookupError, ValueError):
                    pass
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            code = proc.wait()
    return code, timed_out, time.monotonic() - started


def json_lines(path):
    if not path.exists():
        return []
    records = []
    for line in path.read_text(errors='replace').splitlines():
        if line:
            try:
                record = json.loads(line)
                records.append(record if isinstance(record, dict) else {'invalid_record': True})
            except ValueError:
                # A killed worker can leave a final partial write. Never lose
                # its CRASH/TIMEOUT disposition while reading observations.
                records.append({'invalid_record': True})
    return records


def clean(text, python, scratch):
    text = str(text).replace(str(ROOT), '{repo}').replace(str(scratch), '{scratch}')
    for index, part in enumerate(python):
        if '/' in part:
            text = text.replace(part, '{python' + str(index) + '}')
    # Diagnostics may contain installation/source paths, account names or host
    # details. Keep raw logs local; publish only IDs, states and portable argv.
    return text


def public_reason(text):
    return re.sub(r'(?<!\w)/[^\s\"\'<>;,]+', '<path>', str(text))


def probe(python, env, raw, timeout):
    log = raw / 'probe.log'
    code, hung, _ = execute([*python, '-c', PROBE], env, raw, timeout, log)
    if code != 0 or hung:
        return None
    try:
        info = json.loads(log.read_text().splitlines()[-1])
        if (isinstance(info, dict) and isinstance(info.get('implementation'), str)
                and isinstance(info.get('version'), list) and len(info['version']) == 3
                and isinstance(info.get('free_threaded'), bool)):
            return info
        return None
    except (ValueError, IndexError):
        return None


def collect(args, python, env, raw):
    info = probe(python, env, raw, args.startup_timeout)
    if not info or info['version'] != [3, 14, 10] or info['implementation'] != 'cpython':
        raise ValueError('collection requires the pinned CPython 3.14.10 oracle')
    code, hung, _ = execute([*python, '-m', 'test', '--list-tests'], env, raw,
                            args.timeout, raw / 'modules.log')
    if code != 0 or hung:
        raise ValueError('regrtest --list-tests failed; see raw log')
    modules = [line.strip() for line in (raw / 'modules.log').read_text().splitlines()
               if re.fullmatch(r'(?:test\.)?test_\w+(?:\.\w+)*', line.strip())]
    if not modules:
        raise ValueError('no modules collected')
    inventory = {}
    for module in modules:
        log = raw / (module + '.collection.log')
        entry = [str(BRIDGE)] if args.profile else ['-m', 'test']
        command = [*python, *entry, '-u', args.resources, '--list-cases', module]
        code, hung, _ = execute(command, env, raw, args.timeout, log,
                                raw / (module + '.collection.stderr'))
        text = log.read_text(errors='replace')
        # stdout is the upstream ID stream. IDs include parameterized class
        # names, spaces, doctest names without dots, and ModuleSkipped cases.
        cases = sorted(set(line for line in text.splitlines() if line))
        inventory[module] = {'cases': cases, 'returncode': code, 'timeout': hung,
                             'status': 'ERROR' if code != 0 or hung else ('COLLECTED' if cases else 'EMPTY_OR_SKIPPED')}
        print(f'collect {module}: {len(cases)} {inventory[module]["status"]}', flush=True)
    label = 'free-threaded' if info['free_threaded'] else 'gil'
    path = args.results / ('resource-profile.json' if args.profile else 'inventory.json')
    database = json.loads(path.read_text()) if path.exists() else {}
    database[label] = inventory
    if args.profile:
        database['resources'] = args.resources
    path.write_text(json.dumps(database, indent=2, sort_keys=True) + '\n')
    if args.profile:
        print(f'{label} resource-aware inventory added')
        return
    ids = {case for build in database.values() for entry in build.values() for case in entry['cases']}
    if not ids:
        raise ValueError('collection produced no cases')
    write_manifest(args.results / 'expected.tsv', {key: ('NOTRUN', 0) for key in ids})
    print(f'{len(ids)} expected cases ({label} inventory added)')


def merge_case(rows, key, status, seconds):
    if key not in rows or RANK[status] > RANK[rows[key][0]]:
        rows[key] = (status, seconds)
    elif status == rows[key][0]:
        rows[key] = (status, max(rows[key][1], seconds))


def xml_rows(path):
    rows = {}
    if not path.exists():
        return rows
    for case in ET.parse(path).iter('testcase'):
        key = case.get('name')
        if not key:
            continue  # Upstream omits subtest/fixture names; sidecar fills them.
        status = 'PASS'
        for tag, value in [('failure', 'FAIL'), ('error', 'ERROR'), ('skipped', 'SKIP'),
                           ('output', 'XFAIL'), ('outcome', 'XPASS')]:
            if case.find(tag) is not None:
                status = value
                break
        seconds = float(case.get('time', '0'))
        if not math.isfinite(seconds):
            raise ValueError('nonfinite JUnit duration')
        merge_case(rows, key, status, seconds)
    return rows


def run_module(module, ids, args, python, env, raw, unavailable, excluded=None):
    directory = raw / module
    directory.mkdir(parents=True, exist_ok=True)
    events, state, xml, worker_pid = (directory / name for name in ('events.jsonl', 'state.jsonl', 'junit.xml', 'worker.pid'))
    for path in (events, state, xml, worker_pid):
        path.unlink(missing_ok=True)
    worker = shlex.join([*python, str(BRIDGE), '--worker'])
    # Cases that kill this runtime's worker are left out: regrtest ignores
    # them by full id, and they stay NOTRUN with the reason recorded.
    excluded = {key: reason for key, reason in (excluded or {}).items() if key in ids}
    ignore = []
    if excluded:
        (directory / 'excluded.txt').write_text(''.join(key + '\n' for key in sorted(excluded)))
        ignore = ['--ignorefile', str(directory / 'excluded.txt')]
    command = [*python, str(BRIDGE), '-j', '1', '--python', worker,
               '--timeout', str(args.timeout), '--randseed', '0', '-u', args.resources,
               *ignore, '--junit-xml', str(xml), '--tempdir', str(directory), module]
    local_env = dict(env, LIBTEST_EVENTS=str(events), LIBTEST_STATE=str(state), LIBTEST_PID=str(worker_pid))
    code, hung, elapsed = execute(command, local_env, directory, args.timeout + 30,
                                  directory / 'output.log', worker_pid=worker_pid)
    states = json_lines(state)
    disposition = states[-1].get('state', 'MISSING') if states else 'MISSING'
    # Upstream normally implements its timeout with faulthandler exit=True;
    # the parent reports that exit as WORKER_FAILED rather than TIMEOUT.
    faulthandler_timeout = (disposition == 'WORKER_FAILED' and elapsed >= args.timeout * 0.9
                            and re.search(r'(?m)^Timeout \([0-9:.]+\)!',
                                          (directory / 'output.log').read_text(errors='replace')))
    if hung or disposition == 'TIMEOUT' or faulthandler_timeout:
        fallback = 'TIMEOUT'
    elif code is not None and code < 0 or disposition == 'WORKER_FAILED':
        fallback = 'CRASH'
    elif disposition in ('SKIPPED', 'RESOURCE_DENIED'):
        fallback = 'SKIP'
    elif disposition in ('FAILED', 'UNCAUGHT_EXC', 'WORKER_BUG'):
        fallback = 'ERROR'
    else:
        fallback = 'NOTRUN'
    observation_error = any(record.get('invalid_record') for record in states)
    try:
        observed = xml_rows(xml)
    except (ET.ParseError, ValueError):
        observed = {}
        observation_error = True
    records = json_lines(events)
    for event in records:
        if ('invalid_record' in event or not isinstance(event.get('id'), str)
                or not isinstance(event.get('status'), str) or event['status'] not in RANK
                or not isinstance(event.get('seconds'), (int, float))
                or not math.isfinite(event['seconds'])):
            observation_error = True
            continue
        if event['id'] in ids:
            merge_case(observed, event['id'], event['status'], event['seconds'])
        else:
            # unittest emits setUpClass (module.Class), setUpModule (module),
            # etc. without running individual cases.
            match = re.fullmatch(r'(?:setUp|tearDown)(?:Class|Module) \((.+)\)', event['id'])
            if match:
                for key in ids:
                    if key.startswith(match[1] + '.'):
                        merge_case(observed, key, event['status'], 0)
    rows = {}
    reasons = {}
    all_reasons = {}
    for event in records:
        if event.get('status') != 'SKIP' or not isinstance(event.get('id'), str):
            continue
        key = event['id']
        match = re.fullmatch(r'(?:setUp|tearDown)(?:Class|Module) \((.+)\)', key)
        targets = [i for i in ids if i.startswith(match[1] + '.')] if match else [key]
        for ident in targets:
            if ident in ids:
                reason = public_reason(event.get('reason', ''))
                reasons[ident] = reason
                all_reasons.setdefault(ident, set()).add(reason)
    if observation_error and fallback not in ('TIMEOUT', 'CRASH'):
        fallback = 'ERROR'
    for key in ids:
        if key in excluded:
            rows[key] = ('NOTRUN', 0)
        elif fallback in ('TIMEOUT', 'CRASH'):
            rows[key] = (fallback, 0)
        elif observation_error:
            rows[key] = ('ERROR', 0)
        elif key in observed:
            rows[key] = observed[key]
        elif key in unavailable:
            rows[key] = ('SKIP', 0)
        else:
            rows[key] = (fallback, 0)
    module_skip = None
    if disposition in ('SKIPPED', 'RESOURCE_DENIED'):
        match = re.search(r' skipped -- (.+)', (directory / 'output.log').read_text(errors='replace'))
        module_skip = public_reason(match[1]) if match else 'upstream module skip'
    detail = {'state': disposition, 'returncode': code, 'seconds': elapsed,
              'command': [clean(part, python, raw) for part in command],
              'counts': dict(Counter(status for status, _ in rows.values())),
              'unexpected_ids': sorted(set(observed) - set(ids)),
              'observation_error': observation_error,
              'skip_reasons': {key: reasons.get(key, module_skip or unavailable.get(key, 'module skipped'))
                               for key, (status, _) in sorted(rows.items()) if status == 'SKIP'},
              'additional_skip_reasons': {key: sorted(values - {reasons[key]}) for key, values in sorted(all_reasons.items())
                                          if rows[key][0] == 'SKIP' and len(values) > 1},
              'environment': {key: env.get(key) for key in ('PYTHONHASHSEED', 'PYTHON_JIT', 'PYTHONDONTWRITEBYTECODE', 'LC_ALL', 'TZ')}}
    if module_skip:
        detail['skip_reason'] = module_skip
    if excluded:
        detail['excluded'] = dict(sorted(excluded.items()))
    print(f'{module}: {detail["counts"]} ({elapsed:.1f}s)', flush=True)
    return rows, detail


def run(args, python, env, raw):
    expected = read_manifest(args.results / 'expected.tsv')
    inventory = json.loads((args.results / 'inventory.json').read_text())
    modules = {}
    for build in inventory.values():
        for module, entry in build.items():
            modules.setdefault(module, set()).update(entry['cases'])
    if set().union(*modules.values()) != set(expected):
        raise ValueError('expected.tsv and inventory.json disagree')
    owners = {}
    for module, cases in modules.items():
        for key in cases:
            owners.setdefault(key, []).append(module)
    shared = {key: names for key, names in owners.items() if len(names) > 1}
    selected = sorted(modules)
    if args.modules:
        selected = [m for m in selected if any(m == p or m.startswith(p + '.') for p in args.modules)]
        if not selected:
            raise ValueError('module selection matched nothing')
    if args.shard:
        index, total = map(int, args.shard.split('/'))
        if not 0 <= index < total:
            raise ValueError('shard is zero-based INDEX/TOTAL')
        selected = [m for i, m in enumerate(selected) if i % total == index]
    destination = args.results / (args.runtime + '.tsv')
    rows = {key: ('NOTRUN', 0) for key in expected}
    metadata_path = args.results / (args.runtime + '.json')
    metadata = {'modules': {}}
    if args.resume and destination.exists():
        previous = read_manifest(destination)
        rows.update({key: previous[key] for key in rows if key in previous})
        if metadata_path.exists():
            metadata = json.loads(metadata_path.read_text())
    info = probe(python, env, raw, args.startup_timeout)
    metadata.update(resources=args.resources, timeout=args.timeout, jobs=args.jobs,
                    environment={key: env.get(key) for key in ('PYTHONHASHSEED', 'PYTHONDONTWRITEBYTECODE', 'PYTHON_JIT', 'LC_ALL', 'TZ')},
                    source=args.source, expected_sha256=hashlib.sha256((args.results / 'expected.tsv').read_bytes()).hexdigest(),
                    probe={key: value for key, value in (info or {}).items() if key != 'test'})
    if not info:
        # Startup failure invalidates even resumed results: every expected row
        # is visibly NOTRUN, including when only one shard was requested.
        rows = {key: ('NOTRUN', 0) for key in expected}
        metadata['modules'] = {}
        metadata.pop('shared_cases', None)
        metadata['startup'] = 'NOTRUN'
    else:
        metadata['startup'] = 'PASS'
        unavailable = {}
        profile_path = args.results / 'resource-profile.json'
        if profile_path.exists():
            profile = json.loads(profile_path.read_text())
            if profile.get('resources') == args.resources and all(build in profile for build in inventory):
                for module, ids in modules.items():
                    entries = [profile[build].get(module, {}) for build in inventory]
                    if all(entry.get('status') in ('COLLECTED', 'EMPTY_OR_SKIPPED') for entry in entries):
                        generated = {key for entry in entries for key in entry['cases']}
                        unavailable.update(dict.fromkeys(ids - generated, 'resource-profile omission (conditional generation)'))
        metadata['resource_omitted_cases'] = len(unavailable)
        if args.source == 'installed' and info['implementation'] == 'cpython' and info['version'] == [3, 14, 10]:
            build = inventory.get('free-threaded' if info['free_threaded'] else 'gil', {})
            available = {case for entry in build.values() for case in entry['cases']}
            unavailable.update(dict.fromkeys(set(expected) - available, 'not collected by this ABI build (union inventory)'))
        # <runtime>.excluded.tsv beside the results: id and reason (a ticket).
        excluded = {}
        excluded_path = args.results / (args.runtime + '.excluded.tsv')
        if excluded_path.exists():
            with excluded_path.open(encoding='utf-8', newline='') as stream:
                excluded = {row['id']: row['reason'] for row in csv.DictReader(stream, delimiter='\t')}
        metadata['excluded_cases'] = len(excluded)
        with ThreadPoolExecutor(max_workers=args.jobs) as executor:
            futures = {executor.submit(run_module, module, modules[module], args, python,
                                       env, raw, unavailable, excluded): module for module in selected}
            for future in as_completed(futures):
                module = futures[future]
                result, detail = future.result()
                rows.update({key: value for key, value in result.items() if key not in shared})
                repeated = metadata.setdefault('shared_cases', {})
                repeated[module] = {key: value for key, value in result.items() if key in shared}
                for key, names in shared.items():
                    outcomes = [repeated.get(name, {}).get(key, ('NOTRUN', 0)) for name in names]
                    rows[key] = max(outcomes, key=lambda outcome: RANK[outcome[0]])
                metadata['modules'][module] = detail
                write_manifest(destination, rows)
    write_manifest(destination, rows)
    metadata_path.write_text(json.dumps(metadata, indent=2, sort_keys=True) + '\n')
    print(args.runtime, dict(Counter(status for status, _ in rows.values())))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['collect', 'run'])
    parser.add_argument('--python', required=True, help='shell-quoted command, executed without a shell')
    parser.add_argument('--runtime', default='lucifer')
    parser.add_argument('--source', choices=['installed', 'vendored'], default='vendored')
    parser.add_argument('--results', type=Path, default=RESULTS)
    parser.add_argument('--raw-dir', type=Path)
    parser.add_argument('--timeout', type=float, default=180)
    parser.add_argument('--startup-timeout', type=float, default=10)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--resources', default=RESOURCES)
    parser.add_argument('--modules', nargs='+')
    parser.add_argument('--shard', help='zero-based INDEX/TOTAL, stable sorted module stripes')
    parser.add_argument('--resume', action='store_true', help='preserve other shards; selected modules always rerun')
    parser.add_argument('--profile', action='store_true', help='collect with resource-aware conditional generation; keep expected unchanged')
    args = parser.parse_args()
    if args.jobs < 1 or args.timeout <= 0 or args.startup_timeout <= 0 or not re.fullmatch(r'[\w.-]+', args.runtime):
        parser.error('invalid jobs, timeout, or runtime')
    args.results = args.results.resolve()
    args.results.mkdir(parents=True, exist_ok=True)
    raw = (args.raw_dir or args.results / 'raw' / args.runtime).resolve()
    if args.profile:
        raw = raw / 'profile'
    raw.mkdir(parents=True, exist_ok=True)
    python = shlex.split(args.python)
    if not python:
        parser.error('--python must be a nonempty command')
    # Relative interpreter paths are resolved before moving into private dirs.
    if '/' in python[0]:
        # Following a venv's executable symlink changes its sys.prefix and
        # silently drops that environment's test dependencies.
        python[0] = str(Path(python[0]).absolute())
    env = environment(args.source)
    if args.action == 'collect':
        collect(args, python, env, raw)
    else:
        run(args, python, env, raw)


if __name__ == '__main__':
    main()
