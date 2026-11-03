class B:
    def __hash__(self):
        raise ValueError('nope')
{B(): 1}
