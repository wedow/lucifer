#!/usr/bin/env python3
"""Download verified artifacts and prepare an external, offline cp314t corpus."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

HERE = Path(__file__).resolve().parent


def fetch(artifact, cache):
    path = cache / artifact['filename']
    if not path.exists():
        temporary = path.with_suffix(path.suffix + '.part')
        with urllib.request.urlopen(artifact['url'], timeout=120) as response, temporary.open('wb') as output:
            shutil.copyfileobj(response, output)
        temporary.replace(path)
    if hashlib.sha256(path.read_bytes()).hexdigest() != artifact['sha256']:
        raise ValueError('artifact hash mismatch: ' + path.name)
    return path


def stage_tests(lock, work, name, target):
    package = lock['packages'][name]
    source = work / 'sources' / package['source_dir']
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    for entry in package['test_files']:
        origin, dest = source / entry, target / entry
        if origin.is_dir():
            shutil.copytree(origin, dest, ignore=shutil.ignore_patterns('__pycache__', '.pytest_cache', '.hypothesis'))
        elif origin.is_file():
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(origin, dest)
        else:
            raise ValueError('missing test source: ' + name + '/' + entry)
    if 'wheel_overlay' in package:
        payload = work / 'venv/lib/python3.14t/site-packages' / package['wheel_overlay']
        overlay = target / package['wheel_overlay']
        overlay.mkdir(exist_ok=True)
        for origin in payload.iterdir():
            dest = overlay / origin.name
            if not dest.exists() and origin.name != '__pycache__':
                dest.symlink_to(origin, target_is_directory=origin.is_dir())


def prepare(lock, work, python):
    work.mkdir(parents=True, exist_ok=True)
    cache = work / 'downloads'
    cache.mkdir(exist_ok=True)
    for artifact in lock['artifacts']:
        fetch(artifact, cache)
    sources = work / 'sources'
    sources.mkdir(exist_ok=True)
    tests = work / 'tests'
    tests.mkdir(exist_ok=True)
    for name, package in lock['packages'].items():
        artifact = next(a for a in lock['artifacts'] if a['filename'] == package['sdist'])
        # Recreate pristine sources from the verified archive, never the reference clones.
        source = sources / package['source_dir']
        if source.exists():
            shutil.rmtree(source)
        with tarfile.open(cache / artifact['filename']) as archive:
            archive.extractall(sources, filter='data')
    venv = work / 'venv'
    if venv.exists():
        if not (venv / 'pyvenv.cfg').is_file():
            raise ValueError('refusing to replace a non-venv directory')
        shutil.rmtree(venv)
    subprocess.run([python, '-m', 'venv', str(venv)], check=True)
    requirements = work / 'requirements.txt'
    requirements.write_text('\n'.join(
        f"{a['name']}=={a['version']} --hash=sha256:{a['sha256']}"
        for a in lock['artifacts'] if a['filename'].endswith('.whl')) + '\n')
    subprocess.run([str(venv / 'bin/python'), '-m', 'pip', 'install', '--no-index',
                    '--find-links', str(cache), '--require-hashes', '--only-binary=:all:',
                    '--no-deps', '-r', str(requirements)], check=True)
    for name in lock['packages']:
        stage_tests(lock, work, name, tests / name)
    # This environment is not allowed to resolve or fetch additional artifacts.
    print('prepared', len(lock['packages']), 'packages with', len(lock['artifacts']), 'hashed artifacts')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--python', required=True, help='pinned cp314t executable')
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--lock', type=Path, default=HERE / 'packages.lock.json')
    args = parser.parse_args()
    prepare(json.loads(args.lock.read_text()), args.work.resolve(), args.python)
