"""Target-side correctness preflight for the frozen pyperformance panel.

No changes to upstream timed algorithms. These assertions supplement, rather
than replace, their embedded checks. This is not Python conformance coverage.
"""
import asyncio
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import subprocess
import sys
import sysconfig


def load(root, name):
    spec = importlib.util.spec_from_file_location('bench_check_' + name,
                                                 root / ('bm_' + name) / 'run_benchmark.py')
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def check(root, name):
    module = load(root, name)
    if name == 'comprehensions':
        widgets = module.make_some_widgets()
        tray = module.WidgetTray(1, widgets)
        ids = [widget.widget_id for widget in tray.sorted_widgets]
        assert ids == [1, 3, 4, 5, 6, 17, 7, 19, 20, 21, 22, 23, 9, 11, 12, 13, 14, 15]
        module.bench_comprehensions(1)
    elif name == 'regex_dna':
        seq = module.init_benchmarks(100000, module.DEFAULT_RNG_SEED)
        assert module.run_benchmarks(seq) == ([6, 26, 86, 58, 113, 31, 31, 32, 43],
                                              1016745, 1000000, 1336326)
    elif name == 'float':
        for i in range(1000):
            point = module.Point(i)
            point.normalize()
            assert math.isclose(point.x**2 + point.y**2 + point.z**2, 1, abs_tol=1e-12)
        points = [module.Point(i) for i in range(1000)]
        expected = tuple(max(getattr(point, attr) for point in points) for attr in ('x', 'y', 'z'))
        maximum = module.maximize(points)
        assert (maximum.x, maximum.y, maximum.z) == expected
        maximum = module.benchmark(module.POINTS)
        # Full-size answer from the pinned CPython v3.14.10 oracle. Allow only
        # libm-scale variation, separately from the coordinate/norm invariants.
        expected = (0.8944271890997864, 1.0, 0.4472135954456972)
        assert all(math.isclose(getattr(maximum, attr), value, rel_tol=1e-12, abs_tol=1e-12)
                   for attr, value in zip(('x', 'y', 'z'), expected))
    elif name == 'richards':
        assert module.Richards().run(1) is True
    elif name == 'generators':
        assert list(module.tree(range(100000))) == list(range(100000))
    elif name == 'async_generators':
        async def collect():
            return [item async for item in module.tree(range(100000))]
        assert asyncio.run(collect()) == list(range(100000))
    elif name == 'json_loads':
        for obj in (module.DICT, list(module.TUPLE), module.DICT_GROUP):
            assert json.loads(json.dumps(obj)) == obj
        module.bench_json_loads(tuple(json.dumps(obj) for obj in
                                     (module.DICT, module.TUPLE, module.DICT_GROUP)))
    elif name == 'python_startup':
        child = subprocess.run([sys.executable, '-c', 'print(6 * 7)'], capture_output=True,
                               timeout=15)
        assert child.returncode == 0 and child.stdout == b'42\n' and child.stderr == b''
    elif name == 'docutils':
        files = sorted(module.DOC_ROOT.rglob('*.txt'))
        assert len(files) == 49, 'changed or empty document corpus'
        digest = hashlib.sha256()
        # Same inputs and writer settings as upstream build_html; all 49 render
        # successfully on the pinned CPython 3.14.10 / Docutils 0.18.1 oracle.
        for path in files:
            output = module.core.publish_string(source=path.read_text(encoding='utf-8'),
                reader_name='standalone', parser_name='restructuredtext', writer_name='html5',
                settings_overrides={'input_encoding': 'unicode', 'output_encoding': 'unicode',
                                    'report_level': 5})
            digest.update(path.relative_to(module.DOC_ROOT).as_posix().encode() + b'\0'
                          + output.encode() + b'\0')
        actual = digest.hexdigest()
        assert actual == '408bb0f9d1b618f7878d2c4f3444e6eff771467b998b74442a012a9132292f30', actual
    else:
        raise AssertionError('undeclared benchmark')


if __name__ == '__main__':
    check(Path(sys.argv[1]), sys.argv[2])
    assert not sysconfig.get_config_var('Py_GIL_DISABLED') or not sys._is_gil_enabled()
    print('correctness: PASS')
