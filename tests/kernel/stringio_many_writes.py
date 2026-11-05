import io

# Many small writes: linear, as in ElementTree.tostring(encoding='unicode')
s = io.StringIO()
for i in range(60000):
    s.write('<a>%d</a>' % i)
print(len(s.getvalue()), s.tell(), s.getvalue()[-12:])
s.seek(5)
s.write('XY')
print(s.getvalue()[:12], s.tell())
s.seek(0, 2)
s.write('end')
print(s.getvalue()[-8:], s.tell())
s.truncate(10)
s.seek(10)
s.write('!')
print(repr(s.getvalue()), s.tell())

import xml.etree.ElementTree as ET
root = ET.Element('r')
for i in range(3000):
    ET.SubElement(root, 'c', n=str(i)).text = 'v' * 20
out = ET.tostring(root, encoding='unicode')
print(len(out), out[-30:])
