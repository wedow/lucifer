"""_ast (Python/Python-ast.c): the node classes of the abstract syntax tree.

AST is the root class with the constructor, repr, pickling and
copy.replace support of Python-ast.c; the node classes are made from the
table that tools/genast writes from Parser/Python.asdl, in the same order
and with the same attributes as Python-ast.c's init_types.
"""

from _lucifer import repr_enter, repr_leave
from _warnings import warn as _warn

PyCF_ONLY_AST = 0x400
PyCF_TYPE_COMMENTS = 0x1000
PyCF_ALLOW_TOP_LEVEL_AWAIT = 0x2000
PyCF_OPTIMIZED_AST = 0x8000 | PyCF_ONLY_AST

_UnionType = type(int | None)
_GenericAlias = type(list[int])


class AST:
    _fields = ()
    __match_args__ = ()
    _attributes = ()
    __module__ = 'ast'

    def __init__(self, *args, **kw):
        cls = type(self)
        fields = cls._fields
        if len(fields) < len(args):
            n = len(fields)
            raise TypeError('%s constructor takes at most %d positional argument%s'
                            % (cls.__name__, n, '' if n == 1 else 's'))
        n = len(args)
        i = 0
        while i < n:
            setattr(self, fields[i], args[i])
            i += 1
        if not kw and n == len(fields):
            return
        remaining = list(fields[n:])
        attributes = None
        for key, value in kw.items():
            if key in fields:
                if key not in remaining:
                    raise TypeError('%s got multiple values for argument %r' % (cls.__name__, key))
                remaining.remove(key)
            else:
                if attributes is None:
                    attributes = cls._attributes
                if key not in attributes:
                    _warn('%s.__init__ got an unexpected keyword argument %r. Support for arbitrary '
                          'keyword arguments is deprecated and will be removed in Python 3.15.'
                          % (cls.__name__, key), DeprecationWarning, 1)
            setattr(self, key, value)
        if not remaining:
            return
        field_types = getattr(cls, '_field_types', None)
        if field_types is None:
            return      # a user-defined subclass: absent fields stay absent, as before 3.13
        for name in remaining:
            if name not in field_types:
                _warn('Field %r is missing from %s._field_types. This will become an error in '
                      'Python 3.15.' % (name, cls.__name__), DeprecationWarning, 1)
                continue
            t = field_types[name]
            if type(t) is _UnionType:
                pass        # optional: the class default is None
            elif type(t) is _GenericAlias:
                setattr(self, name, [])
            elif t is expr_context:
                setattr(self, name, Load_singleton)
            else:
                _warn('%s.__init__ missing 1 required positional argument: %r. This will become '
                      'an error in Python 3.15.' % (cls.__name__, name), DeprecationWarning, 1)

    def __reduce__(self):
        cls = type(self)
        try:
            d = self.__dict__
        except AttributeError:
            return (cls, ())
        try:
            fields = cls._fields
        except AttributeError:
            return (cls, (), d)
        # Every field present becomes a None placeholder, so that unpickling constructs
        # without warnings and without recursing into children; the dict then fills them.
        args = []
        for name in fields:
            if name not in d:
                break
            args.append(None)
        return (cls, tuple(args), d)

    def __replace__(self, *args, **kwargs):
        """Return a copy of the AST node with new values for the specified fields."""
        if args:
            raise TypeError('__replace__() takes no positional arguments')
        cls = type(self)
        fields = getattr(cls, '_fields', None)
        attributes = getattr(cls, '_attributes', None)
        try:
            d = self.__dict__
        except AttributeError:
            d = None
        _replace_check(self, d, fields, attributes, kwargs)
        payload = {}
        if d is not None:
            for keys in (fields, attributes):
                for key in keys or ():
                    if key in d:
                        payload[key] = d[key]
        payload.update(kwargs)
        return cls(**payload)

    def __repr__(self):
        return _repr(self, 3)


