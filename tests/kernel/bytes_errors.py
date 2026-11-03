def t(f):
    try:
        print(repr(f()))
    except BaseException as e:
        print(type(e).__name__, e)
b = b'abc'
ba = bytearray(b'abc')
class Map:
    def __getitem__(self, k):
        return 7


tests = [lambda:b.find(), lambda:b.find(1,2,3,4), lambda:b.find('a'), lambda:b.find(256), lambda:b.find(b'a','x'),
 lambda:b.count(), lambda:b.startswith(1), lambda:b.startswith((b'a',1)), lambda:b.split(1), lambda:b.split(b''), lambda:b.split(maxsplit='x'), lambda:b.split(1,2,3),
 lambda:b.replace(b'a'), lambda:b.replace('a','b'), lambda:b.strip(1), lambda:b.join(1), lambda:b.join([1]), lambda:b.join(['a']),
 lambda:b.center(), lambda:b.center(5,'a'), lambda:b.center(5,b'ab'), lambda:b.ljust('a'), lambda:b.zfill('a'),
 lambda:b.hex('ab'), lambda:b.hex(1), lambda:b.hex(b'\xff'), lambda:bytes.fromhex(1), lambda:bytes.fromhex('0'), lambda:bytes.fromhex('zz'),
 lambda:b.decode(1), lambda:b.decode('x'), lambda:b.decode(errors=1), lambda:b'\xff'.decode(),
 lambda:b.translate(b'a'), lambda:b.translate(1), lambda:bytes.maketrans(b'a',b'bc'), lambda:bytes.maketrans(1,2),
 lambda:b+1, lambda:b*'a', lambda:b*-1, lambda:b[5], lambda:b['a'], lambda:b[1.0], lambda:1+b, lambda:b%1, lambda:b'%d'%'a', lambda:b'%s'%1,lambda:b'%c'%300, lambda:b'%z'%1, lambda:b'%'%(), lambda:b'%d %d'%(1,), lambda:b'%d'%(1,2),
 lambda:b'%(a)d'%Map(), lambda:b'%(a)d'%1, lambda:b'%s'%b'x', lambda:b'%a'%'é', lambda:b'%r'%'x', lambda:b'%5.2s|%-5s|'%(b'abc',b'x'), lambda: b'%x %X %o %#x %05d %+d'%(255,255,8,255,3,3), lambda:b'%c %c'%(65,b'B'),
 lambda:bytes('a'), lambda:bytes(1,'a'), lambda:bytes('a',1), lambda:bytes('a','utf-8',1), lambda:bytes(-1), lambda:bytes([256]), lambda:bytes(['a']), lambda:bytes(1.5), lambda:bytes(None), lambda:bytes(errors='x'),
 lambda:bytearray('a'), lambda:bytearray(-1), lambda:bytearray([256]), lambda:bytearray(1.5), lambda:bytearray(1,2),
 lambda:ba.pop(5), lambda:bytearray().pop(), lambda:ba.remove(9), lambda:ba.append(256), lambda:ba.append('a'), lambda:ba.insert('a',1), lambda:ba.extend(1), lambda:ba.extend([256]), lambda:ba.extend('a'),
 lambda:ba.__setitem__(0,256), lambda:ba.__setitem__(0,'a'), lambda:ba.__setitem__(5,1), lambda:ba.__setitem__(slice(0,2,2),b'abc'), lambda:ba.__setitem__(slice(None,None,2),b'a'), lambda:ba.__delitem__(5),
 lambda:bytes.__new__(int), lambda:hash(ba), lambda:ba.resize(-1), lambda:b.index(b'z'), lambda:b.rindex(b'z'), lambda:b.partition(1), lambda:b.partition(b''), lambda:b.removeprefix(1), lambda:b.expandtabs('a'),
 lambda:b.splitlines(1,2), lambda:b.upper(1), lambda:b.isalpha(1), lambda:b.fromhex(b'6162'), lambda:int.from_bytes(1), lambda:int.from_bytes([1,2]), lambda:int.from_bytes(bytearray(b'\x01\x00')),
 lambda:int(b'12'), lambda:int(bytearray(b'12')), lambda:int(memoryview(b'12')), lambda:str(ba,'ascii'), lambda:str(memoryview(b'ab'),'ascii'), lambda:ba*2, lambda:ba+b'x', lambda: ba+'x', lambda:b+ba,lambda:b==ba, lambda:b<1, lambda:b'a'<b'b', lambda: b==1,
 lambda:repr(b'\x00\'"\\\n\t\xff'), lambda:repr(b"'"), lambda:repr(b'\'"'), lambda:repr(bytearray(b"'")), lambda:str(b), lambda:b.__bytes__(), lambda:bytes(b), lambda:ba.copy(), lambda:ba.__alloc__(), lambda:ba.__reduce__(), lambda:b.__getnewargs__(),
 lambda:b.__sizeof__(), lambda:ba.__sizeof__(), lambda:bytearray(b'abc').__sizeof__(), lambda:bytearray(10).__alloc__(), lambda:bytearray.fromhex('6162'), lambda: bytearray(b'x')==bytearray(b'x'),
]
for f in tests:
    t(f)
