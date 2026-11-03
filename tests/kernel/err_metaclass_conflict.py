class M1(type):
    pass


class M2(type):
    pass


class A(metaclass=M1):
    pass


class B(metaclass=M2):
    pass


class C(A, B):
    pass
