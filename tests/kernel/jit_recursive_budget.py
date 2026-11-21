# A recursive descent with work on both sides of the recursive call.
# The inliner needs room for the calls that finish the current level.
import sys


def finish(value, key):
    return (value ^ key) % 1009


# Repeat straight-line work so one level fits while two copies use up the
# inline budget. Each level finishes by mixing its own keys into the result.
exec("def descend(depth, key):\n"
     + "    key = (key * 23 + 43) % 1009\n" * 150
     + """\
    a = (key * 3 + 17) % 1009
    b = (key * 7 - 23) % 1009
    c = (key * 11 + 29) % 1009
    d = (key * 13 - 31) % 1009
    e = (key * 17 + 37) % 1009
    f = (key * 19 - 41) % 1009
    if depth:
        key = descend(depth - 1, a ^ b ^ c ^ d ^ e ^ f)
    key = finish(key, a)
    key = finish(key, b)
    key = finish(key, c)
    key = finish(key, d)
    key = finish(key, e)
    return finish(key, f)
""")


def run(n):
    total = 0
    for key in range(n):
        total += descend(1, key)
    return total


# Keep the loop below the point where its outlined calls trigger a rebuild.
print(run(int(sys.argv[1]) // 8 if len(sys.argv) > 1 else 3000))
