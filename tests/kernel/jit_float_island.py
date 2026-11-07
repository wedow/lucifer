# Float arithmetic folded from an operator's method (int / float runs
# float.__truediv__'s division in line) leaves, when its guard fails, at
# the operator with its operands on the stack, never inside the method:
# the interpreter would resume after the operator with them still there.
# Each child fails the Nth guard of every entry (-X lucifer_stress=deopt:N,
# which other runtimes ignore), so some N fails the division's guard.
import subprocess
import sys

CHILD = """
def h(x):
    return x * 2


def f(t, d):
    return h(t / d), h(t / 1000.0)


acc = 0.0
for i in range(1, 400):
    a, b = f(i, 8.0)
    acc += a + b
print(acc)
"""

for n in range(1, 13):
    p = subprocess.run([sys.executable, '-X', 'lucifer_jit=always', '-X', f'lucifer_stress=deopt:{n}', '-c', CHILD],
                       capture_output=True, text=True)
    print(n, p.returncode, p.stdout.strip(), p.stderr.strip().splitlines()[-1:] if p.returncode else '')
