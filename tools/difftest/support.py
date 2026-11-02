"""Target-side helpers; no third-party imports or implementation internals."""
import json
import sys


def loops():
    return int(sys.argv[1])


def awfy(name, inner):
    """Use the unchanged upstream answer verifier, including special workloads."""
    import os
    import importlib.util
    from pathlib import Path
    from time import perf_counter_ns

    directory = Path(os.environ["DIFFTEST_ROOT"]) / "third_party/awfy/Python"
    sys.path.insert(0, str(directory))
    # AWFY's json.py and the state transport's stdlib json must coexist.
    spec = importlib.util.spec_from_file_location("difftest_awfy_" + name, directory / (name.lower() + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    benchmark = getattr(module, name)()
    started = perf_counter_ns()
    for _ in range(loops()):
        if benchmark.inner_benchmark_loop(inner) is not True:
            raise AssertionError(name + " failed upstream verification")
    print(name + ': verified')
    print('DIFFTEST_TIME=' + str(perf_counter_ns() - started) + 'ns')
    return {"name": name, "inner": inner, "iterations": loops(), "verified": True}


def graph(value):
    """Encode types, exact floats, dictionary order, cycles and container aliases."""
    seen = {}
    nodes = []

    def atom(item):
        kind = type(item)
        if kind is type(None):
            return ["none"]
        if kind in (bool, int, str):
            return [kind.__name__, item]
        if kind is float:
            return ["float", item.hex()]
        if kind is bytes:
            return ["bytes", item.hex()]
        raise TypeError("unordered state members must be scalar values")

    def visit(item):
        kind = type(item)
        if kind not in (list, tuple, dict, set, frozenset, bytearray):
            return atom(item)
        identity = id(item)
        if identity in seen:
            return ["ref", seen[identity]]
        index = len(nodes)
        seen[identity] = index
        node = {"id": index, "type": kind.__name__}
        nodes.append(node)
        if kind is dict:
            node["items"] = [[visit(k), visit(v)] for k, v in item.items()]
        elif kind in (set, frozenset):
            node["items"] = sorted((atom(v) for v in item), key=lambda v: json.dumps(v, ensure_ascii=True))
        elif kind is bytearray:
            node["hex"] = item.hex()
        else:
            node["items"] = [visit(v) for v in item]
        return ["ref", index]

    root = visit(value)
    return {"root": root, "nodes": nodes}
