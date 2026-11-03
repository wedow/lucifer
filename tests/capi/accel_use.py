"""What the stdlib does once the three accelerators import: heapq, bisect and
statistics bind the C functions, and statistics computes through them."""
import bisect, heapq, statistics, _bisect, _heapq, _statistics

for mod, c in ((bisect, _bisect), (heapq, _heapq)):
    print(mod.__name__, sorted(n for n in dir(c) if not n.startswith('__') and getattr(mod, n) is getattr(c, n)))
print(statistics._normal_dist_inv_cdf is _statistics._normal_dist_inv_cdf)

h = [5, 2, 8, 1, 9, 3]
heapq.heapify(h)
print([heapq.heappop(h) for _ in range(len(h))])
print(heapq.nlargest(3, [4, 1, 7, 3, 8]), heapq.nsmallest(2, [4, 1, 7, 3, 8], key=lambda x: -x))
print(list(heapq.merge([1, 4, 7], [2, 5], [3, 6, 9])))
a = [1, 2, 4, 4, 8]
print(bisect.bisect(a, 4), bisect.bisect_left(a, 4))
bisect.insort(a, 5)
bisect.insort_left(a, 4, key=lambda x: x)
print(a)

print(statistics.mean([1, 2, 3.5]), statistics.median([3, 1, 2]), statistics.stdev([1.5, 2.5, 3.5, 7]))
n = statistics.NormalDist(10, 2.5)
print(n.inv_cdf(0.9), n.cdf(12), n.mean, n.stdev, n.variance)
print(statistics.quantiles([1, 2, 3, 4, 5, 6, 7, 8, 9], n=4))
print(statistics.fmean([1, 2, 3]), statistics.mode([1, 1, 2]), statistics.multimode([1, 1, 2, 2]))
