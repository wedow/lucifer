# The descriptor types' reprs name their kind, attribute and class
# (Objects/descrobject.c); a method-wrapper names its object's type.
print(repr(list.append), repr(dict.__dict__["fromkeys"]), repr(object.__init__))
print(repr(int.real), repr(classmethod.__func__))
print(repr((1).__add__).split(" at ")[0])
