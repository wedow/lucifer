# str.encode and bytes.decode take the codec and handler names as C reads
# them: any spelling codecs.lookup accepts, and a str subclass's text, none
# of its methods run.


class Loud(str):
    def __eq__(self, other):
        raise RuntimeError('__eq__ ran')

    def __hash__(self):
        raise RuntimeError('__hash__ ran')

    def lower(self):
        raise RuntimeError('lower ran')


text = 'caf\xe9 \ud800'
for name in ('utf-8', 'UTF-8', 'utf8', 'U8', 'Utf_8', 'utf 8', 'latin-1', 'Latin1', 'ISO-8859-1', 'iso_8859_1',
             'l1', 'ascii', 'US-ASCII', '646', 'cp65001', Loud('utf-8'), Loud('LATIN-1'), 'utf-16-le', 'cp1252'):
    for errors in ('strict', 'surrogatepass', 'replace', 'ignore', 'backslashreplace', Loud('surrogatepass'),
                   Loud('ignore')):
        try:
            data = text.encode(name, errors)
            print(repr(name), repr(errors), data, ascii(data.decode(name, errors)))
        except (UnicodeError, LookupError) as e:
            print(repr(name), repr(errors), type(e).__name__, e)
print(ascii('x\u20ac'.encode('utf--8', 'strict')), b'x'.decode(' UTF 8 '))
for name in ('utf-9', '', Loud('nope')):
    try:
        'x'.encode(name)
    except LookupError as e:
        print(type(e).__name__, e)
