"""Host-Python utilities shared by the benchmark drivers (stdlib only)."""
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
STATUSES = 'PASS FAIL ERROR SKIP XFAIL XPASS TIMEOUT CRASH NOTRUN'.split()
HEADER = ['id', 'status', 'seconds']


def command(text):
    return shlex.split(text)


def environment():
    env = os.environ.copy()
    env.update(PYTHONHASHSEED='0', PYTHON_JIT='0', TZ='UTC', LC_ALL='C.UTF-8',
               OMP_NUM_THREADS='1', OPENBLAS_NUM_THREADS='1',
               PYTHONDONTWRITEBYTECODE='1')
    env.pop('PYTHONPATH', None)
    env.pop('PYTHONHOME', None)
    env.pop('PYTHONOPTIMIZE', None)
    env.pop('PYTHON_GIL', None)
    return env


def run(argv, timeout=30, cwd=None, env_overrides=None):
    """Bound the entire child process group, including pyperf workers."""
    start = time.monotonic()
    try:
        env = environment()
        env.update(env_overrides or {})
        proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, errors='replace', cwd=cwd,
                                env=env, start_new_session=True)
    except OSError:
        return 'NOTRUN', '', '', time.monotonic() - start
    try:
        out, err = proc.communicate(timeout=timeout)
        status = 'PASS' if proc.returncode == 0 else ('CRASH' if proc.returncode < 0 else 'ERROR')
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        out, err = proc.communicate()
        status = 'TIMEOUT'
    return status, out, err, time.monotonic() - start


PROBE = '''import json, sys, sysconfig
j = getattr(sys, '_jit', None)
print(json.dumps(dict(version=list(sys.version_info[:3]),
 python_executable=sys.executable,
 implementation=sys.implementation.name,
 abi_flags=getattr(sys, 'abiflags', ''),
 configure_args=sysconfig.get_config_var('CONFIG_ARGS'),
 compiler=sysconfig.get_config_var('CC'),
 free_threaded=bool(sysconfig.get_config_var('Py_GIL_DISABLED')),
 gil_enabled=getattr(sys, '_is_gil_enabled', lambda: True)(),
 jit_available=bool(j and j.is_available()),
 jit_enabled=bool(j and j.is_enabled()))))
'''


def probe(argv):
    status, out, _, _ = run(argv + ['-c', PROBE], timeout=15)
    try:
        info = json.loads(out) if status == 'PASS' else None
        if not isinstance(info, dict) or not isinstance(info['version'], list):
            return None
        binary = info.pop('python_executable', None) or (shutil.which(argv[0]) if argv else None)
        if binary:
            info['executable_sha256'] = sha256(binary)
        info['arguments'] = public_data(argv[1:])
        return public_data(info)
    except (OSError, ValueError, KeyError, TypeError):
        return None


def runtime_name(value):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', value) or value == 'expected':
        raise ValueError('runtime must be a plain filename component other than expected')
    return value


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + '\n')
    temporary.replace(path)


def write_manifest(directory, runtime, rows, expected):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    for name, values in [('expected', [(name, 'NOTRUN', 0) for name in expected]),
                         (runtime, rows)]:
        path = directory / (name + '.tsv')
        with path.open('w', newline='') as stream:
            writer = csv.writer(stream, delimiter='\t', lineterminator='\n')
            writer.writerow(HEADER)
            for ident, status, seconds in sorted(values):
                assert status in STATUSES
                writer.writerow([ident, status, f'{seconds:.9f}'])


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_hash(directory):
    digest = hashlib.sha256()
    for path in sorted(Path(directory).rglob('*')):
        if path.is_file() and '__pycache__' not in path.parts:
            digest.update(path.relative_to(directory).as_posix().encode() + b'\0')
            digest.update(path.read_bytes())
    return digest.hexdigest()


def public_data(value):
    """Omit identifying pyperf metadata; retain timing, calibration and CPU data."""
    private = {'hostname', 'python_executable', 'command', 'command_line',
               'executable', 'prefix', 'base_prefix', 'boot_time'}
    if isinstance(value, dict):
        return {key: public_data(item) for key, item in value.items() if key not in private}
    if isinstance(value, list):
        return [public_data(item) for item in value]
    if isinstance(value, str):
        # Includes traceback paths and pyperf's python_cflags/install prefixes.
        return re.sub(r'/(?:home|tmp|opt|var/tmp)/[^\s\"\'<>;,]+', '<local-path>', value)
    return value
