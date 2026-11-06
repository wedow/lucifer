# Calls the optimizing tier should inline although the callee is large: a
# dispatch over many operations, most of which the program never asks for,
# so the code a call runs is a small part of it. The branches the
# interpreter never took are not compiled; when one is taken later, compiled
# code leaves there, and the results stay the interpreter's.
# tools/jitcheck also runs this at two sizes and requires that entries into
# compiled code and direct calls do not grow with the iterations.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def dispatch(op, x):
    if op == 0:
        return x + 1
    elif op == 1:
        return x * 3 - 1
    elif op == 2:
        return (x * 7 + 3) // 2 - x % 5
    elif op == 3:
        return (x - 11) * (x + 13) % 1009
    elif op == 4:
        return (x << 3) ^ (x >> 2) | 17
    elif op == 5:
        return (x * x + 2 * x + 1) % 997 - (x & 255)
    elif op == 6:
        return -x + (x // 3) * 4 - (x % 7) * 2
    elif op == 7:
        return (x + 1) * (x + 2) * (x + 3) % 10007
    elif op == 8:
        return x * 5 + (x ^ 1234) - (x | 4321) + (x & 99)
    elif op == 9:
        return (x * 31 + 17) % 65521 + (x // 11) - (x % 13)
    elif op == 10:
        return (x - 3) * (x - 5) * (x - 7) // 9 + x % 4
    elif op == 11:
        return (x ^ (x << 1)) & 0xFFFF | (x % 3) + (x // 5)
    elif op == 12:
        return (x * 9 - 4) * (x + 8) % 4093 - (x >> 3) + 2
    elif op == 13:
        return x * x * x % 7919 + (x * 3 ^ 77) - (x // 2)
    elif op == 14:
        return ((x + 5) // 2) * ((x + 6) // 3) % 1201 + x % 9
    elif op == 15:
        return (x | 0x55) - (x & 0xAA) + (x ^ 0x0F) * 2 - x // 7
    elif op == 16:
        return (x * 13 + 5) * (x * 17 + 3) % 30011 - (x % 19)
    elif op == 17:
        return (x - 100) * (x - 200) // 37 + (x % 23) * (x % 29)
    elif op == 18:
        return (x << 2) - (x >> 1) + (x * 11 % 101) - (x // 13)
    elif op == 19:
        return (x * 41 + 7) % 8191 + (x * 43 + 9) % 4099 - x % 31
    elif op == 20:
        return (x + 3) * (x + 9) * (x + 27) % 50021 - (x >> 4) + 1
    elif op == 21:
        return (x ^ 0x3C3C) + (x | 0x0F0F) - (x & 0xF0F0) + x // 17
    elif op == 22:
        return (x * 6 - 5) * (x * 4 + 7) % 12007 - (x % 37) * 3
    elif op == 23:
        return (x * 53 + 29) % 60013 + (x // 19) * (x % 41) - 6
    return -1


def run(op, n):
    s = 0
    for i in range(n):
        s += dispatch(op, i)
    return s


def main():
    print(run(1, N))
    # Branches compiled code never took: each leaves, and enough of them recompile.
    for op in (0, 2, 7, 13, 23, 99):
        print(op, run(op, 300))
    print(run(1, N))
    print(sum(run(op, 60) for op in range(25)))


main()
