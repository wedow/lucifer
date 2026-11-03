try:
    raise KeyError('hidden')
except KeyError:
    raise ValueError('shown') from None
