# isinstance and issubclass against classes the code names, in a hot
# loop: an instance of one class, of three, and of many subclasses of
# builtins (whose answer is yes), each against a class or a tuple of
# classes; issubclass of a constant class and of a class the site sees
# several of. None of them needs the tuple or a call once compiled.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class A:
    pass


class B(A):
    pass


class C:
    pass


class D(C):
    pass


class S1(str):
    pass


class S2(str):
    pass


class I1(int):
    pass


class I2(int):
    pass


def run(n):
    b = B()
    three = [B(), C(), D()]
    many = ['s', S1('a'), S2('b'), 3, True, I1(4), I2(5)]
    s = 0
    for i in range(n):
        x = three[i % 3]
        y = many[i % 7]
        if isinstance(b, A):
            s += 1
        if isinstance(b, (C, B)):
            s += 2
        if isinstance(x, A):
            s += 3
        if isinstance(x, (C, str)):
            s += 4
        if isinstance(y, (int, str)):
            s += 5
        if issubclass(B, A):
            s += 6
        if issubclass(type(y), (int, str)):
            s += 7
        if issubclass(type(x), (dict, list)):
            s += 8
    return s


print(run(N))
