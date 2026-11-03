class A(Exception):
    pass


def fail(n):
    raise A(n)


def run():
    errors = []
    for n in ('a', 'b'):
        try:
            fail(n)
        except A as e:
            errors = [*errors, e]
    raise ExceptionGroup('several', errors)


run()
