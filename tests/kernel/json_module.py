import _json
import json
import sys
from collections import OrderedDict

print(_json.make_scanner.__name__, _json.make_encoder.__name__, _json.scanstring.__module__)

# encode_basestring and encode_basestring_ascii.
samples = ['', 'plain', 'q"b\\s/', '\b\f\n\r\t\x00\x1f\x7f', 'caf\xe9', '€\ud800', '\U0001d120x',
           'a' * 50 + '\n', type('S', (str,), {})('sub')]
for s in samples:
    print(ascii(_json.encode_basestring(s)), ascii(_json.encode_basestring_ascii(s)))
for f in (_json.encode_basestring, _json.encode_basestring_ascii):
    try:
        f(b'x')
    except TypeError as e:
        print(e)


def show(f, *args):
    try:
        print(ascii(f(*args)))
    except Exception as e:
        print(type(e).__name__, ascii(str(e)), getattr(e, 'pos', None), getattr(e, '__notes__', None))


# scanstring.
for doc, end in (('"abc"', 1), ('"a\\"b\\\\c\\/\\b\\f\\n\\r\\t"', 1), ('"\\u00e9\\u20AC"', 1),
                 ('"\\ud834\\udd20 \\ud834x \\udd20"', 1), ('"\\ud834\\u0041"', 1), ('x"z\U0001d120x"', 2),
                 ('"abc', 1), ('"a\\', 1), ('"a\\q"', 1), ('"\\u12"', 1), ('"\\u12g4"', 1),
                 ('"\\ud834\\u12g4"', 1), ('"a\nb"', 1), ('"abc"', 9), ('"abc"', -1), ('"abc"', 5),
                 # A complete escape at the end leaves the string unterminated; a cut one is invalid.
                 ('"\\u0041', 1), ('"\\u004', 1), ('"\\ud834\\udd1e', 1), ('"\\ud834\\udd1', 1),
                 ('"\\ud834\\u', 1), ('"\\ud834\\', 1), ('"\\ud834\\u0041', 1), ('"\\ud834\\"', 1)):
    show(_json.scanstring, doc, end)
show(_json.scanstring, '"a\nb\x00"', 1, False)
show(_json.scanstring, b'"a"', 1)
show(_json.scanstring, '"a"', sys.maxsize + 1)
show(_json.scanstring, '"a"', 1.0)


# make_scanner.
class Ctx:
    strict = True
    object_hook = None
    object_pairs_hook = None
    parse_float = float
    parse_int = int
    parse_constant = {'NaN': 'nan!', 'Infinity': 'inf!', '-Infinity': '-inf!'}.__getitem__
    memo = None


scan = _json.make_scanner(Ctx())
print(scan.strict, scan.parse_int is int, scan.object_hook)
for doc in ('{"a": [1, -2, 3.5, 1e3, 2E-2, -0.0, 0, true, false, null], "b" : {"c": "d"}}',
            ' [] ', '[ ]', '{ }', '[1,2', '[1,]', '{"a":1,}', '{"a" 1}', '{a:1}', '{"a":1 "b":2}', '[1 2]',
            'NaN', 'Infinity', '-Infinity', '-', '-x', '01', '1.', '1.e5', '1e', '1e+', '12abc', 'nul', 'tru',
            'fals', '"\\u00e9"', '[', '{', '{"a":', '', '"x', '١', '[1, {"k": [true, {"z": []}]}]'):
    for idx in (0, 1):
        show(scan, doc, idx)
show(scan, '[1]', -1)
show(scan, b'[1]', 0)
show(scan, '[1]', 10)
try:
    scan('x', 0)
except StopIteration as e:
    print('StopIteration', e.value)

ctx = Ctx()
ctx.object_hook = lambda d: ('hook', sorted(d.items()))
show(_json.make_scanner(ctx), '{"b": 1, "a": {"c": 2}}', 0)
ctx.object_pairs_hook = lambda pairs: ('pairs', pairs)
show(_json.make_scanner(ctx), '{"b": 1, "a": {}}', 0)
ctx = Ctx()
ctx.parse_int = lambda s: ('int', s)
ctx.parse_float = lambda s: ('float', s)
ctx.strict = 0
show(_json.make_scanner(ctx), '[1, -12, 1.5, 3e4, "a\tb"]', 0)
ctx.parse_int = float
show(_json.make_scanner(ctx), '[10]', 0)
ctx.parse_float = int
show(_json.make_scanner(ctx), '[1.5]', 0)
show(scan, '[-12345678901234567, 123456789012345678, 1234567890123456789, -1234567890123456789, -0, 0.5e-3, -7]', 0)
keys = _json.make_scanner(Ctx())('[{"key": 1}, {"key": 2}]', 0)[0]
print(list(keys[0])[0] is list(keys[1])[0])
show(_json.make_scanner, 1)
show(scan, '9' * 5000, 0)

