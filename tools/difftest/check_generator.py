"""Optional binary checks for the separately installed Hypothesis lane."""
import argparse
import csv
import json
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

from run import ROOT, HERE, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generator-python', type=Path, required=True)
    parser.add_argument('--oracle', type=Path, required=True)
    args = parser.parse_args()
    generator, oracle = args.generator_python.absolute(), args.oracle.absolute()
    (ROOT / '.difftest-work').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='generator-check-', dir=ROOT / '.difftest-work') as raw:
        work = Path(raw)
        for name in ['first', 'replay']:
            subprocess.run([str(generator), str(HERE / 'generate.py'), 'emit', '--seed', '731',
                '--count', '12', '--output', str(work / name)], check=True, timeout=60)
        first = {p.name: p.read_bytes() for p in (work / 'first').iterdir()}
        replay = {p.name: p.read_bytes() for p in (work / 'replay').iterdir()}
        assert first == replay, 'same pin and seed must reproduce exact programs and specs'
        assert len(list((work / 'first').glob('*.py'))) == 12
        fake = work / 'fake.py'
        fake.write_text('''import os, subprocess, sys
from pathlib import Path
if sys.argv[1] == '-c': os.execv(sys.executable, [sys.executable] + sys.argv[1:])
p = subprocess.run([sys.executable] + sys.argv[1:], capture_output=True)
sys.stdout.buffer.write(p.stdout); sys.stderr.buffer.write(p.stderr)
if 'xs.insert(' in Path(sys.argv[2]).read_text(): print('injected insert defect')
sys.exit(p.returncode)
''')
        target = shlex.join([str(oracle), str(fake)])
        result = subprocess.run([str(generator), str(HERE / 'generate.py'), 'fuzz',
            '--seed', '731', '--count', '40', '--output', str(work / 'fuzz'),
            '--oracle', str(oracle), '--python', target, '--loops', '2', '--timeout', '3'],
            capture_output=True, text=True, timeout=180)
        assert result.returncode != 0, 'injected bug must be found'
        minimal = work / 'fuzz/minimized.py'
        assert minimal.is_file(), result.stdout + result.stderr
        spec = json.loads((work / 'fuzz/minimized.json').read_text())['spec']
        assert any(op == 'insert' for op, _ in spec['ops'])
        assert len(spec['ops']) == 4 and spec['xs'] == [] and spec['s'] == ''
        inventory = work / 'inventory.json'
        inventory.write_text(json.dumps({'schema': 1, 'cases': {
            minimal.relative_to(ROOT).as_posix(): {'sha256': digest(minimal.read_bytes()),
                'state': True, 'timing': False, 'exit': 0}}}))
        replay = subprocess.run([sys.executable, str(HERE), 'run', '--oracle', str(oracle),
            '--python', target, '--runtime', 'fake', '--loops', '2', '--inventory', str(inventory),
            '--results', str(work / 'results'), '--work', str(work / 'reproduction')], timeout=30)
        assert replay.returncode == 1
        with (work / 'results/fake.tsv').open() as stream:
            assert [row['status'] for row in csv.DictReader(stream, delimiter='\t')] == ['FAIL']
    print('generator checks: seeded replay, shrinking, standalone failure replay passed')


if __name__ == '__main__':
    main()
