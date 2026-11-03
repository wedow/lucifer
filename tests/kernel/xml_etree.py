import io
import xml.etree.ElementTree as ET
from xml.parsers import expat

doc = ('<?xml version="1.0" encoding="utf-8"?>\n<!-- top -->\n<root xmlns="urn:a" xmlns:b="urn:b" id=\'1\'>\n'
       '  <b:child x="a&amp;b &lt; &#65;&#x42;" y="">text &amp; more<![CDATA[ <raw> ]]>tail</b:child>\n'
       '  <empty/><?pi data here?><!--c--><e a="1"/>café 中\n</root>\n')
root = ET.fromstring(doc)
print(root.tag, root.attrib, [(c.tag, c.attrib, c.text, c.tail) for c in root])
print(ET.tostring(root, encoding='unicode'))
print(ET.tostring(ET.fromstring(doc.encode('utf-8')), encoding='unicode') == ET.tostring(root, encoding='unicode'))
tree = ET.parse(io.BytesIO(doc.encode()))
print([e.tag for e in tree.iter()], tree.getroot().find('{urn:a}empty') is not None)
print(ET.tostring(ET.fromstring('<a><b>1</b><b>2</b></a>')), ET.fromstring('<a>x</a>').text)
for bad in ('<a><b></a>', '<a>', '', '<a></a><b/>', '<a b="1" b="2"/>', '<a>&unknown;</a>', '<a b=1/>', '<p:a/>', '<a><!-- -- --></a>', 'text', '<a>\x00</a>'):
    try:
        ET.fromstring(bad)
        print('parsed', repr(bad))
    except ET.ParseError as e:
        print(repr(bad), e.code)
events = []
p = expat.ParserCreate()
p.StartElementHandler = lambda n, a: events.append(('s', n, a))
p.EndElementHandler = lambda n: events.append(('e', n))
p.CharacterDataHandler = lambda d: events.append(('d', d))
p.buffer_text = True
for chunk in (b'<a x="1">he', b'llo &am', b'p; <b/>wor', b'ld</a>'):
    p.Parse(chunk, False)
p.Parse(b'', True)
merged = []
for event in events:
    if merged and event[0] == 'd' and merged[-1][0] == 'd':
        merged[-1] = ('d', merged[-1][1] + event[1])
    else:
        merged.append(event)
print(merged)
print(expat.ErrorString(2), expat.errors.XML_ERROR_SYNTAX, expat.version_info >= (2, 0))
pull = ET.XMLPullParser(events=('start', 'end'))
pull.feed('<r><a>1</a>')
pull.feed('<b/></r>')
print([(ev, el.tag) for ev, el in pull.read_events()])
