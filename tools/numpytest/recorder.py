"""Small pytest adapter, imported only by the interpreter being tested."""

import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import sysconfig

import pytest


def pytest_configure(config):
    # NumPy sees the installed parallel plugin even in the serial lane, where
    # auto-loading is disabled. Preserve its marks without enabling wrapping.
    for marker in ("thread_unsafe", "parallel_threads(n)", "parallel_threads_limit(n)", "iterations(n)"):
        config.addinivalue_line("markers", marker + ": upstream concurrency annotation")


def nodeid(item):
    # Some typing parametrizations contain absolute installed data filenames.
    # Only replace the known installation root, including inside parameters.
    return item.nodeid.replace(os.environ["NUMPYTEST_SITE"] + "/", "")


def pytest_collection_modifyitems(config, items):
    selected = []
    deselected = []
    shards = json.loads(os.environ.get("NUMPYTEST_SHARDS", '[""]'))
    skip = Path(os.environ["NUMPYTEST_SKIP"]) if "NUMPYTEST_SKIP" in os.environ else None
    done = set(skip.read_text().splitlines()) if skip and skip.exists() else set()
    for item in items:
        ident = nodeid(item)
        if not any((ident == shard or ident.startswith(shard + "::") or ident.startswith(shard + "["))
                   if "::" in shard else ident.split("::", 1)[0].startswith(shard)
                   for shard in shards):
            deselected.append(item)
            continue
        item.user_properties.append(("nodeid64", base64.b64encode(ident.encode()).decode()))
        selected.append(item)
    # The collection record is the whole selection; a restart runs only the rest.
    Path(os.environ["NUMPYTEST_COLLECTION"]).write_text(
        json.dumps(sorted(nodeid(item) for item in selected)) + "\n"
    )
    deselected += [item for item in selected if nodeid(item) in done]
    items[:] = [item for item in selected if nodeid(item) not in done]
    config.hook.pytest_deselected(items=deselected)


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    report = (yield).get_result()
    # JUnit represents non-strict XPASS as an ordinary successful testcase.
    if hasattr(report, "wasxfail") and report.passed:
        item.user_properties.append(("outcome", "XPASS"))
    journal = os.environ.get("NUMPYTEST_REPORTS")
    if journal:
        crash = getattr(report.longrepr, "reprcrash", None)
        with open(journal, "a") as stream:
            stream.write(json.dumps({"id": nodeid(item), "when": report.when,
                                     "outcome": report.outcome, "seconds": report.duration,
                                     "wasxfail": hasattr(report, "wasxfail"),
                                     "reason": crash.message.splitlines()[0] if crash and crash.message else ""}) + "\n")


def relocate_typing_inputs(module, work):
    """Keep installed dependency modules out of mypy's source search root."""
    original = Path(module.DATA_DIR)
    copied = work / 'typing-inputs'
    shutil.rmtree(copied, ignore_errors=True)       # a restarted shard copies again
    shutil.copytree(original, copied, ignore=shutil.ignore_patterns('__pycache__', '.mypy_cache'))
    digest = hashlib.sha256()
    files = 0
    for path in sorted(copied.rglob('*')):
        if path.is_file():
            digest.update(path.relative_to(copied).as_posix().encode() + b'\0')
            digest.update(path.read_bytes())
            files += 1
    run = module.api.run

    def checked(args):
        out, err, code = run([arg.replace(str(original), str(copied)) for arg in args])
        # Upstream assertions and collected parameters keep their original paths.
        return out.replace(str(copied), str(original)), err.replace(str(copied), str(original)), code

    module.api.run = checked
    module._numpytest_relocated = True
    return dict(files=files, sha256=digest.hexdigest(), layout='external copy of installed typing data')


@pytest.hookimpl(tryfirst=True)
def pytest_runtest_setup(item):
    if (nodeid(item).split('::', 1)[0] == 'numpy/typing/tests/test_typing.py'
            and hasattr(item.module, 'api') and not getattr(item.module, '_numpytest_relocated', False)):
        item.config._numpytest_typing_inputs = relocate_typing_inputs(
            item.module, Path(os.environ['NUMPYTEST_COLLECTION']).parent)
    active = os.environ.get("NUMPYTEST_ACTIVE")
    if active:
        Path(active).write_text(nodeid(item))


def pytest_sessionfinish(session, exitstatus):
    # Check after collection AND after test imports; never force PYTHON_GIL=0.
    state = {
        "version": sys.version.split()[0],
        "abiflags": sys.abiflags,
        "free_threaded_build": bool(sysconfig.get_config_var("Py_GIL_DISABLED")),
        "gil_enabled": sys._is_gil_enabled() if hasattr(sys, "_is_gil_enabled") else None,
        "exitstatus": int(exitstatus),
    }
    if hasattr(session.config, '_numpytest_typing_inputs'):
        state['typing_inputs'] = session.config._numpytest_typing_inputs
    parallel = session.config.pluginmanager.get_plugin("_run-parallel")
    if parallel is not None:
        state["parallel_cases"] = len(parallel.run_in_parallel)
        state["parallel_ids"] = sorted(i.replace(os.environ["NUMPYTEST_SITE"] + "/", "") for i in parallel.run_in_parallel)
        state["serial_fallback"] = {
            ident.replace(os.environ["NUMPYTEST_SITE"] + "/", ""): reason
            for ident, reason in parallel.thread_unsafe.items()
        }
    Path(os.environ["NUMPYTEST_STATE"]).write_text(json.dumps(state) + "\n")
