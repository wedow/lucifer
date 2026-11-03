class Broken(Exception):
    def __str__(self):
        raise ValueError('inside str')


raise Broken('x')
