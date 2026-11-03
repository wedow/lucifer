class A:
    def f(self):
        return super().nope


A().f()
