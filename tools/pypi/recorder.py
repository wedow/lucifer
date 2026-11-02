"""Pytest adapter executed by the target, without modifying upstream tests."""
import json
import errno
import ipaddress
import hashlib
import importlib.metadata
import os
from pathlib import Path
import sys
import sysconfig
import socket

import pytest


def offline(event, args):
    if event == 'socket.connect':
        address = args[1]
        if not isinstance(address, tuple):  # Unix-domain sockets are local.
            return
        host = address[0]
    elif event in ('socket.getaddrinfo', 'socket.gethostbyname', 'socket.gethostbyaddr'):
        host = args[0]
    elif event == 'socket.getnameinfo':
        host = args[0][0]
    else:
        return
    if isinstance(host, bytes):
        try:
            host = host.decode('ascii')
        except UnicodeError:
            host = '<invalid-host>'
    if host in (None, '', 'localhost'):
        return
    try:
        address = ipaddress.ip_address(host)
        if address.is_loopback or event == 'socket.connect' and address.is_unspecified:
            return
    except ValueError:
        pass
    if event != 'socket.connect':
        raise socket.gaierror(socket.EAI_NONAME, 'corpus: external DNS disabled')
    raise OSError(errno.ENETUNREACH, 'corpus: external network disabled')


sys.addaudithook(offline)


def pytest_configure(config):
    config.addinivalue_line('markers', 'thread_unsafe: upstream serial-only test')
    config.addinivalue_line('markers', 'parallel_threads(n): upstream concurrency annotation')


def emit(event):
    with open(os.environ['CORPUS_JOURNAL'], 'a', encoding='utf-8') as stream:
        stream.write(json.dumps(event, ensure_ascii=True) + '\n')


def canonical(name):
    name = name.replace(os.environ['CORPUS_SOURCE'] + '/', '')
    for site in json.loads(os.environ.get('CORPUS_SITES', '[]')):
        name = name.replace(site + '/', 'site-packages/')
    return name


def ident(item):
    name = canonical(item.nodeid)
    indexed = getattr(item, 'originalname', None) in json.loads(os.environ.get('CORPUS_INDEXED_TESTS', '[]'))
    if (os.environ.get('CORPUS_PARAMETER_IDS') == 'indices' or indexed) and hasattr(item, 'callspec'):
        indices = ','.join(f'{key}={index}' for key, index in sorted(item.callspec.indices.items()))
        name = canonical(item.parent.nodeid) + '::' + item.originalname + '[' + indices + ']'
    if os.environ['CORPUS_PACKAGE'] == 'pyyaml' and hasattr(item, '_function') and not item._fargs:
        name += '::' + item._function.__name__
    return name


@pytest.hookimpl(trylast=True)
def pytest_collection_modifyitems(items):
    skips = json.loads(os.environ.get('CORPUS_SKIPS', '{}'))
    floor = float(os.environ.get('CORPUS_TIMEOUT_FLOOR', '0'))
    for item in items:
        marker = item.get_closest_marker('timeout')
        if marker:
            limit = marker.args[0] if marker.args else marker.kwargs.get('timeout', 0)
            if isinstance(limit, (int, float)) and 0 < limit < floor:
                item.add_marker(pytest.mark.timeout(floor), append=False)
        original = getattr(item, 'originalname', item.name.split('[', 1)[0])
        if original in skips:
            item.add_marker(pytest.mark.skip(reason='corpus: ' + skips[original]))
        if 'requires_network' in item.keywords:
            item.add_marker(pytest.mark.skip(reason='corpus: external network disabled'))
        if os.environ['CORPUS_PACKAGE'] == 'requests' and any(
                name.startswith('httpbin') for name in item.fixturenames):
            item.add_marker(pytest.mark.skip(reason='corpus: HTTP service fixtures outside offline subset'))
    emit({'collection': [ident(item) for item in items],
          'upstream_ids': {ident(item): canonical(item.nodeid) for item in items
                           if ident(item) != canonical(item.nodeid)}})


@pytest.hookimpl(hookwrapper=True, tryfirst=True)
def pytest_runtest_protocol(item, nextitem):
    emit({'active': ident(item)})
    yield


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    report = (yield).get_result()
    emit({'id': ident(item), 'phase': report.when, 'outcome': report.outcome,
          'seconds': report.duration, 'xfail': hasattr(report, 'wasxfail'),
          'reason': str(report.longrepr) if report.failed or report.skipped else ''})


def pytest_sessionfinish(session, exitstatus):
    native = {}
    for name, module in list(sys.modules.items()):
        filename = getattr(module, '__file__', None)
        if filename and filename.endswith(('.so', '.pyd')) and 'site-packages' in filename:
            with open(filename, 'rb') as stream:
                native[name] = hashlib.file_digest(stream, 'sha256').hexdigest()
    emit({'state': {'version': sys.version.split()[0], 'abi': sys.abiflags,
                   'package_version': importlib.metadata.version(os.environ['CORPUS_PACKAGE']),
                   'native_modules': native,
                   'native_library_versions': {
                       'zlib': getattr(sys.modules.get('zlib'), 'ZLIB_RUNTIME_VERSION', None),
                       'openssl': getattr(sys.modules.get('ssl'), 'OPENSSL_VERSION', None),
                       'sqlite': getattr(sys.modules.get('sqlite3'), 'sqlite_version', None)},
                   'gil_enabled': sys._is_gil_enabled() if hasattr(sys, '_is_gil_enabled') else None,
                   'free_threaded': bool(sysconfig.get_config_var('Py_GIL_DISABLED')),
                   'exitstatus': int(exitstatus)}})
