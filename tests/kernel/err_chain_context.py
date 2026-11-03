def handler():
    try:
        raise LookupError('first')
    except LookupError:
        raise ValueError('second')


handler()