def _replace_check(self, d, fields, attributes, kwargs):
    """ast_type_replace_check: a TypeError for a keyword that is no field or
    attribute and for a field the copy would leave unfilled."""
    cls = type(self)
    expecting = set(fields or ())
    if attributes:
        expecting.update(attributes)
    for key in kwargs:
        if key not in expecting:
            raise TypeError('%s.__replace__ got an unexpected keyword argument %r.' % (cls.__name__, key))
        expecting.discard(key)
    if d is not None:
        expecting.difference_update(d)
        if attributes:
            expecting.difference_update(attributes)
    field_types = getattr(cls, '_field_types', None)
    if field_types is not None:
        for name, t in field_types.items():
            if type(t) is _UnionType:
                expecting.discard(name)
    m = len(expecting)
    if m:
        names = ', '.join(sorted(repr(n) for n in expecting))
        raise TypeError('%s.__replace__ missing %d keyword argument%s: %s.'
                        % (cls.__name__, m, '' if m == 1 else 's', names))


def _repr_list(seq, depth):
    """ast_repr_list: only the first and last items, with '...' between."""
    n = len(seq)
    if n == 0:
        return repr(seq)
    shown = (seq[0], seq[n - 1]) if n > 1 else (seq[0],)
    texts = [_repr(x, depth - 1) if isinstance(x, AST) else repr(x) for x in shown]
    if n > 2:
        texts.insert(1, '...')
    return ('[%s]' if isinstance(seq, list) else '(%s)') % ', '.join(texts)


def _repr(self, depth):
    """ast_repr_max_depth"""
    name = type(self).__name__
    if depth <= 0:
        return name + '(...)'
    if repr_enter(self):
        return name + '(...)'
    try:
        fields = type(self)._fields
        if not fields:
            return name + '()'
        parts = []
        for field in fields:
            value = getattr(self, field)
            if isinstance(value, (list, tuple)):
                text = _repr_list(value, depth)
            elif isinstance(value, AST):
                text = _repr(value, depth - 1)
            else:
                text = repr(value)
            parts.append(field + '=' + text)
        return name + '(' + ', '.join(parts) + ')'
    finally:
        repr_leave(self)


_BUILTIN_TYPES = {'identifier': str, 'string': str, 'int': int, 'constant': object}


def _make_types(nodes):
    classes = {}
    for name, base, kind, fields, attributes, doc in nodes:
        names = tuple([f[0] for f in fields])
        ns = {'_fields': names, '__match_args__': names, '__module__': 'ast', '__doc__': doc}
        if kind in ('sum', 'product'):
            ns['_attributes'] = tuple([a[0] for a in attributes])
        for f in fields + attributes:
            if f[2] == '?':
                ns[f[0]] = None
        classes[name] = type(name, (classes[base] if base else AST,), ns)
    for name, base, kind, fields, attributes, doc in nodes:
        if kind in ('sum',):
            continue
        field_types = {}
        for fname, ftype, flag in fields:
            t = _BUILTIN_TYPES.get(ftype) or classes[ftype]
            field_types[fname] = t | None if flag == '?' else list[t] if flag == '*' else t
        cls = classes[name]
        cls._field_types = field_types
        cls.__annotations__ = field_types
    return classes


