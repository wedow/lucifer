import contextvars

v = contextvars.ContextVar('v', default='d')
w = contextvars.ContextVar('w')
print(v.name, v.get(), v.get('x'), repr(v).startswith("<ContextVar name='v' default='d' at 0x"))
try:
    w.get()
except LookupError as e:
    print('LookupError', e.args[0] is w)
t = v.set(1)
print(v.get(), t.var is v, t.old_value is contextvars.Token.MISSING)
ctx = contextvars.copy_context()
v.set(2)
print(ctx[v], v.get(), len(ctx), v in ctx, list(ctx.items()) == [(v, 1)])
print(ctx.run(lambda: (v.get(), v.set(3) and v.get())), v.get())
try:
    ctx.run(ctx.run, print)
except RuntimeError as e:
    print('RuntimeError', 'already entered' in str(e))
v.reset(t)
print(v.get())
try:
    v.reset(t)
except RuntimeError as e:
    print('RuntimeError', 'used' in str(e))
try:
    w.reset(contextvars.ContextVar('z').set(1))
except ValueError as e:
    print('ValueError')
with v.set('ctx') as tok:
    print(v.get())
print(v.get())
print(contextvars.ContextVar[int])
c = contextvars.Context()
print(len(c), c.get(v, 'none'), c == contextvars.Context())
try:
    ctx[1]
except TypeError:
    print('TypeError')
