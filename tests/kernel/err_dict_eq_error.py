class B:
    def __hash__(self): return 1
    def __eq__(self, other): raise KeyError('eq')
d = {B(): 1}
d[B()]
