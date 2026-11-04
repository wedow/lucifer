import tracemalloc

print(tracemalloc.is_tracing(), tracemalloc.get_traceback_limit(), tracemalloc.get_traced_memory())
tracemalloc.start(3)
print(tracemalloc.is_tracing(), tracemalloc.get_traceback_limit())
tracemalloc.stop()
print(tracemalloc.is_tracing(), tracemalloc.get_traceback_limit(), tracemalloc.get_object_traceback(object()))
try:
    tracemalloc.start(0)
except ValueError as e:
    print(e)
print(tracemalloc.take_snapshot if False else 'ok')
