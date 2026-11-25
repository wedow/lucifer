# A frame evaluation hook set and cleared while a loop of a function called
# once runs: the loop is compiled again afterwards, so its unit's polls grow
# with the iterations (read from -X lucifer_jit_stats in a child process;
# the oracle has no tier and prints the same line).
import os
import re
import subprocess
import sys

if len(sys.argv) > 1:
    import hook_toggle

    def main(n):
        s = 0
        for i in range(n):
            if i == 50:
                hook_toggle.toggle()
            s += i & 7
        return s

    print(main(int(sys.argv[1])))
    sys.exit(0)

if sys.implementation.name != 'lucifer':
    print('compiled after the hook')
    sys.exit(0)


def polls(n):
    out = subprocess.run([sys.executable, '-X', 'lucifer_jit_stats', __file__, str(n)],
                         capture_output=True, text=True, env=os.environ)
    found = re.search(r'jit: executed:.* poll (\d+)', out.stderr)
    return int(found[1]) if found else 0


small, large = polls(20000), polls(40000)
print('compiled after the hook' if large - small >= 10000 else f'interpreted after the hook: polls {small}, {large}')
