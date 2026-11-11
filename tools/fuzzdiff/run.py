"""Differential fuzzing with python-formalization's case generators: a target interpreter against
the CPython oracle.

Both interpreters generate each generator's cases from a seed and run them, in batches of a process
each; the [case, result] pairs compare as JSON. A process that dies or runs out of time costs the case
it was on, and the batch resumes after it. Parts of a result that differ between two oracle runs
(ids, addresses) are not compared. One summary line per generator and seed goes to stdout, and each
case that does not match to WORK/results/GENERATOR-sSEED.jsonl.
"""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import resource
import shlex
import shutil
import signal
import subprocess
import sys
import tarfile
import urllib.request

from compare import compare

sys.set_int_max_str_digits(0)  # results carry ints of any size
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
LOCK = HERE / 'source.lock.json'
WORKER = HERE / 'worker.py'
# The difftest entry points at the locked commit (helpers and coverage tools left out).
GENERATORS = [
    'accel_diff', 'binary64_diff', 'binary64_complex_diff', 'bytecode_diff', 'bytecode_frame_diff',
    'bytecode_gen_diff', 'bytesobj_diff', 'corelibs_diff', 'datetime_diff', 'datetime_zoneinfo_diff',
    'decimal_diff', 'dictset_diff', 'dictset_sethooks_diff', 'fractions_diff', 'ipaddress_diff',
    'itertools_diff', 'json_diff', 'listsort_diff', 'longobj_diff', 'longobj_complex_diff',
    'longobj_str_diff', 'mathint_diff', 'mathint_float_diff', 'pickle_diff', 'pygrammar_peg',
    'pygrammar_lexer', 'pygrammar_check', 'pygrammar_unparse', 'pyio_base_diff', 'pyio_buffered_diff',
    'pyio_callback_diff', 'pyio_diff', 'pyio_share_diff', 'pyio_state_diff', 'pyio_text_diff',
    'rangeslice_diff', 'sequences_diff', 'strsearch_diff', 'textlibs_diff', 'typeobj_abstract_diff',
    'typeobj_diff', 'typeobj_exc_diff', 'typeobj_structseq_diff', 'unicodeobj_diff',
]
# The worker's SPEC for a generator that is not simply its script (see worker.py).
SPECS = {f'pygrammar_{part}': f'pygrammar_diff --part {part}' for part in ('peg', 'lexer', 'check', 'unparse')}
SPECS['pyio_buffered_diff'] = 'pyio_buffered_diff:gen_case:run_python'  # its main is its own


def fetch(entry, work):
    path = work / entry['url'].rsplit('/', 1)[1]
    if not path.exists():
        part = path.with_suffix('.part')
        with urllib.request.urlopen(entry['url'], timeout=120) as response, part.open('wb') as out:
            shutil.copyfileobj(response, out)
        part.replace(path)
    if hashlib.sha256(path.read_bytes()).hexdigest() != entry['sha256']:
        raise SystemExit(f'fuzzdiff: hash mismatch for {path.name}')
    return path


def drop_module_asserts(path):
    """Module-level asserts check which implementation loaded (a C accelerator or pure Python), and
    Lucifer implements some of those modules in Python; the cases still compare behaviour."""
    source = path.read_text()
    lines = source.splitlines(keepends=True)
    for node in ast.parse(source).body:
        if isinstance(node, ast.Assert):
            lines[node.lineno - 1:node.end_lineno] = ['pass\n'] + ['\n'] * (node.end_lineno - node.lineno)
    path.write_text(''.join(lines))


def prepare(work):
    """Unpack the generators, and a CPython tree of the locked grammar beside the vendored Lib and pegen.
    Returns the difftest directory and the CPython tree."""
    lock = {entry['name']: entry for entry in json.loads(LOCK.read_text())}
    tree = work / 'python-formalization'
    if not tree.exists():
        with tarfile.open(fetch(lock['python-formalization'], work)) as archive:
            archive.extractall(work / 'unpack', filter='data')
        (work / 'unpack').joinpath(lock['python-formalization']['tree']).rename(tree)
        (work / 'unpack').rmdir()
        for script in (tree / 'difftest').glob('*.py'):
            drop_module_asserts(script)
    cpython = work / 'cpython'
    if not cpython.exists():
        (cpython / 'Grammar').mkdir(parents=True)
        shutil.copy(fetch(lock['python.gram'], work), cpython / 'Grammar/python.gram')
        for part in ('Lib', 'Tools'):
            (cpython / part).symlink_to(ROOT / 'third_party/cpython' / part)
    return tree / 'difftest', cpython


