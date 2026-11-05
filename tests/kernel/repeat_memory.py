import resource

# A repeat allocates its result once: the peak grows by about the result's size.
a = object()
for make in (lambda n: (a,) * n, lambda n: [a] * n):
    before = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    r = make(2 ** 25)                       # 256 MiB of references
    grown = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss - before
    print(len(r), grown < 384 * 1024)
    del r
