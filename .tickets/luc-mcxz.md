---
id: luc-mcxz
status: open
deps: []
links: []
created: 2026-11-11T02:46:59Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# binascii/quopri: quoted-printable encoding of tab and CR, '==' decoding, uu trailing garbage, hexlify group overflow

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): textlibs_diff, about 10 cases over seeds 1 and 2 at 1,000 cases each.

```python
import binascii
print(binascii.b2a_qp(b"a\t\rb"))
print(binascii.b2a_qp(b"a" * 70 + b"\n" + b"ba bbbb"))
print(binascii.b2a_qp(b"a" * 74 + b" ab"))
print(binascii.b2a_qp(b"x \t\r\n\t "))
```
CPython 3.14t:
```
b'a\t\rb'
b'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\nba bbbb'
b'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa =\nab'
b'x =09\r\n\t=20'
```
Lucifer:
```
b'a=09\rb'
b'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\nba bbbb'
b'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa =\nab'
b'x =09\r\n\t=20'
```

```python
import quopri, io
for d in (b"a \n_ \n", b"a\t\r\n", b"x \r"):
    for hdr in (False, True):
        o = io.BytesIO(); quopri.encode(io.BytesIO(d), o, False, hdr); print(d, hdr, o.getvalue())
```
CPython 3.14t:
```
b'a \n_ \n' False b'a=20\n_=20\n'
b'a \n_ \n' True b'a_\n=5F_\n'
b'a\t\r\n' False b'a=09\r\n'
b'a\t\r\n' True b'a=09\r\n'
b'x \r' False b'x \r'
b'x \r' True b'x_\r'
```
Lucifer:
```
b'a \n_ \n' False b'a=20\n_=20\n'
b'a \n_ \n' True b'a=20\n=5F=20\n'
b'a\t\r\n' False b'a=09\r\n'
b'a\t\r\n' True b'a=09\r\n'
b'x \r' False b'x=20\r'
b'x \r' True b'x=20\r'
```

```python
import binascii
for d in (b"===3Daa", b"==", b"=3D=", b"a==A", b"=\r\n=", b"====="):
    for h in (False, True):
        print(d, h, binascii.a2b_qp(d, header=h))
```
CPython 3.14t:
```
b'===3Daa' False b'==aa'
b'===3Daa' True b'==aa'
b'==' False b'='
b'==' True b'='
b'=3D=' False b'='
b'=3D=' True b'='
b'a==A' False b'a=A'
b'a==A' True b'a=A'
b'=\r\n=' False b''
b'=\r\n=' True b''
b'=====' False b'=='
b'=====' True b'=='
```
Lucifer:
```
b'===3Daa' False b'===aa'
b'===3Daa' True b'===aa'
b'==' False b'='
b'==' True b'='
b'=3D=' False b'='
b'=3D=' True b'='
b'a==A' False b'a==A'
b'a==A' True b'a==A'
b'=\r\n=' False b''
b'=\r\n=' True b''
b'=====' False b'===='
b'=====' True b'===='
```

```python
import binascii
for d in (b"#86)C\n  \nA", b"!  \x7f\n", b"\x20\x20", b"!80  X\n"):
    try: print(binascii.a2b_uu(d))
    except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
Error Trailing garbage
Error Trailing garbage
b''
Error Trailing garbage
```
Lucifer:
```
Error Illegal char
Error Illegal char
b''
b'a'
```

```python
import binascii
try: print(binascii.hexlify(b"abcd", "a", 2**31))
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
OverflowError Python int too large to convert to C int
```
Lucifer:
```
b'61626364'
```

```python
import binascii
for f, a in ((binascii.a2b_base64, (b"=\n", b"HH")), (binascii.b2a_base64, (b"abc", True, 16, True))):
    try: print(f(*a))
    except Exception as e: print(type(e).__name__, e)
for f, kw in ((binascii.a2b_base64, dict(padded=False)), (binascii.b2a_base64, dict(padded=True, wrapcol=16))):
    try: print(f(b"abc", **kw))
    except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError a2b_base64() takes exactly 1 positional argument (2 given)
TypeError b2a_base64() takes at most 2 arguments (4 given)
TypeError a2b_base64() got an unexpected keyword argument 'padded'
TypeError b2a_base64() takes at most 2 arguments (3 given)
```
Lucifer:
```
TypeError a2b_base64() takes 1 positional argument but 2 were given
TypeError b2a_base64() takes 1 positional argument but 4 were given
TypeError a2b_base64() got an unexpected keyword argument 'padded'
TypeError b2a_base64() got an unexpected keyword argument 'padded'
```