class Interpreter:
    def __init__(self, command, difftest, cpython, timeout, memory):
        self.command, self.difftest, self.timeout, self.memory = command, difftest, timeout, memory
        self.env = dict(os.environ, PYTHONHASHSEED='0', PYTHONIOENCODING='utf-8', TZ='UTC',
                        CPYTHON_SRC=str(cpython))

    def limit(self):
        os.setsid()  # a group of its own, so a timeout also ends the children cases fork
        resource.setrlimit(resource.RLIMIT_AS, (self.memory, self.memory))

    def worker(self, *args):
        """Run the worker; returns its exit status ('timeout' for one killed for time) and stderr tail."""
        with subprocess.Popen(self.command + [str(WORKER), str(self.difftest), *map(str, args)], env=self.env,
                              stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                              preexec_fn=self.limit) as proc:
            try:
                err = proc.communicate(timeout=self.timeout)[1]
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.communicate()
                return 'timeout', ''
        return proc.returncode, err.decode(errors='replace')[-2000:]

    def run(self, spec, seed, start, stop, out):
        """The dropped-draw tally and a [case, result] pair for each case in [start, stop). A case
        whose process dies or times out gets a crash or timeout result, and the next process resumes
        after it; when generation itself fails, every remaining case does."""
        dropped, pairs = None, []
        while start + len(pairs) < stop:
            out.unlink(missing_ok=True)
            status, err = self.worker(spec, seed, start + len(pairs), stop, out)
            # each line ends in a newline when whole: the last piece is empty, or cut short by death
            lines = out.read_text().split('\n')[:-1] if out.exists() else []
            if lines:
                dropped = json.loads(lines[0])
            pairs += [json.loads(line) for line in lines[1:]]
            failure = ({'timeout': self.timeout} if status == 'timeout'
                       else {'crash': f'exit {status}', 'stderr': err})
            if not lines:
                pairs += [[None, failure]] * (stop - start - len(pairs))
            elif start + len(pairs) < stop:
                pairs.append([None, failure])
        return dropped, pairs


def fuzz(name, seed, count, batch, oracle, target, scratch, log):
    """Counts for one generator and seed; a record of each case that does not match goes to `log`."""
    spec = SPECS.get(name, name)
    counts = {'generator': name, 'seed': seed, 'cases': count, 'match': 0, 'mismatch': 0, 'skip': 0,
              'error': 0}
    dropped = None
    for start in range(0, count, batch):
        stop = min(start + batch, count)
        dropped, want = oracle.run(spec, seed, start, stop, scratch)
        _, got = target.run(spec, seed, start, stop, scratch)
        statuses = [compare(w, g) for w, g in zip(want, got)]
        if 'mismatch' in statuses:  # a second oracle run tells nondeterministic parts of a result
            _, again = oracle.run(spec, seed, start, stop, scratch)
            statuses = [compare(w, g, a) for w, g, a in zip(want, got, again)]
        for index, (status, w, g) in enumerate(zip(statuses, want, got), start):
            counts[status] += 1
            if status != 'match':
                record = {'status': status, 'seed': seed, 'index': index, 'case': w[0], 'oracle': w[1],
                          'target': g[1]}
                if g[0] not in (None, w[0]):
                    record['target_case'] = g[0]  # the target generated another case
                log.write(json.dumps(record) + '\n')
    scratch.unlink(missing_ok=True)
    counts['dropped'] = dropped
    return counts


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('generators', nargs='*', choices=GENERATORS, metavar='GENERATOR',
                   help='difftest entry points (default: all of them)')
    p.add_argument('--oracle', required=True, help='CPython 3.14t command')
    p.add_argument('--python', default=str(ROOT / 'bin/lucifer'), help='target command as shell words')
    p.add_argument('--seed', type=int, action='append', help='repeatable (default: 0)')
    p.add_argument('--cases', type=int, default=1000, help='per generator and seed')
    p.add_argument('--batch', type=int, default=250, help='cases per process')
    p.add_argument('--timeout', type=int, default=300, help='seconds per process')
    p.add_argument('--memory', type=int, default=8, help='GiB of address space per process')
    p.add_argument('--work', type=Path, default=ROOT / 'build/fuzzdiff')
    args = p.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    difftest, cpython = prepare(work)
    limits = (args.timeout, args.memory << 30)
    oracle = Interpreter(shlex.split(args.oracle), difftest, cpython, *limits)
    target = Interpreter(shlex.split(args.python), difftest, cpython, *limits)
    (work / 'results').mkdir(exist_ok=True)
    failed = False
    for name in args.generators or GENERATORS:
        for seed in args.seed or [0]:
            stem = f'{name}-s{seed}'
            with (work / 'results' / f'{stem}.jsonl').open('w') as log:
                counts = fuzz(name, seed, args.cases, args.batch, oracle, target, work / f'{stem}.out', log)
            print(json.dumps(counts), flush=True)
            failed |= bool(counts['mismatch'] or counts['error'])
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
