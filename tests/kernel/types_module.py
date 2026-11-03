import sys
import types

for name in sorted(n for n in dir(types) if not n.startswith('__')):
    v = getattr(types, name)
    print(name, getattr(v, '__module__', None) if isinstance(v, type) else type(v).__name__,
          getattr(v, '__qualname__', None))

# the types, by what they describe
def f(): pass
async def co(): pass
def g(): yield
async def ag(): yield
class C:
    def m(self): pass
    @classmethod
    def cm(cls): pass
c = co()
print(types.FunctionType is type(f), types.LambdaType is types.FunctionType, types.CodeType is type(f.__code__))
print(types.MappingProxyType is type(C.__dict__), types.SimpleNamespace(a=1))
print(types.GeneratorType is type(g()), types.CoroutineType is type(c), types.AsyncGeneratorType is type(ag()))
print(types.MethodType is type(C().m), types.BuiltinFunctionType is type(len), types.BuiltinMethodType is type([].append))
print(types.WrapperDescriptorType is type(object.__init__), types.MethodWrapperType is type(object().__str__),
      types.MethodDescriptorType is type(str.join), types.ClassMethodDescriptorType is type(dict.__dict__['fromkeys']))
print(types.ModuleType is type(sys), types.GetSetDescriptorType is type(types.FunctionType.__code__),
      types.MemberDescriptorType is type(types.FunctionType.__globals__))
print(types.EllipsisType is type(...), types.NoneType is type(None), types.NotImplementedType is type(NotImplemented))
print(types.CellType is type((lambda a: lambda: a)(1).__closure__[0]))
print(types.CapsuleType, types.GenericAlias is type(list[int]), types.UnionType is type(int | str))
c.close()

# tracebacks and frames
try:
    raise TypeError
except TypeError as exc:
    tb = exc.__traceback__
    print(types.TracebackType is type(tb), types.FrameType is type(tb.tb_frame))
    print(tb.tb_frame.f_code.co_name, tb.tb_frame is sys._getframe(), tb.tb_frame.f_globals is globals())
    print(tb.tb_lineno, tb.tb_frame.f_lineno == tb.tb_lineno, tb.tb_next)

def inner():
    raise ValueError('deep')
def outer():
    inner()
try:
    outer()
except ValueError as exc:
    tb = exc.__traceback__
    names = []
    while tb is not None:
        names.append((tb.tb_frame.f_code.co_name, tb.tb_frame.f_back is not None))
        tb = tb.tb_next
    print(names)

# the helpers
def body(ns):
    ns['x'] = 1
    ns['f'] = lambda self: self.x
K = types.new_class('K', (), {}, body)
print(K, K().f(), K.__module__, K.__mro__)
class Meta(type):
    @classmethod
    def __prepare__(mcs, name, bases, **kw):
        print('prepare', name, kw)
        return {'prepared': True}
K2 = types.new_class('K2', (dict,), {'metaclass': Meta})
print(K2.prepared, K2.__bases__, type(K2).__name__)
print(types.prepare_class('P', (int,), {'metaclass': Meta}))
class Entry:
    def __mro_entries__(self, bases):
        return (int,)
print(types.resolve_bases((Entry(), object)), types.resolve_bases((int,)))
print(types.new_class('E', (Entry(),)).__mro__[1], types.new_class('E', (Entry(),)).__orig_bases__ is not None)
print(types.DynamicClassAttribute, len(types.get_original_bases(types.new_class('E', (Entry(),)))))
ns = types.SimpleNamespace(a=1, b=[2])
print(ns, ns == types.SimpleNamespace(a=1, b=[2]), types.SimpleNamespace(**{'z': 0}).z)
print(types.MethodType(f, 3).__self__)
