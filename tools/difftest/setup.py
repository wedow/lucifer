"""Prepare the separately pinned, free-threaded oracle generator environment."""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle", type=Path, required=True)
    parser.add_argument("--venv", type=Path, required=True)
    args = parser.parse_args()
    oracle, venv = args.oracle.resolve(), args.venv.resolve()
    info = json.loads(subprocess.check_output([str(oracle), "-c",
        "import json,sys,sysconfig; print(json.dumps([list(sys.version_info[:3]),"
        "sys.implementation.name, bool(sysconfig.get_config_var('Py_GIL_DISABLED')),"
        "sys._is_gil_enabled()]))"], timeout=15))
    if info != [[3, 14, 10], "cpython", True, False]:
        parser.error("requires the CPython v3.14.10 free-threaded oracle")
    if venv.exists():
        parser.error("venv already exists; choose a new directory")
    subprocess.run([str(oracle), "-m", "venv", str(venv)], check=True, timeout=120)
    subprocess.run([str(venv / "bin/python"), "-m", "pip", "install", "--require-hashes",
                    "--only-binary=:all:", "--no-deps", "-r",
                    str(Path(__file__).with_name("requirements.lock"))], check=True, timeout=180)


if __name__ == "__main__":
    main()
