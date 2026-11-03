class Prop:
    def __init__(self):
        self._x = 'initial'

    @property
    def x(self):
        """the x property"""
        print('get x')
        return self._x

    @x.setter
    def x(self, value):
        print('set x', value)
        self._x = value

    @x.deleter
    def x(self):
        print('del x')
        self._x = 'deleted'

    def _get_y(self):
        return 'y'

    y = property(_get_y, doc='y doc')


p = Prop()
print(p.x)
p.x = 'new'
print(p.x)
del p.x
print(p._x)
print(Prop.x.__doc__, Prop.y.__doc__, p.y)
print(type(Prop.x).__name__, Prop.x.fget.__name__, Prop.x.__name__)
if Prop.x.fset is not None and Prop.y.fset is None:
    print('fset')


class Methods:
    tag = 'Methods.tag'

    def plain(self):
        return 'plain'

    @classmethod
    def cm(cls, arg):
        print('cm', cls.__name__, arg)
        return cls.tag

    @staticmethod
    def sm(arg):
        return arg


class SubMethods(Methods):
    tag = 'SubMethods.tag'


mm = Methods()
print(Methods.cm('c'), mm.cm('i'), SubMethods.cm('s'))
print(Methods.sm('static'), mm.sm('from instance'))
print(type(Methods.plain).__name__)
print(type(mm.plain).__name__, type(Methods.cm).__name__)
if mm.plain.__self__ is mm and mm.plain.__func__ is Methods.plain:
    print('bound method')
if Methods.cm.__self__ is Methods and SubMethods.cm.__self__ is SubMethods:
    print('classmethod binds the class')
bound = Methods.plain.__get__(mm)
print(bound(), Methods.plain(mm))


class Data:
    def __set_name__(self, owner, name):
        self.name = name

    def __get__(self, obj, objtype=None):
        if obj is None:
            return self
        print('Data.__get__', self.name, objtype.__name__)
        return obj.stored

    def __set__(self, obj, value):
        print('Data.__set__', self.name, value)
        obj.stored = value

    def __delete__(self, obj):
        print('Data.__delete__', self.name)


class NonData:
    def __get__(self, obj, objtype=None):
        return 'NonData.__get__'


class Holder:
    d = Data()
    n = NonData()


h = Holder()
h.d = 'first'
print(h.d)
del h.d
print(h.n)
h.n = 'instance wins'
print(h.n)
if Holder.d is Holder.__dict__.get('d'):
    print('class access returns the descriptor')


class Base:
    def hello(self):
        return 'Base.hello'

    @classmethod
    def make(cls):
        return cls.__name__


class Derived(Base):
    def hello(self):
        return 'Derived.hello'


dd = Derived()
print(super(Derived, dd).hello(), super(Derived, Derived).make())
sp = super(Derived, dd)
print(type(sp).__name__, sp.__thisclass__.__name__, sp.__self_class__.__name__)
if sp.__self__ is dd:
    print('super.__self__')


class Zero(Derived):
    def hello(self):
        s = super()
        return s.hello()


print(Zero().hello())
print(type(object.__init__).__name__, type(object().__init__).__name__)
print(type(type.mro).__name__, type(object.__new__).__name__)
print(type(type.__dict__.get('__name__')).__name__)
print(type(Methods.__dict__.get('cm')).__name__, type(Methods.__dict__.get('sm')).__name__)
print(type(object.__dict__.get('__init_subclass__')).__name__)
print(type(int.__dict__).__name__)
if Methods.__dict__.get('sm').__func__ is Methods.sm:
    print('staticmethod __func__')
print(Methods.plain.__name__, Methods.plain.__qualname__, Methods.plain.__module__)
print(type.__dict__.get('__name__').__get__(Methods), Methods.__dict__.get('tag'))
print(object.__init__.__name__, object.__init__.__objclass__.__name__)
