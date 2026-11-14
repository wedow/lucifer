# Units that die are freed: each round makes a function of its own code
# (kept alive, so its units are never freed with their code), calls it
# until it is compiled, and rebinds the global it relies on, which kills
# its unit. A collection runs every few rounds. tools/jitcheck runs this
# at two sizes and requires that the dead units still held (the
# -X lucifer_jit_stats report's) do not grow with the rounds.
import gc
import sys
import types

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
K = 1


def template(x):
    return K + x


def main(n):
    global K
    kept = []
    total = 0
    for r in range(n // 10):
        f = types.FunctionType(template.__code__.replace(co_name=f"f{r}"), globals())
        kept.append(f)
        for i in range(60):
            total += f(i)
        K = 2 if K == 1 else 1
        total += f(1)
        if r % 10 == 9:
            gc.collect()
    return total, len(kept)


print(main(N))
