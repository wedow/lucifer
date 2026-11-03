def one():
    raise LookupError('one')


def two():
    try:
        one()
    except LookupError:
        raise ValueError('two')


def three():
    try:
        two()
    except ValueError as e:
        raise RuntimeError('three') from e


three()
