# unicodedata: the properties of every code point, names, lookups and normalization.
import unicodedata

print(unicodedata.unidata_version, type(unicodedata.ucd_3_2_0).__name__, unicodedata.ucd_3_2_0.unidata_version)
for db in (unicodedata, unicodedata.ucd_3_2_0):
    total = 0
    counts = {}
    for cp in range(0x110000):
        c = chr(cp)
        row = (db.category(c), db.bidirectional(c), db.combining(c), db.mirrored(c), db.east_asian_width(c),
               db.decomposition(c), db.decimal(c, -1), db.digit(c, -1), db.numeric(c, -1.0), db.name(c, ''))
        total = (total * 1000003 + hash(repr(row))) % (1 << 61) if False else (total * 31 + len(repr(row))) % 1000000007
        counts[row[0]] = counts.get(row[0], 0) + 1
    print(total, sorted(counts.items()))
names = ['LATIN SMALL LETTER A', 'HANGUL SYLLABLE GAG', 'CJK UNIFIED IDEOGRAPH-4E00', 'GREEK SMALL LETTER ALPHA',
         'latin small letter a', 'NO SUCH NAME', 'LATIN CAPITAL LETTER A WITH MACRON AND GRAVE', 'LINE FEED',
         'BELL', 'ZERO WIDTH NO-BREAK SPACE', 'BYTE ORDER MARK', 'HANGUL SYLLABLE GGAGG', 'HANGUL SYLLABLE X',
         'CJK UNIFIED IDEOGRAPH-FFFFF', 'EMOJI MODIFIER FITZPATRICK TYPE-3', 'x' * 300, '']
for db in (unicodedata, unicodedata.ucd_3_2_0):
    for n in names:
        try:
            print(repr(db.lookup(n)))
        except KeyError as e:
            print('KeyError', str(e)[:60])
for cp in (0x41, 0xAC00, 0x4E00, 0x10FFFF, 0x20, 0x7F, 0xE000, 0x1F600):
    try:
        print(unicodedata.name(chr(cp)))
    except ValueError as e:
        print('ValueError', e)
samples = ['abc', 'é', 'é', 'ẛ̣', 'ﬁ', 'Å', 'Å', '각', '각',
           'क़', 'Ω', 'ạ̣̇', '\U0001d15e', '', '½', '㌀', 'x̖̀y']
for form in ('NFC', 'NFD', 'NFKC', 'NFKD'):
    for db in (unicodedata, unicodedata.ucd_3_2_0):
        print(form, [ascii(db.normalize(form, s)) for s in samples], [db.is_normalized(form, s) for s in samples])
for bad in [lambda: unicodedata.normalize('X', 'a'), lambda: unicodedata.normalize('NFC', 5), lambda: unicodedata.is_normalized('X', 'a'),
            lambda: unicodedata.category('ab'), lambda: unicodedata.category(5), lambda: unicodedata.decimal('a'),
            lambda: unicodedata.digit('a'), lambda: unicodedata.numeric('a'), lambda: unicodedata.numeric('a', None),
            lambda: unicodedata.name('\x00'), lambda: unicodedata.lookup(5), lambda: unicodedata.UCD(),
            lambda: unicodedata.normalize('NFC'), lambda: unicodedata.category('')]:
    try:
        print(bad())
    except Exception as e:
        print(type(e).__name__, e)
print(unicodedata.decimal('7'), unicodedata.digit('²'), unicodedata.numeric('½'), unicodedata.numeric('一', 'd'),
      unicodedata.east_asian_width('一'), unicodedata.mirrored('('), unicodedata.combining('́'))
print('é'.isidentifier(), 'café'.isidentifier(), 'é'.upper(), ascii('\N{GREEK SMALL LETTER ALPHA}'))
