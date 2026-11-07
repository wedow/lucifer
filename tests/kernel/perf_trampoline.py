# Stack trampolines for perf (-X perf, sys.activate_stack_trampoline): each Python function that
# runs gets a py::QUALNAME:FILENAME symbol in /tmp/perf-PID.map, at the address of its own native
# trampoline; a forked child starts its own map; perf_jit is a backend too. The children run with
# the default tiers: compiled code inlines callees, which then have no trampoline of their own.
import os
import string
import subprocess
import sys
import tempfile

def run(source, *flags):
    with tempfile.TemporaryDirectory() as d:
        script = os.path.join(d, "perftest.py")
        with open(script, "w") as f:
            f.write(source)
        p = subprocess.run([sys.executable, *flags, script], capture_output=True, text=True,
                           env=dict(os.environ, PYTHON_JIT="0"))
        return script, p


def symbols(pid, script):
    path = f"/tmp/perf-{pid}.map"
    if not os.path.exists(path):
        return None
    found = {}
    with open(path) as f:
        lines = f.read().splitlines()
    os.unlink(path)
    for line in lines:
        addr, size, name = line.split(" ", 2)
        assert set(addr) <= set(string.hexdigits) and not addr.startswith("0x"), line
        if name.startswith("py::") and name.endswith(":" + script):
            found[name[4:-len(script) - 1]] = True
    return sorted(found)


CALLS = """
def foo():
    total = 0
    for i in range(2000):
        total += i
    return total

def bar():
    return foo()

def baz():
    return bar()

print(baz())
"""

script, p = run(CALLS, "-Xperf")
print("-X perf:", p.returncode, repr(p.stdout), repr(p.stderr))
script, p = run("import sys; print(sys.is_stack_trampoline_active(), sys.argv[0] != '')\n" + CALLS, "-Xperf")
print("-X perf active:", p.stdout.split()[:2])

API = """
import os, sys

def foo():
    pass

def spam():
    pass

def bar():
    sys.deactivate_stack_trampoline()
    foo()
    sys.activate_stack_trampoline("perf")
    spam()

def baz():
    bar()

print(sys.is_stack_trampoline_active())
sys.activate_stack_trampoline("perf")
print(sys.is_stack_trampoline_active())
baz()
print(os.getpid())
"""
script, p = run(API)
out = p.stdout.split()
print("api:", p.returncode, out[:2], repr(p.stderr), symbols(int(out[2]), script))

script, p = run(CALLS.replace("print(baz())", "import os; baz(); print(os.getpid())"), "-Xperf")
print("map:", p.returncode, symbols(int(p.stdout), script))

FORK = """
import os, sys

def foo_fork():
    pass

def bar_fork():
    foo_fork()

def baz_fork():
    bar_fork()

def foo():
    pid = os.fork()
    if pid == 0:
        print(os.getpid(), flush=True)
        baz_fork()
        os._exit(0)
    os.waitpid(pid, 0)
    print(os.getpid())

def bar():
    foo()

def baz():
    bar()

baz()
"""
script, p = run(FORK, "-Xperf")
child, parent = map(int, p.stdout.split())
print("fork parent:", symbols(parent, script))
print("fork child:", symbols(child, script))

for backend in ("perf", "perf_jit", "invalid"):
    _, p = run(f"""
import os, sys
print(os.getpid())
sys.activate_stack_trampoline({backend!r})
sys.activate_stack_trampoline({backend!r})
print(sys.is_stack_trampoline_active())
sys.deactivate_stack_trampoline()
print(sys.is_stack_trampoline_active())
""")
    pid, *out = p.stdout.split()
    for path in (f"/tmp/perf-{pid}.map", f"/tmp/jit-{pid}.dump"):
        if os.path.exists(path):
            os.unlink(path)
    print(backend, p.returncode, out, p.stderr.strip().splitlines()[-1:])

# A name with no UTF-8 form: its frame still runs, and the exception the encoding raised is not seen.
# A function after it has a name, so that the map exists whatever else got a symbol.
_, p = run('''
import os, sys
sys.activate_stack_trampoline("perf")
eval(compile("x = 1", "bad\\ud800file", "exec"))
def named():
    pass
named()
print(os.getpid())
''')
os.unlink(f"/tmp/perf-{int(p.stdout)}.map")
print("unencodable:", p.returncode, repr(p.stderr))
