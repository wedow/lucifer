# A loop that unpacks its argument at a hundred sites, called with a tuple
# and then with a list, by turns (as pyperformance's unpack_sequence does).
# The tuple's type guard moves out of the loop, standing for every site;
# when it fails for the list, the code must recompile once into a unit
# that takes both, not learn one site per recompile while the loop runs in
# the interpreter. tools/jitcheck runs this at two sizes and requires that
# entries left to the interpreter and side exits do not grow with the
# iterations.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000

exec("def unpack(n, seq):\n"
     "    s = 0\n"
     "    for _ in range(n):\n"
     + "        a, b, c, d = seq\n" * 100 +
     "        s += a + d\n"
     "    return s\n")

t, l = (1, 2, 3, 4), [5, 6, 7, 8]
total = 0
for _ in range(N // 100):
    total += unpack(50, t) + unpack(50, l)
print(total)
