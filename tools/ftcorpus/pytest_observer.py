"""Target-side pytest event observer; assertions and selectors remain upstream."""
import json
import os
import random
import sys
import sysconfig
import pytest


def emit(value):
    with open(os.environ['FT_OBSERVATIONS'], 'a', encoding='utf-8') as stream:
        stream.write(json.dumps(value, ensure_ascii=True) + '\n')


def gil(stage):
    emit({'stage': stage, 'free_threaded': sysconfig.get_config_var('Py_GIL_DISABLED') == 1,
          'gil_enabled': getattr(sys, '_is_gil_enabled', lambda: True)()})


@pytest.hookimpl(tryfirst=True)
def pytest_configure(config):
    # The timing fixture can be called only once and is shared between copies.
    # Preserve its functional check using the plugin's ordinary serial fallback.
    config.addinivalue_line('thread_unsafe_fixtures', 'benchmark')
    for name in json.loads(os.environ.get('FT_SERIAL_FIXTURES', '[]')):
        config.addinivalue_line('thread_unsafe_fixtures', name)
    random.seed(int(os.environ['FT_SEED']))
    gil('plugins')


def pytest_collection_modifyitems(config, items):
    shared_names = set(json.loads(os.environ.get('FT_SERIAL_GLOBALS', '[]')))
    if shared_names:
        for item in items:
            code = getattr(getattr(item, 'obj', None), '__code__', None)
            if code is not None and shared_names.intersection(code.co_names):
                item.add_marker(pytest.mark.thread_unsafe(reason='shares an upstream output filename between test copies'))
    path = os.environ.get('FT_SELECTION')
    if path:
        with open(path, encoding='utf-8') as stream:
            selected = set(json.load(stream))
        deselected = [item for item in items if item.nodeid not in selected]
        items[:] = [item for item in items if item.nodeid in selected]
        config.hook.pytest_deselected(items=deselected)


@pytest.hookimpl(trylast=True)
def pytest_collection_finish(session):
    gil('collection')
    for item in session.items:
        emit({'collected': item.nodeid})


@pytest.hookimpl(trylast=True)
def pytest_runtest_setup(item):
    gil('setup')
    emit({'active': item.nodeid})


def pytest_collectreport(report):
    if report.skipped or report.failed:
        emit({'collection_node': report.nodeid, 'collection_status': 'SKIP' if report.skipped else 'ERROR',
              'reason': str(report.longrepr)})


def pytest_runtest_logreport(report):
    if report.failed:
        status = 'XPASS' if str(report.longrepr).startswith('[XPASS(strict)]') else ('FAIL' if report.when == 'call' else 'ERROR')
    elif report.skipped:
        status = 'XFAIL' if hasattr(report, 'wasxfail') else 'SKIP'
    elif report.when == 'call':
        status = 'XPASS' if hasattr(report, 'wasxfail') else 'PASS'
    else:
        return
    emit({'id': report.nodeid, 'status': status, 'seconds': report.duration,
          'when': report.when, 'reason': str(report.longrepr) if status != 'PASS' else ''})


def pytest_sessionfinish(session, exitstatus):
    gil('completion')
    emit({'exitstatus': int(exitstatus)})
    for plugin in session.config.pluginmanager.get_plugins():
        if hasattr(plugin, 'run_in_parallel') and hasattr(plugin, 'thread_unsafe'):
            emit({'parallelism': plugin.run_in_parallel, 'serial_fallback': plugin.thread_unsafe})
