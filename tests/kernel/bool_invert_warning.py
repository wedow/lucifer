import warnings

with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter('always')
    print(~True, ~False, ~5)
print([(w.category.__name__, str(w.message)[:40], w.filename.split('/')[-1], w.lineno) for w in caught])