# BEGIN GENERATED
_NODES = (
    ('mod', None, 'sum', (), (),
     'mod = Module(stmt* body, type_ignore* type_ignores)\n    | Interactive(stmt* body)\n    | Expression(expr body)\n    | FunctionType(expr* argtypes, expr returns)'),
    ('Module', 'mod', 'ctor', (('body', 'stmt', '*'), ('type_ignores', 'type_ignore', '*')), (),
     'Module(stmt* body, type_ignore* type_ignores)'),
    ('Interactive', 'mod', 'ctor', (('body', 'stmt', '*'),), (),
     'Interactive(stmt* body)'),
    ('Expression', 'mod', 'ctor', (('body', 'expr', ''),), (),
     'Expression(expr body)'),
    ('FunctionType', 'mod', 'ctor', (('argtypes', 'expr', '*'), ('returns', 'expr', '')), (),
     'FunctionType(expr* argtypes, expr returns)'),
    ('stmt', None, 'sum', (), (('lineno', 'int', ''), ('col_offset', 'int', ''), ('end_lineno', 'int', '?'), ('end_col_offset', 'int', '?')),
     'stmt = FunctionDef(identifier name, arguments args, stmt* body, expr* decorator_list, expr? returns, string? type_comment, type_param* type_params)\n     | AsyncFunctionDef(identifier name, arguments args, stmt* body, expr* decorator_list, expr? returns, string? type_comment, type_param* type_params)\n     | ClassDef(identifier name, expr* bases, keyword* keywords, stmt* body, expr* decorator_list, type_param* type_params)\n     | Return(expr? value)\n     | Delete(expr* targets)\n     | Assign(expr* targets, expr value, string? type_comment)\n     | TypeAlias(expr name, type_param* type_params, expr value)\n     | AugAssign(expr target, operator op, expr value)\n     | AnnAssign(expr target, expr annotation, expr? value, int simple)\n     | For(expr target, expr iter, stmt* body, stmt* orelse, string? type_comment)\n     | AsyncFor(expr target, expr iter, stmt* body, stmt* orelse, string? type_comment)\n     | While(expr test, stmt* body, stmt* orelse)\n     | If(expr test, stmt* body, stmt* orelse)\n     | With(withitem* items, stmt* body, string? type_comment)\n     | AsyncWith(withitem* items, stmt* body, string? type_comment)\n     | Match(expr subject, match_case* cases)\n     | Raise(expr? exc, expr? cause)\n     | Try(stmt* body, excepthandler* handlers, stmt* orelse, stmt* finalbody)\n     | TryStar(stmt* body, excepthandler* handlers, stmt* orelse, stmt* finalbody)\n     | Assert(expr test, expr? msg)\n     | Import(alias* names)\n     | ImportFrom(identifier? module, alias* names, int? level)\n     | Global(identifier* names)\n     | Nonlocal(identifier* names)\n     | Expr(expr value)\n     | Pass\n     | Break\n     | Continue'),
    ('FunctionDef', 'stmt', 'ctor', (('name', 'identifier', ''), ('args', 'arguments', ''), ('body', 'stmt', '*'), ('decorator_list', 'expr', '*'), ('returns', 'expr', '?'), ('type_comment', 'string', '?'), ('type_params', 'type_param', '*')), (),
     'FunctionDef(identifier name, arguments args, stmt* body, expr* decorator_list, expr? returns, string? type_comment, type_param* type_params)'),
    ('AsyncFunctionDef', 'stmt', 'ctor', (('name', 'identifier', ''), ('args', 'arguments', ''), ('body', 'stmt', '*'), ('decorator_list', 'expr', '*'), ('returns', 'expr', '?'), ('type_comment', 'string', '?'), ('type_params', 'type_param', '*')), (),
     'AsyncFunctionDef(identifier name, arguments args, stmt* body, expr* decorator_list, expr? returns, string? type_comment, type_param* type_params)'),
    ('ClassDef', 'stmt', 'ctor', (('name', 'identifier', ''), ('bases', 'expr', '*'), ('keywords', 'keyword', '*'), ('body', 'stmt', '*'), ('decorator_list', 'expr', '*'), ('type_params', 'type_param', '*')), (),
     'ClassDef(identifier name, expr* bases, keyword* keywords, stmt* body, expr* decorator_list, type_param* type_params)'),
    ('Return', 'stmt', 'ctor', (('value', 'expr', '?'),), (),
     'Return(expr? value)'),
    ('Delete', 'stmt', 'ctor', (('targets', 'expr', '*'),), (),
     'Delete(expr* targets)'),
    ('Assign', 'stmt', 'ctor', (('targets', 'expr', '*'), ('value', 'expr', ''), ('type_comment', 'string', '?')), (),
     'Assign(expr* targets, expr value, string? type_comment)'),
    ('TypeAlias', 'stmt', 'ctor', (('name', 'expr', ''), ('type_params', 'type_param', '*'), ('value', 'expr', '')), (),
     'TypeAlias(expr name, type_param* type_params, expr value)'),
    ('AugAssign', 'stmt', 'ctor', (('target', 'expr', ''), ('op', 'operator', ''), ('value', 'expr', '')), (),
     'AugAssign(expr target, operator op, expr value)'),
    ('AnnAssign', 'stmt', 'ctor', (('target', 'expr', ''), ('annotation', 'expr', ''), ('value', 'expr', '?'), ('simple', 'int', '')), (),
     'AnnAssign(expr target, expr annotation, expr? value, int simple)'),
    ('For', 'stmt', 'ctor', (('target', 'expr', ''), ('iter', 'expr', ''), ('body', 'stmt', '*'), ('orelse', 'stmt', '*'), ('type_comment', 'string', '?')), (),
     'For(expr target, expr iter, stmt* body, stmt* orelse, string? type_comment)'),
    ('AsyncFor', 'stmt', 'ctor', (('target', 'expr', ''), ('iter', 'expr', ''), ('body', 'stmt', '*'), ('orelse', 'stmt', '*'), ('type_comment', 'string', '?')), (),
     'AsyncFor(expr target, expr iter, stmt* body, stmt* orelse, string? type_comment)'),
    ('While', 'stmt', 'ctor', (('test', 'expr', ''), ('body', 'stmt', '*'), ('orelse', 'stmt', '*')), (),
     'While(expr test, stmt* body, stmt* orelse)'),
    ('If', 'stmt', 'ctor', (('test', 'expr', ''), ('body', 'stmt', '*'), ('orelse', 'stmt', '*')), (),
     'If(expr test, stmt* body, stmt* orelse)'),
    ('With', 'stmt', 'ctor', (('items', 'withitem', '*'), ('body', 'stmt', '*'), ('type_comment', 'string', '?')), (),
     'With(withitem* items, stmt* body, string? type_comment)'),
    ('AsyncWith', 'stmt', 'ctor', (('items', 'withitem', '*'), ('body', 'stmt', '*'), ('type_comment', 'string', '?')), (),
     'AsyncWith(withitem* items, stmt* body, string? type_comment)'),
    ('Match', 'stmt', 'ctor', (('subject', 'expr', ''), ('cases', 'match_case', '*')), (),
     'Match(expr subject, match_case* cases)'),
    ('Raise', 'stmt', 'ctor', (('exc', 'expr', '?'), ('cause', 'expr', '?')), (),
     'Raise(expr? exc, expr? cause)'),
    ('Try', 'stmt', 'ctor', (('body', 'stmt', '*'), ('handlers', 'excepthandler', '*'), ('orelse', 'stmt', '*'), ('finalbody', 'stmt', '*')), (),
     'Try(stmt* body, excepthandler* handlers, stmt* orelse, stmt* finalbody)'),
    ('TryStar', 'stmt', 'ctor', (('body', 'stmt', '*'), ('handlers', 'excepthandler', '*'), ('orelse', 'stmt', '*'), ('finalbody', 'stmt', '*')), (),
     'TryStar(stmt* body, excepthandler* handlers, stmt* orelse, stmt* finalbody)'),
    ('Assert', 'stmt', 'ctor', (('test', 'expr', ''), ('msg', 'expr', '?')), (),
     'Assert(expr test, expr? msg)'),
    ('Import', 'stmt', 'ctor', (('names', 'alias', '*'),), (),
     'Import(alias* names)'),
    ('ImportFrom', 'stmt', 'ctor', (('module', 'identifier', '?'), ('names', 'alias', '*'), ('level', 'int', '?')), (),
     'ImportFrom(identifier? module, alias* names, int? level)'),
    ('Global', 'stmt', 'ctor', (('names', 'identifier', '*'),), (),
     'Global(identifier* names)'),
    ('Nonlocal', 'stmt', 'ctor', (('names', 'identifier', '*'),), (),
     'Nonlocal(identifier* names)'),
    ('Expr', 'stmt', 'ctor', (('value', 'expr', ''),), (),
     'Expr(expr value)'),
    ('Pass', 'stmt', 'ctor', (), (),
     'Pass'),
    ('Break', 'stmt', 'ctor', (), (),
     'Break'),
    ('Continue', 'stmt', 'ctor', (), (),
     'Continue'),
    ('expr', None, 'sum', (), (('lineno', 'int', ''), ('col_offset', 'int', ''), ('end_lineno', 'int', '?'), ('end_col_offset', 'int', '?')),
     'expr = BoolOp(boolop op, expr* values)\n     | NamedExpr(expr target, expr value)\n     | BinOp(expr left, operator op, expr right)\n     | UnaryOp(unaryop op, expr operand)\n     | Lambda(arguments args, expr body)\n     | IfExp(expr test, expr body, expr orelse)\n     | Dict(expr?* keys, expr* values)\n     | Set(expr* elts)\n     | ListComp(expr elt, comprehension* generators)\n     | SetComp(expr elt, comprehension* generators)\n     | DictComp(expr key, expr value, comprehension* generators)\n     | GeneratorExp(expr elt, comprehension* generators)\n     | Await(expr value)\n     | Yield(expr? value)\n     | YieldFrom(expr value)\n     | Compare(expr left, cmpop* ops, expr* comparators)\n     | Call(expr func, expr* args, keyword* keywords)\n     | FormattedValue(expr value, int conversion, expr? format_spec)\n     | Interpolation(expr value, constant str, int conversion, expr? format_spec)\n     | JoinedStr(expr* values)\n     | TemplateStr(expr* values)\n     | Constant(constant value, string? kind)\n     | Attribute(expr value, identifier attr, expr_context ctx)\n     | Subscript(expr value, expr slice, expr_context ctx)\n     | Starred(expr value, expr_context ctx)\n     | Name(identifier id, expr_context ctx)\n     | List(expr* elts, expr_context ctx)\n     | Tuple(expr* elts, expr_context ctx)\n     | Slice(expr? lower, expr? upper, expr? step)'),
    ('BoolOp', 'expr', 'ctor', (('op', 'boolop', ''), ('values', 'expr', '*')), (),
     'BoolOp(boolop op, expr* values)'),
    ('NamedExpr', 'expr', 'ctor', (('target', 'expr', ''), ('value', 'expr', '')), (),
     'NamedExpr(expr target, expr value)'),
    ('BinOp', 'expr', 'ctor', (('left', 'expr', ''), ('op', 'operator', ''), ('right', 'expr', '')), (),
     'BinOp(expr left, operator op, expr right)'),
    ('UnaryOp', 'expr', 'ctor', (('op', 'unaryop', ''), ('operand', 'expr', '')), (),
     'UnaryOp(unaryop op, expr operand)'),
    ('Lambda', 'expr', 'ctor', (('args', 'arguments', ''), ('body', 'expr', '')), (),
     'Lambda(arguments args, expr body)'),
    ('IfExp', 'expr', 'ctor', (('test', 'expr', ''), ('body', 'expr', ''), ('orelse', 'expr', '')), (),
     'IfExp(expr test, expr body, expr orelse)'),
    ('Dict', 'expr', 'ctor', (('keys', 'expr', '*'), ('values', 'expr', '*')), (),
     'Dict(expr?* keys, expr* values)'),
    ('Set', 'expr', 'ctor', (('elts', 'expr', '*'),), (),
     'Set(expr* elts)'),
    ('ListComp', 'expr', 'ctor', (('elt', 'expr', ''), ('generators', 'comprehension', '*')), (),
     'ListComp(expr elt, comprehension* generators)'),
    ('SetComp', 'expr', 'ctor', (('elt', 'expr', ''), ('generators', 'comprehension', '*')), (),
     'SetComp(expr elt, comprehension* generators)'),
    ('DictComp', 'expr', 'ctor', (('key', 'expr', ''), ('value', 'expr', ''), ('generators', 'comprehension', '*')), (),
     'DictComp(expr key, expr value, comprehension* generators)'),
    ('GeneratorExp', 'expr', 'ctor', (('elt', 'expr', ''), ('generators', 'comprehension', '*')), (),
     'GeneratorExp(expr elt, comprehension* generators)'),
    ('Await', 'expr', 'ctor', (('value', 'expr', ''),), (),
     'Await(expr value)'),
    ('Yield', 'expr', 'ctor', (('value', 'expr', '?'),), (),
     'Yield(expr? value)'),
    ('YieldFrom', 'expr', 'ctor', (('value', 'expr', ''),), (),
     'YieldFrom(expr value)'),
    ('Compare', 'expr', 'ctor', (('left', 'expr', ''), ('ops', 'cmpop', '*'), ('comparators', 'expr', '*')), (),
     'Compare(expr left, cmpop* ops, expr* comparators)'),
    ('Call', 'expr', 'ctor', (('func', 'expr', ''), ('args', 'expr', '*'), ('keywords', 'keyword', '*')), (),
     'Call(expr func, expr* args, keyword* keywords)'),
    ('FormattedValue', 'expr', 'ctor', (('value', 'expr', ''), ('conversion', 'int', ''), ('format_spec', 'expr', '?')), (),
     'FormattedValue(expr value, int conversion, expr? format_spec)'),
    ('Interpolation', 'expr', 'ctor', (('value', 'expr', ''), ('str', 'constant', ''), ('conversion', 'int', ''), ('format_spec', 'expr', '?')), (),
     'Interpolation(expr value, constant str, int conversion, expr? format_spec)'),
    ('JoinedStr', 'expr', 'ctor', (('values', 'expr', '*'),), (),
     'JoinedStr(expr* values)'),
    ('TemplateStr', 'expr', 'ctor', (('values', 'expr', '*'),), (),
     'TemplateStr(expr* values)'),
    ('Constant', 'expr', 'ctor', (('value', 'constant', ''), ('kind', 'string', '?')), (),
     'Constant(constant value, string? kind)'),
    ('Attribute', 'expr', 'ctor', (('value', 'expr', ''), ('attr', 'identifier', ''), ('ctx', 'expr_context', '')), (),
     'Attribute(expr value, identifier attr, expr_context ctx)'),
    ('Subscript', 'expr', 'ctor', (('value', 'expr', ''), ('slice', 'expr', ''), ('ctx', 'expr_context', '')), (),
     'Subscript(expr value, expr slice, expr_context ctx)'),
    ('Starred', 'expr', 'ctor', (('value', 'expr', ''), ('ctx', 'expr_context', '')), (),
     'Starred(expr value, expr_context ctx)'),
    ('Name', 'expr', 'ctor', (('id', 'identifier', ''), ('ctx', 'expr_context', '')), (),
     'Name(identifier id, expr_context ctx)'),
    ('List', 'expr', 'ctor', (('elts', 'expr', '*'), ('ctx', 'expr_context', '')), (),
     'List(expr* elts, expr_context ctx)'),
    ('Tuple', 'expr', 'ctor', (('elts', 'expr', '*'), ('ctx', 'expr_context', '')), (),
     'Tuple(expr* elts, expr_context ctx)'),
    ('Slice', 'expr', 'ctor', (('lower', 'expr', '?'), ('upper', 'expr', '?'), ('step', 'expr', '?')), (),
     'Slice(expr? lower, expr? upper, expr? step)'),
    ('expr_context', None, 'sum', (), (),
     'expr_context = Load | Store | Del'),
    ('Load', 'expr_context', 'simple', (), (),
     'Load'),
    ('Store', 'expr_context', 'simple', (), (),
     'Store'),
    ('Del', 'expr_context', 'simple', (), (),
     'Del'),
    ('boolop', None, 'sum', (), (),
     'boolop = And | Or'),
    ('And', 'boolop', 'simple', (), (),
     'And'),
    ('Or', 'boolop', 'simple', (), (),
     'Or'),
    ('operator', None, 'sum', (), (),
     'operator = Add | Sub | Mult | MatMult | Div | Mod | Pow | LShift | RShift | BitOr | BitXor | BitAnd | FloorDiv'),
    ('Add', 'operator', 'simple', (), (),
     'Add'),
    ('Sub', 'operator', 'simple', (), (),
     'Sub'),
    ('Mult', 'operator', 'simple', (), (),
     'Mult'),
    ('MatMult', 'operator', 'simple', (), (),
     'MatMult'),
    ('Div', 'operator', 'simple', (), (),
     'Div'),
    ('Mod', 'operator', 'simple', (), (),
     'Mod'),
    ('Pow', 'operator', 'simple', (), (),
     'Pow'),
    ('LShift', 'operator', 'simple', (), (),
     'LShift'),
    ('RShift', 'operator', 'simple', (), (),
     'RShift'),
    ('BitOr', 'operator', 'simple', (), (),
     'BitOr'),
    ('BitXor', 'operator', 'simple', (), (),
     'BitXor'),
    ('BitAnd', 'operator', 'simple', (), (),
     'BitAnd'),
    ('FloorDiv', 'operator', 'simple', (), (),
     'FloorDiv'),
    ('unaryop', None, 'sum', (), (),
     'unaryop = Invert | Not | UAdd | USub'),
    ('Invert', 'unaryop', 'simple', (), (),
     'Invert'),
    ('Not', 'unaryop', 'simple', (), (),
     'Not'),
    ('UAdd', 'unaryop', 'simple', (), (),
     'UAdd'),
    ('USub', 'unaryop', 'simple', (), (),
     'USub'),
    ('cmpop', None, 'sum', (), (),
     'cmpop = Eq | NotEq | Lt | LtE | Gt | GtE | Is | IsNot | In | NotIn'),
    ('Eq', 'cmpop', 'simple', (), (),
     'Eq'),
    ('NotEq', 'cmpop', 'simple', (), (),
     'NotEq'),
    ('Lt', 'cmpop', 'simple', (), (),
     'Lt'),
    ('LtE', 'cmpop', 'simple', (), (),
     'LtE'),
    ('Gt', 'cmpop', 'simple', (), (),
     'Gt'),
    ('GtE', 'cmpop', 'simple', (), (),
     'GtE'),
    ('Is', 'cmpop', 'simple', (), (),
     'Is'),
    ('IsNot', 'cmpop', 'simple', (), (),
     'IsNot'),
    ('In', 'cmpop', 'simple', (), (),
     'In'),
    ('NotIn', 'cmpop', 'simple', (), (),
     'NotIn'),
    ('comprehension', None, 'product', (('target', 'expr', ''), ('iter', 'expr', ''), ('ifs', 'expr', '*'), ('is_async', 'int', '')), (),
     'comprehension(expr target, expr iter, expr* ifs, int is_async)'),
    ('excepthandler', None, 'sum', (), (('lineno', 'int', ''), ('col_offset', 'int', ''), ('end_lineno', 'int', '?'), ('end_col_offset', 'int', '?')),
     'excepthandler = ExceptHandler(expr? type, identifier? name, stmt* body)'),
    ('ExceptHandler', 'excepthandler', 'ctor', (('type', 'expr', '?'), ('name', 'identifier', '?'), ('body', 'stmt', '*')), (),
     'ExceptHandler(expr? type, identifier? name, stmt* body)'),
    ('arguments', None, 'product', (('posonlyargs', 'arg', '*'), ('args', 'arg', '*'), ('vararg', 'arg', '?'), ('kwonlyargs', 'arg', '*'), ('kw_defaults', 'expr', '*'), ('kwarg', 'arg', '?'), ('defaults', 'expr', '*')), (),
     'arguments(arg* posonlyargs, arg* args, arg? vararg, arg* kwonlyargs, expr?* kw_defaults, arg? kwarg, expr* defaults)'),
    ('arg', None, 'product', (('arg', 'identifier', ''), ('annotation', 'expr', '?'), ('type_comment', 'string', '?')), (('lineno', 'int', ''), ('col_offset', 'int', ''), ('end_lineno', 'int', '?'), ('end_col_offset', 'int', '?')),
     'arg(identifier arg, expr? annotation, string? type_comment)'),
    ('keyword', None, 'product', (('arg', 'identifier', '?'), ('value', 'expr', '')), (('lineno', 'int', ''), ('col_offset', 'int', ''), ('end_lineno', 'int', '?'), ('end_col_offset', 'int', '?')),
     'keyword(identifier? arg, expr value)'),
    ('alias', None, 'product', (('name', 'identifier', ''), ('asname', 'identifier', '?')), (('lineno', 'int', ''), ('col_offset', 'int', ''), ('end_lineno', 'int', '?'), ('end_col_offset', 'int', '?')),
     'alias(identifier name, identifier? asname)'),
    ('withitem', None, 'product', (('context_expr', 'expr', ''), ('optional_vars', 'expr', '?')), (),
     'withitem(expr context_expr, expr? optional_vars)'),
    ('match_case', None, 'product', (('pattern', 'pattern', ''), ('guard', 'expr', '?'), ('body', 'stmt', '*')), (),
     'match_case(pattern pattern, expr? guard, stmt* body)'),
    ('pattern', None, 'sum', (), (('lineno', 'int', ''), ('col_offset', 'int', ''), ('end_lineno', 'int', ''), ('end_col_offset', 'int', '')),
     'pattern = MatchValue(expr value)\n        | MatchSingleton(constant value)\n        | MatchSequence(pattern* patterns)\n        | MatchMapping(expr* keys, pattern* patterns, identifier? rest)\n        | MatchClass(expr cls, pattern* patterns, identifier* kwd_attrs, pattern* kwd_patterns)\n        | MatchStar(identifier? name)\n        | MatchAs(pattern? pattern, identifier? name)\n        | MatchOr(pattern* patterns)'),
    ('MatchValue', 'pattern', 'ctor', (('value', 'expr', ''),), (),
     'MatchValue(expr value)'),
    ('MatchSingleton', 'pattern', 'ctor', (('value', 'constant', ''),), (),
     'MatchSingleton(constant value)'),
    ('MatchSequence', 'pattern', 'ctor', (('patterns', 'pattern', '*'),), (),
     'MatchSequence(pattern* patterns)'),
    ('MatchMapping', 'pattern', 'ctor', (('keys', 'expr', '*'), ('patterns', 'pattern', '*'), ('rest', 'identifier', '?')), (),
     'MatchMapping(expr* keys, pattern* patterns, identifier? rest)'),
    ('MatchClass', 'pattern', 'ctor', (('cls', 'expr', ''), ('patterns', 'pattern', '*'), ('kwd_attrs', 'identifier', '*'), ('kwd_patterns', 'pattern', '*')), (),
     'MatchClass(expr cls, pattern* patterns, identifier* kwd_attrs, pattern* kwd_patterns)'),
    ('MatchStar', 'pattern', 'ctor', (('name', 'identifier', '?'),), (),
     'MatchStar(identifier? name)'),
    ('MatchAs', 'pattern', 'ctor', (('pattern', 'pattern', '?'), ('name', 'identifier', '?')), (),
     'MatchAs(pattern? pattern, identifier? name)'),
    ('MatchOr', 'pattern', 'ctor', (('patterns', 'pattern', '*'),), (),
     'MatchOr(pattern* patterns)'),
    ('type_ignore', None, 'sum', (), (),
     'type_ignore = TypeIgnore(int lineno, string tag)'),
    ('TypeIgnore', 'type_ignore', 'ctor', (('lineno', 'int', ''), ('tag', 'string', '')), (),
     'TypeIgnore(int lineno, string tag)'),
    ('type_param', None, 'sum', (), (('lineno', 'int', ''), ('col_offset', 'int', ''), ('end_lineno', 'int', ''), ('end_col_offset', 'int', '')),
     'type_param = TypeVar(identifier name, expr? bound, expr? default_value)\n           | ParamSpec(identifier name, expr? default_value)\n           | TypeVarTuple(identifier name, expr? default_value)'),
    ('TypeVar', 'type_param', 'ctor', (('name', 'identifier', ''), ('bound', 'expr', '?'), ('default_value', 'expr', '?')), (),
     'TypeVar(identifier name, expr? bound, expr? default_value)'),
    ('ParamSpec', 'type_param', 'ctor', (('name', 'identifier', ''), ('default_value', 'expr', '?')), (),
     'ParamSpec(identifier name, expr? default_value)'),
    ('TypeVarTuple', 'type_param', 'ctor', (('name', 'identifier', ''), ('default_value', 'expr', '?')), (),
     'TypeVarTuple(identifier name, expr? default_value)'),
)
# END GENERATED

globals().update(_make_types(_NODES))
Load_singleton = Load()
del _NODES
