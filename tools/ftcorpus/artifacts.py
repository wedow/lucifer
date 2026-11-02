"""Check immutable installed wheel payloads without importing target packages."""
import hashlib
from pathlib import Path
import zipfile


def verify(lock, work, name):
    normalize = lambda value: value.lower().replace('_', '-').replace('.', '-')
    artifacts = [a for a in lock['artifacts'] if a['filename'].endswith('.whl') and normalize(a['name']) == normalize(name)]
    if len(artifacts) != 1:
        raise ValueError('one pinned wheel is required for ' + name)
    artifact = artifacts[0]
    wheel = work / 'downloads' / artifact['filename']
    if hashlib.sha256(wheel.read_bytes()).hexdigest() != artifact['sha256']:
        raise ValueError('wheel hash changed: ' + wheel.name)
    site = work / 'venv/lib/python3.14t/site-packages'
    files = 0
    with zipfile.ZipFile(wheel) as archive:
        for entry in archive.infolist():
            if entry.is_dir() or '.dist-info/' in entry.filename or '.data/' in entry.filename:
                continue
            installed = site / entry.filename
            if not installed.is_file() or hashlib.sha256(installed.read_bytes()).digest() != hashlib.sha256(archive.read(entry)).digest():
                raise ValueError('installed wheel payload changed: ' + entry.filename)
            files += 1
    if not files:
        raise ValueError('wheel contains no checked payload: ' + name)
    return {'wheel_sha256': artifact['sha256'], 'payload_files': files}
