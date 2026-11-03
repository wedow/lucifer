class Guard:
    def __enter__(self):
        return self

    def __exit__(self, kind, value, tb):
        print('exit sees', kind.__name__)
        return False


with Guard():
    raise LookupError('inside with')
