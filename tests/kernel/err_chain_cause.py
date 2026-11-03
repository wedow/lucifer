class Wrapped(Exception):
    pass


def load():
    try:
        raise OSError('disk')
    except OSError as e:
        raise Wrapped('could not load') from e


load()
