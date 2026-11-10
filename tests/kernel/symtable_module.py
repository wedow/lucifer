# symtable's tables are memoized by (entry, filename): the native symbol
# table entries hash, compare and repr as objects of a ready type.
import symtable

src = "def f(x):\n    return x\nclass C:\n    def m(self): pass\n"
for i in range(3):
    t = symtable.symtable(src, "?", "exec")
    print(t.get_type(), [c.get_name() for c in t.get_children()])
    print(t.lookup('f').is_assigned(), t.get_children()[1].get_parameters())
print(type(t._table).__name__, hash(t._table) == hash(t._table), t._table == t._table)
