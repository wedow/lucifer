"""Execute one program, optionally writing its explicit STATE graph."""
import json
import runpy
import sys

from support import graph


program, count, state_path = sys.argv[1:]
sys.argv = [program, count]
try:
    namespace = runpy.run_path(program, run_name="__main__")
finally:
    with open("capabilities.json", "w", encoding="utf-8") as stream:
        json.dump({"gil": getattr(sys, "_is_gil_enabled", lambda: None)()}, stream)
if state_path != "-":
    state = graph(namespace["STATE"])
    with open(state_path, "w", encoding="utf-8", newline="\n") as stream:
        json.dump(state, stream, ensure_ascii=True, sort_keys=True)
        stream.write("\n")
