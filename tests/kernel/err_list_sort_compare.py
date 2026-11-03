class Bad:
    def __lt__(self, other):
        raise RuntimeError('cannot compare')


sorted([Bad(), Bad()])
