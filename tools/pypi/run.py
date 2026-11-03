#!/usr/bin/env python3
"""Hash-pinned production-package tests; the driver uses host Python and stdlib."""
import argparse
from collections import Counter
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tarfile
import urllib.request

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
STATUSES = 'PASS FAIL ERROR SKIP XFAIL XPASS TIMEOUT CRASH NOTRUN'.split()


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def target_command(value):
    command = shlex.split(value)
    if not command:
        raise ValueError('empty interpreter command')
    if '/' in command[0]:
        # Preserve venv symlinks: dereferencing one would select the base site.
        command[0] = str(Path(command[0]).absolute())
    return command


def manifest(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix('.tmp')
    with temp.open('w', encoding='utf-8', newline='') as stream:
        stream.write('id\tstatus\tseconds\n')
        for name, (status, seconds) in sorted(rows.items()):
            if not name or any(c in name for c in '\t\r\n') or status not in STATUSES or not math.isfinite(seconds) or seconds < 0:
                raise ValueError('invalid manifest row')
            stream.write(f'{name}\t{status}\t{seconds:.6f}\n')
    temp.replace(path)


def inventory(path):
    with path.open(encoding='utf-8', newline='') as stream:
        reader = csv.DictReader(stream, delimiter='\t', quoting=csv.QUOTE_NONE)
        if reader.fieldnames != ['id', 'status', 'seconds']:
            raise ValueError('invalid manifest header')
        rows = list(reader)
    ids = [row['id'] for row in rows]
    if not ids or len(ids) != len(set(ids)):
        raise ValueError('empty or duplicate expected inventory')
    return ids


FATAL_SIGNALS = {int(s) for s in (signal.SIGSEGV, signal.SIGABRT, signal.SIGBUS, signal.SIGILL, signal.SIGFPE, signal.SIGKILL)}


def execute(command, cwd, env, log, timeout):
    """Run a command; a shell wrapper reports a fatal signal as exit 128+N, returned as -N."""
    with log.open('wb') as stream:
        try:
            proc = subprocess.Popen(command, cwd=cwd, env=env, stdout=stream,
                                    stderr=subprocess.STDOUT, start_new_session=True)
        except OSError:
            return None
        try:
            code = proc.wait(timeout=timeout)
            return 128 - code if code - 128 in FATAL_SIGNALS else code
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.wait()
            return 'TIMEOUT'
        except BaseException:
            if proc.poll() is None:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
            raise
        finally:
            # Test subprocesses belong to this invocation even if their parent exits.
            if proc.poll() is not None:
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass


def artifact(entry, cache):
    cache.mkdir(parents=True, exist_ok=True)
    path = cache / entry['filename']
    if not path.exists():
        temp = path.with_suffix(path.suffix + '.download')
        with urllib.request.urlopen(entry['url'], timeout=60) as response, temp.open('wb') as output:
            shutil.copyfileobj(response, output)
        if sha(temp) != entry['sha256']:
            temp.unlink()
            raise ValueError('download hash mismatch: ' + path.name)
        temp.replace(path)
    if sha(path) != entry['sha256']:
        raise ValueError('artifact hash mismatch: ' + path.name)
    return path


def setup(args, pin):
    for entry in pin['artifacts']:
        artifact(entry, args.cache)
    for pkg in pin['packages'].values():
        artifact(pkg['source'], args.cache)
        for entry in pkg.get('test_sources', {}).values():
            artifact(entry, args.cache)
    command = target_command(args.python)
    subprocess.run(command + ['-m', 'venv', str(args.env)], check=True)
    requirements = args.lock or HERE / 'requirements.lock'
    omit = pin['packages'].get(args.package, {}).get('environment_omit', [])
    if omit:
        if sha(requirements) != sha(HERE / 'requirements.lock'):
            raise ValueError('package-specific omissions require the current lock')
        groups = {}
        for entry in pin['artifacts']:
            if entry['name'] not in omit:
                groups.setdefault((entry['name'], entry['version']), set()).add(entry['sha256'])
        args.work.mkdir(parents=True, exist_ok=True)
        requirements = args.work / (args.package + '.requirements.lock')
        requirements.write_text(''.join(name + '==' + version + ''.join(
            ' --hash=sha256:' + digest for digest in sorted(hashes)) + '\n'
            for (name, version), hashes in sorted(groups.items())))
    subprocess.run([str(args.env / 'bin/python'), '-m', 'pip', 'install',
                    '--no-index', '--find-links', str(args.cache), '--only-binary=:all:',
                    '--no-deps', '--require-hashes', '-r', str(requirements)], check=True)


def events(path):
    result = []
    if path.exists():
        for line in path.read_text().splitlines():
            try:
                result.append(json.loads(line))
            except ValueError:
                # A killed writer may leave only its final line incomplete.
                if line != path.read_text().splitlines()[-1]:
                    raise
    return result


def run(args, pin):
    requirements = (args.lock or HERE / 'requirements.lock').read_bytes()
    lock_hash = hashlib.sha256(requirements).hexdigest()
    snapshots = args.results / 'locks'
    snapshots.mkdir(parents=True, exist_ok=True)
    (snapshots / (lock_hash + '.lock')).write_bytes(requirements)
    fingerprints = {'lock_sha256': lock_hash,
                    'corpus_sha256': sha(HERE / 'corpus.json'),
                    'harness_sha256': {name: sha(HERE / name) for name in
                                       ('run.py', 'recorder.py', 'unittest_worker.py')}}
    pkg = pin['packages'][args.package]
    target = args.results / args.package
    expected = target / 'expected.tsv'
    rows = {name: ('NOTRUN', 0.0) for name in inventory(expected)} if expected.exists() else {}
    if args.action == 'run' and rows:
        # A run refused below must not leave an earlier run's outcomes in place.
        manifest(target / (args.runtime + '.tsv'), rows)
    work =args.work / args.package / args.runtime
    work.mkdir(parents=True, exist_ok=True)
    # Saved examples from an earlier run would change which examples this run draws.
    shutil.rmtree(work / 'hypothesis', ignore_errors=True)
    env = os.environ.copy()
    for key in ['PYTHONHOME', 'PYTHONPATH', 'PYTHON_GIL', 'PYTEST_ADDOPTS', 'PYTEST_PLUGINS']:
        env.pop(key, None)
    env.update(PYTHONHASHSEED='0', TZ='UTC', LC_ALL='C.UTF-8', PYTHON_JIT='0',
               PYTEST_DISABLE_PLUGIN_AUTOLOAD='1', OPENBLAS_NUM_THREADS='1', OMP_NUM_THREADS='1',
               COLUMNS='80', HYPOTHESIS_STORAGE_DIRECTORY=str(work / 'hypothesis'),
               MPLCONFIGDIR=str(work / 'matplotlib'))
    for name in pkg.get('unset_env', []):
        env.pop(name, None)
    command = target_command(args.python)
    if Path(command[0]).is_absolute():
        env['PATH'] = str(Path(command[0]).parent) + os.pathsep + env.get('PATH', '')
    probe = execute(command + ['-c', 'import json,sys,site; print(json.dumps({"ok":True,"version":sys.version.split()[0],"sites":site.getsitepackages()}))'],
                    work, env, work / 'probe.log', min(args.timeout, 20))
    try:
        info = json.loads((work / 'probe.log').read_text())
        ready = probe == 0 and info.get('ok') is True
    except (ValueError, AttributeError):
        ready = False
    reason = 'interpreter failed startup'
    data = []
    code = None
    if ready:
        archive = artifact(pkg['source'], args.cache)
        source_parent = work / 'source'
        # Fresh source on every invocation; test side effects never contaminate a rerun.
        if source_parent.exists():
            shutil.rmtree(source_parent)
        source_parent.mkdir()
        with tarfile.open(archive) as source:
            source.extractall(source_parent, filter='data')
        source = next(source_parent.iterdir())
        overrides = []
        for name, entry in pkg.get('test_sources', {}).items():
            dependency = work / 'test-sources' / name
            if dependency.exists():
                shutil.rmtree(dependency)
            dependency.mkdir(parents=True)
            with tarfile.open(artifact(entry, args.cache)) as stream:
                stream.extractall(dependency, filter='data')
            overrides.append(str(next(dependency.iterdir()) / entry['pythonpath']))
        journal = work / 'events.jsonl'
        journal.unlink(missing_ok=True)
        env.update(CORPUS_JOURNAL=str(journal), CORPUS_SOURCE=str(source),
                   CORPUS_PACKAGE=args.package,
                   CORPUS_SITE=str(args.site) if args.site else '',
                   CORPUS_SITES=json.dumps(([str(args.site)] if args.site else []) + info.get('sites', [])),
                   CORPUS_IMPORTS=json.dumps(pkg.get('imports', [])),
                   CORPUS_SKIPS=json.dumps(pkg.get('skip_tests', {})),
                   CORPUS_PARAMETER_IDS=pkg.get('parameter_ids', ''),
                   CORPUS_INDEXED_TESTS=json.dumps(pkg.get('indexed_tests', [])),
                   CORPUS_TIMEOUT_FLOOR=str(pkg.get('timeout_floor', 0)),
                   CORPUS_OVERRIDES=json.dumps(overrides),
                   PYTHONPATH=os.pathsep.join([str(HERE), str(source), *[str(source / p) for p in pkg.get('pythonpath', [])]]))
        # The target's site-packages wins over flat source packages with native modules.
        bootstrap = ('import os,sys,site,json; s=json.loads(os.environ["CORPUS_OVERRIDES"])+([os.environ["CORPUS_SITE"]] if os.environ["CORPUS_SITE"] else [])+site.getsitepackages(); '
                     'sys.path[:]=s+[p for p in sys.path if p not in s]; '
                     'import importlib,json; [importlib.import_module(n) for n in json.loads(os.environ["CORPUS_IMPORTS"])]; ')
        argv = ['-s', '-c', bootstrap + 'import pytest; raise SystemExit(pytest.main(sys.argv[1:]))', '-p', 'recorder', '-p', 'pytest_timeout',
                '-p', 'hypothesis.extra.pytestplugin', '--hypothesis-seed=0',
                '-p', 'pytest_mock', '-p', 'pytest_benchmark.plugin',
                *[part for plugin in pkg.get('plugins', []) for part in ('-p', plugin)],
                '--rootdir', str(source), '--import-mode=' + pkg.get('import_mode', 'importlib'),
                '--timeout=60', '--timeout-method=signal', '--tb=short', '-q',
                *['--ignore=' + str(source / path) for path in pkg.get('ignore', [])],
                *pkg.get('args', []),
                *[str(source / path) for path in pkg['tests']]]
        if pkg.get('framework') == 'unittest':
            argv = ['-s', '-c', bootstrap + 'import runpy; runpy.run_module("unittest_worker",run_name="__main__")']
        if args.action == 'collect':
            argv.append('--collect-only')
        code = execute(command + argv, source if pkg.get('cwd_source') else work,
                       env, work / 'pytest.log', args.timeout)
        data = events(journal)
        collections = [e['collection'] for e in data if 'collection' in e]
        collected = collections[-1] if collections else []
        state = next((e['state'] for e in reversed(data) if 'state' in e), {})
        allowed_skips = dict(pkg.get('collection_skips', {}).get('always', {}))
        if state.get('free_threaded'):
            allowed_skips.update(pkg.get('collection_skips', {}).get('free_threaded', {}))
        valid_collection = bool(collected) and len(collected) == len(set(collected)) and state.get('exitstatus') not in (2, 3, 4, 5)
        if args.action == 'collect':
            if not valid_collection or code != 0:
                print(f'{args.package}: invalid collection; inspect external pytest.log', file=sys.stderr)
                return 1
            if rows and set(rows) != set(collected) | set(allowed_skips):
                print(f'{args.package}: collection differs from pinned expected inventory', file=sys.stderr)
                return 1
            rows = {name: ('NOTRUN', 0.0) for name in set(collected) | set(allowed_skips)}
            manifest(expected, rows)
            print(f'{args.package}: collected {len(rows)}')
            return 0
        unknown = set(collected) - set(rows)
        if unknown:
            raise ValueError('unexpected test IDs: ' + ', '.join(sorted(unknown)[:5]))
        reason = 'completed' if code == 0 else 'pytest returned ' + str(code)
        if state.get('free_threaded') and state.get('gil_enabled') is not False:
            reason += '; free-threaded GIL enabled'
        for ident, explanation in allowed_skips.items():
            if ident in rows and ident not in collected:
                data.append({'id': ident, 'phase': 'collection', 'outcome': 'skipped',
                             'seconds': 0.0, 'xfail': False, 'reason': explanation})
        active = None
        for event in data:
            if 'active' in event:
                active = event['active']
            if 'id' not in event or event['id'] not in rows:
                continue
            name = event['id']
            old, elapsed = rows[name]
            phase, outcome = event['phase'], event['outcome']
            status = old
            if outcome == 'failed':
                if '[XPASS(strict)]' in event.get('reason', ''):
                    status = 'XPASS'
                elif 'Timeout (>' in event.get('reason', ''):
                    status = 'TIMEOUT'
                else:
                    status = 'FAIL' if phase == 'call' else 'ERROR'
            elif outcome == 'skipped' and old not in ('FAIL', 'ERROR', 'TIMEOUT', 'XPASS'):
                status = 'XFAIL' if event['xfail'] else 'SKIP'
            elif phase == 'call' and old not in ('FAIL', 'ERROR', 'TIMEOUT', 'XPASS'):
                status = 'XPASS' if event['xfail'] else 'PASS'
            rows[name] = (status, elapsed + event['seconds'])
            if phase == 'teardown' or pkg.get('framework') == 'unittest':
                active = None
        if code == 'TIMEOUT' or isinstance(code, int) and code < 0:
            if active in rows and rows[active][0] not in ('FAIL', 'ERROR'):
                rows[active] = ('TIMEOUT' if code == 'TIMEOUT' else 'CRASH', rows[active][1])
        if not valid_collection:
            reason += '; incomplete collection'
    if not rows:
        raise ValueError('cannot score without an established expected inventory')
    manifest(target / (args.runtime + '.tsv'), rows)
    def public(value):
        text = json.dumps(value, ensure_ascii=True)
        python = Path(shlex.split(args.python)[0]).resolve()
        for path, label in [(args.work, '<work>'), (args.cache, '<cache>'), (args.env, '<env>'),
                            (python.parent.parent, '<python>'), (ROOT, '<repo>'), (Path.home(), '<external>')]:
            if path:
                text = text.replace(str(Path(path).resolve()), label).replace(str(path), label)
        text = re.sub(r'pytest-of-[^/\\\s]+', 'pytest-of-runtime', text)
        return json.loads(text)
    evidence = {'package': args.package, 'version': pkg['version'], 'runtime': args.runtime,
                'source_sha256': pkg['source']['sha256'], **fingerprints,
                'test_sources': {name: entry['sha256'] for name, entry in pkg.get('test_sources', {}).items()},
                'environment_omit': pkg.get('environment_omit', []),
                'reason': reason, 'returncode': code, 'counts': dict(Counter(status for status, _ in rows.values())),
                'state': next((e['state'] for e in reversed(data) if 'state' in e), None),
                'collection_sha256': hashlib.sha256('\n'.join(sorted(next((e['collection'] for e in reversed(data) if 'collection' in e), []))).encode()).hexdigest(),
                'upstream_ids': next((e.get('upstream_ids', {}) for e in data if 'collection' in e), {})
                                if pkg.get('publish_upstream_ids', True) else {},
                'outcomes': [{k:v for k,v in e.items() if k != 'seconds'} for e in data if e.get('reason')]}
    (target / (args.runtime + '.json')).write_text(json.dumps(public(evidence), indent=2) + '\n')
    print(args.package, args.runtime, evidence['counts'], flush=True)
    return int(ready and (code != 0 or 'GIL enabled' in reason or any(
        status in ('FAIL','ERROR','XPASS','TIMEOUT','CRASH','NOTRUN') for status, _ in rows.values())))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['setup', 'collect', 'run'])
    parser.add_argument('--python', required=True, help='interpreter command, parsed without a shell')
    parser.add_argument('--package')
    parser.add_argument('--runtime', default='lucifer')
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--env', type=Path)
    parser.add_argument('--site', type=Path, help='reuse installed wheels, including cp314t binaries, for another runtime')
    parser.add_argument('--lock', type=Path, help='dependency lock snapshot; defaults to the current requirements.lock')
    parser.add_argument('--results', type=Path, default=ROOT / 'results/pypi')
    parser.add_argument('--timeout', type=float, default=900)
    args = parser.parse_args()
    if (not re.fullmatch(r'[A-Za-z0-9_.-]+', args.runtime) or args.runtime == 'expected'
            or not math.isfinite(args.timeout) or args.timeout <= 0):
        parser.error('invalid runtime or timeout')
    for name in ('cache', 'work', 'env', 'site', 'results', 'lock'):
        value = getattr(args, name)
        if value is not None:
            setattr(args, name, value.absolute())
    pin = json.loads((HERE / 'corpus.json').read_text())
    if args.action == 'setup':
        if args.env is None:
            parser.error('setup requires --env')
        setup(args, pin)
        return 0
    if args.package not in pin['packages']:
        parser.error('--package must name a locked package')
    return run(args, pin)


if __name__ == '__main__':
    def terminated(signum, frame):
        raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, terminated)
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print('pypi:', error, file=sys.stderr)
        sys.exit(2)
