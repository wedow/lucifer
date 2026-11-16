# The function exec and eval run their code in is freed with its frame, as
# CPython's is, not by a collection: the namespaces it held go at once,
# and a hot eval leaves no garbage behind for the collector.
import gc


class Noisy:
    def __init__(self, name):
        self.name = name

    def __del__(self):
        print('freed', self.name)


code = compile("x", '<e>', 'eval')
gc.disable()
gc.collect()
print(eval(code, {'x': 1, 'n': Noisy('eval globals')}))
print('after eval')
exec("y = 1", {'z': Noisy('exec globals')})
print('after exec')
print(eval("v", {}, {'v': 3}))
stmt = compile("y = x", '<e>', 'exec')
gc.collect()
for i in range(2000):
    eval(code, {'x': i})
    exec(stmt, {'x': i}, {})
print('little garbage', gc.collect() < 100)