# make_encoder.
for indent in (None, '  ', '\t'):
    for sort_keys in (False, True):
        enc = _json.make_encoder({}, repr, _json.encode_basestring_ascii, indent, ': ', ', ', sort_keys, False, True)
        doc = {'z': [1, 2.5, None, True, False, 'x€'], 'a': {}, 'b': [], 3: (4, [5, {}]), 1.5: 'f',
               None: 0, True: 1, 'n': {'deep': [[], {}, [{}]]}}
        show(enc, doc, 0)
        show(enc, doc, 2)
        show(enc, doc, -3)
        show(enc, 'just a string', 0)
enc = _json.make_encoder(None, str, _json.encode_basestring, '\t', ': ', ', ', False, False, False)
show(enc, ['spam', {'ham': 'eggs'}], 3)
show(enc, ['spam'], 3.0)
show(enc, [float('inf')], 0)
show(enc, {float('nan'): 1}, 0)
enc = _json.make_encoder(None, str, _json.encode_basestring, None, ':', ',', False, False, True)
show(enc, [float('inf'), float('-inf'), float('nan'), 1e300, 0.1, -0.0, 2 ** 70], 0)


class MyInt(int):
    def __repr__(self):
        return 'bad'


class MyFloat(float):
    def __repr__(self):
        return 'bad'


class MyDict(dict):
    def items(self):
        return [('x', 1), ('y', 2)]


class BadItems(dict):
    def items(self):
        return [('x', 1, 2)]


show(enc, [MyInt(5), MyFloat(1.5), {MyInt(7): MyFloat(2.5)}, MyDict(q=1), OrderedDict([('b', 1), ('a', 2)])], 0)
show(enc, BadItems(a=1), 0)
show(enc, {b'k': 1}, 0)
show(_json.make_encoder(None, str, _json.encode_basestring, None, ':', ',', False, True, True), {b'k': 1, 'v': 2}, 0)
show(_json.make_encoder(None, str, _json.encode_basestring, ' ', ':', ',', False, True, True), {b'k': 1}, 0)
show(enc, {'a': 1, 2: 3}, 0)
show(_json.make_encoder(None, str, _json.encode_basestring, None, ':', ',', True, False, True), {'a': 1, 2: 3}, 0)

circ = []
circ.append(circ)
d = {}
d['self'] = d
markers = {}
menc = _json.make_encoder(markers, lambda o: [o] if isinstance(o, complex) else repr(o), _json.encode_basestring,
                          None, ':', ',', False, False, True)
show(menc, circ, 0)
show(menc, [d], 0)
show(menc, [1j], 0)
show(menc, [object, ...], 0)
print(len(markers))


def bad_default(o):
    raise TypeError('no ' + type(o).__name__)


show(_json.make_encoder(None, bad_default, _json.encode_basestring, None, ':', ',', False, False, True),
     {'a': [1, (2, set())]}, 0)
show(_json.make_encoder(None, str, lambda s: None, None, ':', ',', False, False, True), {'a': 1}, 0)
show(_json.make_encoder(None, str, lambda s: 1 / 0, None, ':', ',', False, False, True), 'x', 0)
show(_json.make_encoder, 1, None, None, None, ': ', ', ', False, False, False)
show(_json.make_encoder, None, None, None, None, 1, ', ', False, False, False)
show(_json.make_encoder, None, None, None)
e = _json.make_encoder(markers=None, default=str, encoder=_json.encode_basestring, indent=None,
                       key_separator=':', item_separator=',', sort_keys=1, skipkeys=0, allow_nan=[])
print(e.sort_keys, e.skipkeys, e.indent, e.key_separator, e.item_separator, e.markers, e.default is str)
# default() shrinks the list being encoded: the encoder stops at its new end.
lst = [object()] * 10
shrink = _json.make_encoder(None, lambda o: lst.pop() and None, _json.encode_basestring, None, ', ', ', ',
                            False, False, True)
show(shrink, lst, 0)

# Through the json package, which takes the accelerators.
print(json.decoder.scanstring is _json.scanstring, json.scanner.make_scanner is _json.make_scanner,
      json.encoder.c_make_encoder is _json.make_encoder)
data = {'name': 'caf\xe9 €', 'items': [{'id': i, 'ok': i % 2 == 0, 'v': i / 4} for i in range(3)]}
for kwargs in ({}, {'ensure_ascii': False}, {'indent': 2, 'sort_keys': True}, {'separators': (',', ':')}):
    text = json.dumps(data, **kwargs)
    print(ascii(text), json.loads(text) == data)
show(json.loads, '{"a": 1, "a": 2, "b": [1, 2, 3}')
show(json.loads, '[1, 2] x')
print(json.loads('{"a": 1.5, "b": 2}', parse_float=str, object_pairs_hook=list))
