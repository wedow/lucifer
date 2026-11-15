# Freed compiled code is reused, and a forked child has code of its own:
# the parent compiles functions it keeps using, forks, and then parent and
# child each compile, kill and collect many more units (whose ranges the
# freed ones reuse) while calling the functions compiled before the fork.
import gc
import os
import types

K = 1


def kept(n):
    total = 0
    for i in range(n):
        total += i * 3 + (i & 1)
    return total


def template(x):
    return K + x


def churn(rounds):
    global K
    total = 0
    for r in range(rounds):
        f = types.FunctionType(template.__code__.replace(co_name=f"f{r}"), globals())
        for i in range(60):
            total += f(i)
        K = 2 if K == 1 else 1
        total += f(1) + kept(50)
        if r % 10 == 9:
            gc.collect()
    return total


def main():
    for _ in range(60):
        kept(50)
    read, write = os.pipe()
    pid = os.fork()
    result = churn(300)
    if pid == 0:
        os.write(write, repr(result).encode())
        os._exit(0)
    os.close(write)
    child = os.read(read, 100).decode()
    os.waitpid(pid, 0)
    print(result, child, kept(50))


main()
