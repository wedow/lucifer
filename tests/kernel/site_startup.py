# Site runs before user code and before the working directory enters sys.path.
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory


PROBE = """
import builtins
import sys

print("site" in sys.modules, sys.flags.no_site, sys.flags.no_user_site)
print([name for name in ("exit", "quit", "copyright", "credits", "license", "help")
       if hasattr(builtins, name)])
print(globals().get("pth_marker"), "sitecustomize" in sys.modules, sys.path[0] == "")
if hasattr(builtins, "exit"):
    exit(100)
"""

lane = ["-X" + arg for arg in sys.orig_argv
        if arg.startswith(("lucifer_jit=", "lucifer_stress="))]
with TemporaryDirectory(dir=os.getcwd()) as tmp:
    root = Path(tmp)
    version = f"python{sys.version_info.major}.{sys.version_info.minor}{sys.abiflags}"
    packages = root / "lib" / version / "site-packages"
    packages.mkdir(parents=True)
    pth = packages / "startup.pth"
    pth.write_text("import __main__; __main__.pth_marker = 'from .pth'\n", encoding="utf-8")
    (root / "sitecustomize.py").write_text("print('working directory hook')\n", encoding="utf-8")
    env = {"PYTHONUSERBASE": str(root)}
    for flags in ([], ["-S"], ["-I"], ["-I", "-S"]):
        result = subprocess.run(
            [sys.executable, *flags, *lane, "-B", "-c", PROBE],
            cwd=root, env=env, capture_output=True, text=True)
        print(flags, result.returncode)
        print(result.stdout, end="")
        if result.stderr:
            print(result.stderr, end="")

    pth.write_text("import sys; sys.exit('startup hook failed')\n", encoding="utf-8")
    result = subprocess.run([sys.executable, *lane, "-B", "-c", "print('user code')"],
                            cwd=root, env=env, capture_output=True, text=True)
    print("startup error", result.returncode, repr(result.stdout))
    print(result.stderr, end="")
