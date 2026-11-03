inner = ExceptionGroup('inner', [LookupError('k'), ValueError('v')])
outer = ExceptionGroup('outer', [TypeError('t'), inner])
raise outer
