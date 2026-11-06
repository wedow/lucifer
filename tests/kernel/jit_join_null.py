# A call whose argument has a join in it, as in `f(x or "/", 303)`: the
# NULL the call pushed beside its callable reaches the join from both
# paths, and stays a NULL entry there, not a self argument. The callee is
# inlined, and its first parameter must be the argument, both when it runs
# to the end in compiled code and when it leaves at an instruction that
# runs Python (the truth of an object with __bool__).
class Proxy:
    def __bool__(self):
        return True


current = Proxy()


def plain(location, code=302, response=None):
    return (location, code, response)


def leaves(location, code=302, response=None):
    if current:
        return (location, code, response)
    return None


def view(referrer):
    return plain(referrer or "/", 303)


def view_leaving(referrer):
    return leaves(referrer or "/", 303)


def main():
    seen = {}
    for i in range(3000):
        referrer = "/a" if i % 2 else ""
        for r in (view(referrer), view_leaving(referrer)):
            seen[r] = seen.get(r, 0) + 1
    for r in sorted(seen, key=repr):
        print(r, seen[r])


main()
