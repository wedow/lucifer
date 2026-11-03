#!/usr/bin/env python3
"""Binary-level checks: availability, coverage, interrupted runs and artifact trust."""
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile

HERE = Path(__file__).resolve().parent


def main():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        harness = root / 'tools/pypi'
        harness.mkdir(parents=True)
        shutil.copy(HERE / 'run.py', harness)
        (root / 'tools/lib').mkdir()
        shutil.copy(HERE.parent / 'lib/exitcode.py', root / 'tools/lib')
        for name in ('recorder.py', 'unittest_worker.py'):
            shutil.copy(HERE / name, harness)
        (harness / 'requirements.lock').write_text('# fixture\n')
        archive = root / 'suite.tar.gz'
        with tarfile.open(archive, 'w:gz') as stream:
            data = b'pass\n'
            entry = tarfile.TarInfo('suite/test_sample.py')
            entry.size = len(data)
            stream.addfile(entry, io.BytesIO(data))
        pin = {'packages': {'fixture': {'version':'1', 'tests':['test_sample.py'],
                'source': {'filename':archive.name, 'url':'https://invalid.example/',
                           'sha256':hashlib.sha256(archive.read_bytes()).hexdigest()}}}}
        (harness / 'corpus.json').write_text(json.dumps(pin))
        result = root / 'results/fixture'
        result.mkdir(parents=True)
        expected = 'id\tstatus\tseconds\na\tNOTRUN\t0\nb\tNOTRUN\t0\n'
        (result / 'expected.tsv').write_text(expected)
        fake = root / 'fake.py'
        fake.write_text('''import json,os,shutil,signal,sys,time
mode=sys.argv[1]
if mode=='symlink' and not sys.executable.endswith('linked-python'): raise SystemExit(7)
if mode=='fail': raise SystemExit(7)
if mode=='bad': print('not JSON'); raise SystemExit()
if 'CORPUS_JOURNAL' not in os.environ:
 print(json.dumps({'ok':True,'version':'fixture','prefix':sys.prefix})); raise SystemExit()
f=open(os.environ['CORPUS_JOURNAL'],'a',buffering=1)
def emit(x): f.write(json.dumps(x)+'\\n')
if mode=='viewers': emit({'collection':['a','b']+(['viewer1'] if shutil.which('display') else [])}); emit({'state':{'exitstatus':0}}); raise SystemExit()
if mode=='prefix': emit({'collection':['a','b']}); emit({'id':'a','phase':'call','outcome':'failed','seconds':0.0,'xfail':False,'reason':'in '+sys.prefix}); emit({'id':'b','phase':'call','outcome':'passed','seconds':0.0,'xfail':False,'reason':''}); emit({'state':{'exitstatus':1}}); raise SystemExit()
if mode=='empty': emit({'collection':[]}); emit({'state':{'exitstatus':0}}); raise SystemExit()
if mode=='hypothesis':
 # Each run must draw the same examples, with no replay of earlier runs.
 store=os.path.join(os.environ['HYPOTHESIS_STORAGE_DIRECTORY'],'replay')
 seeded='--hypothesis-seed=0' in sys.argv and not os.path.exists(store)
 os.makedirs(store,exist_ok=True)
 if not seeded: mode='fail-a'
emit({'collection':['a','b']+(['z'] if mode=='extra' else [])})
emit({'id':'a','phase':'call','outcome':'failed' if mode=='fail-a' else 'passed','seconds':0.01,'xfail':False,'reason':''})
emit({'active':'b'})
if mode=='timeout': time.sleep(10)
if mode=='crash': os.kill(os.getpid(),signal.SIGTERM)
if mode=='segv': os.kill(os.getpid(),signal.SIGSEGV)
emit({'id':'b','phase':'call','outcome':'skipped','seconds':0.0,'xfail':True,'reason':'expected'})
emit({'state':{'exitstatus':0,'free_threaded':mode=='collection-skip','gil_enabled':False}})
''')
        def run(command, runtime, results_root=None):
            return subprocess.run([sys.executable,str(harness/'run.py'),'run', '--python',command,
                '--package','fixture','--runtime',runtime,'--cache',str(root), '--work',str(root/'work'),
                '--results',str(results_root or root/'results'),'--timeout','0.5'], capture_output=True, text=True)
        checks = 0
        for name,command in [('missing',str(root/'absent')),('fail',f'{sys.executable} {fake} fail'),
                              ('bad',f'{sys.executable} {fake} bad')]:
            p = run(command,name)
            assert p.returncode == 0, p.stderr
            assert (result/(name+'.tsv')).read_text().count('NOTRUN')==2
            checks += 1
        for mode,status in [('pass','XFAIL'),('timeout','TIMEOUT'),('crash','CRASH')]:
            p = run(f'{sys.executable} {fake} {mode}',mode)
            rows=(result/(mode+'.tsv')).read_text()
            assert 'a\tPASS\t' in rows and 'b\t'+status+'\t' in rows, (p.stderr,rows)
            assert p.returncode == (0 if mode=='pass' else 1)
            checks += 1
        # A shell wrapper reports a fatal signal as exit 128+N.
        wrapper = root / 'wrapper'
        wrapper.write_text(f'#!/bin/sh\n{sys.executable} {fake} "$@"\n')
        wrapper.chmod(0o755)
        p = run(f'{wrapper} segv', 'wrapper')
        rows = (result/'wrapper.tsv').read_text()
        assert 'a\tPASS\t' in rows and 'b\tCRASH\t' in rows, (p.stderr, rows)
        checks += 1
        # A refused run must not leave an earlier run's passes in place.
        (result/'stale.tsv').write_text(expected.replace('NOTRUN', 'PASS'))
        p = run(f'{sys.executable} {fake} extra', 'stale')
        assert p.returncode == 2 and 'unexpected test IDs' in p.stderr, p.stderr
        assert 'PASS' not in (result/'stale.tsv').read_text()
        checks += 1
        for attempt in range(2):
            p = run(f'{sys.executable} {fake} hypothesis', 'hypothesis')
            assert 'a\tPASS\t' in (result/'hypothesis.tsv').read_text(), (attempt, p.stderr)
        checks += 1
        linked = root / 'linked-python'
        linked.symlink_to(sys.executable)
        p = run(f'{linked} {fake} symlink', 'symlink')
        assert p.returncode == 0 and 'a\tPASS\t' in (result/'symlink.tsv').read_text()
        checks += 1
        p = subprocess.run([sys.executable,str(harness/'run.py'),'collect','--python',
            f'{sys.executable} {fake} empty','--package','fixture','--runtime','empty',
            '--cache',str(root),'--work',str(root/'work'),'--results',str(root/'results')],
            capture_output=True,text=True)
        assert p.returncode == 1 and (result/'expected.tsv').read_text() == expected
        checks += 1
        skipped = root / 'skip-results/fixture'
        skipped.mkdir(parents=True)
        (skipped/'expected.tsv').write_text(expected+'c\tNOTRUN\t0\n')
        pin['packages']['fixture']['collection_skips']={'free_threaded':{'c':'upstream GIL-only case'}}
        (harness/'corpus.json').write_text(json.dumps(pin))
        p = run(f'{sys.executable} {fake} collection-skip','skip',root/'skip-results')
        assert p.returncode == 0 and 'c\tSKIP\t' in (skipped/'skip.tsv').read_text(), p.stderr
        checks += 1
        # A venv outside the fixture root: its bin holds no viewer, and its prefix is the environment path.
        with tempfile.TemporaryDirectory() as outside:
            venv = Path(outside) / 'env'
            subprocess.run([sys.executable, '-m', 'venv', str(venv)], check=True)
            venv_python = venv / 'bin' / 'python'
            # Host PATH entries must not change collected ids: only the pinned PATH reaches the suite.
            no_viewers = root / 'no-viewers'
            no_viewers.mkdir()
            viewer_bin = root / 'viewer-bin'
            viewer_bin.mkdir()
            (viewer_bin / 'display').write_text('#!/bin/sh\nexit 0\n')
            (viewer_bin / 'display').chmod(0o755)
            viewers = root / 'viewer-results'

            def collect_on(path_entry):
                return subprocess.run([sys.executable, str(harness / 'run.py'), 'collect', '--python',
                    f'{venv_python} {fake} viewers', '--package', 'fixture', '--runtime', 'viewers',
                    '--cache', str(root), '--work', str(root / 'work'), '--results', str(viewers)],
                    capture_output=True, text=True, env={**os.environ, 'PATH': str(path_entry)})
            p = collect_on(no_viewers)
            assert p.returncode == 0, p.stderr
            p = collect_on(viewer_bin)
            assert p.returncode == 0 and 'differs' not in p.stderr, p.stderr
            checks += 1
            # Without --env, the environment path (the interpreter's prefix) must not reach committed evidence.
            prefixed = root / 'prefix-results'
            (prefixed / 'fixture').mkdir(parents=True)
            (prefixed / 'fixture' / 'expected.tsv').write_text(expected)
            p = subprocess.run([sys.executable, str(harness / 'run.py'), 'run', '--python', f'{venv_python} {fake} prefix',
                '--package', 'fixture', '--runtime', 'prefix', '--cache', str(root), '--work', str(root / 'work'),
                '--results', str(prefixed), '--timeout', '5'], capture_output=True, text=True)
            evidence = (prefixed / 'fixture' / 'prefix.json').read_text()
            assert str(venv) not in evidence and str(venv.resolve()) not in evidence and '<env>' in evidence, (p.stderr, evidence)
            checks += 1
        archive.write_bytes(b'corrupt')
        p = run(f'{sys.executable} {fake} pass','tamper')
        assert p.returncode == 2 and 'hash mismatch' in p.stderr
        checks += 1
        scoreboard=HERE.parents[1]/'tools/scoreboard'
        p=subprocess.run([sys.executable,str(scoreboard),str(root/'results')],capture_output=True,text=True)
        assert p.returncode == 0 and '0.00%' in p.stdout and '50.00%' in p.stdout
        checks += 1
        guard = '''import errno,socket,sys,types
pytest=types.ModuleType('pytest')
pytest.hookimpl=lambda **kw: lambda function: function
sys.modules['pytest']=pytest
sys.path.insert(0,sys.argv[1])
import recorder
for action in (lambda: socket.getaddrinfo('example.invalid',80),
               lambda: socket.socket().connect(('192.0.2.1',80))):
 try: action()
 except OSError as exc: assert exc.errno in (errno.ENETUNREACH,socket.EAI_NONAME)
 else: raise AssertionError('external network was allowed')
with socket.socket() as server, socket.socket() as client:
 server.bind(('127.0.0.1',0)); server.listen()
 client.connect(server.getsockname())
 connection,_=server.accept(); connection.close()
'''
        p = subprocess.run([sys.executable, '-c', guard, str(HERE)], capture_output=True, text=True, timeout=5)
        assert p.returncode == 0, p.stderr
        checks += 1
        print(f'{checks} package-corpus binary checks passed')


if __name__=='__main__':
    main()
