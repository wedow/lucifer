# Each thread has its own current context: a raw thread starts with none,
# whatever thread ran before it, and Context.run switches and restores it.
import _thread
import contextvars

v = contextvars.ContextVar('v', default='unset')
v.set('main')
out = []
for i in range(8):
    def work(i=i):
        out.append(v.get())
        v.set(i)
        out.append(contextvars.copy_context()[v])
    _thread.start_joinable_thread(work).join()
print(out)
print(v.get())

# run() switches to its context and back, on return and on raise, nested.
a, b = contextvars.Context(), contextvars.copy_context()
def inner():
    v.set('in a')
    return b.run(lambda: (v.get(), v.set('in b'), v.get())[::2]), v.get()
print(a.run(inner), v.get(), a[v], b[v])
def boom():
    v.set('lost')
    raise KeyError('x')
try:
    contextvars.Context().run(boom)
except KeyError as e:
    print('KeyError', e, v.get())
w = contextvars.ContextVar('w')
print(a.run(w.get, 'dflt'), contextvars.Context().run(v.get))
try:
    a.run(w.get)
except LookupError as e:
    print('LookupError', e.args[0] is w)
v.set(contextvars.Token.MISSING)
print(v.get() is contextvars.Token.MISSING)
