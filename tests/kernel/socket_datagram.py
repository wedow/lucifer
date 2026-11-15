# Datagram sockets: sendto, recvfrom and recvfrom_into over UDP on IPv4 and
# IPv6 loopback and Unix-domain datagram sockets; send and recv flags; timed
# and nonblocking calls; argument errors.
import errno
import os
import socket
import tempfile


def exchange(family, a_address, b_address):
    with socket.socket(family, socket.SOCK_DGRAM) as a, socket.socket(family, socket.SOCK_DGRAM) as b:
        a.bind(a_address)
        b.bind(b_address)
        print(family.name, a.sendto(b'hello', b.getsockname()), a.sendto(b'there', 0, b.getsockname()))
        data, sender = b.recvfrom(100)
        print(data, sender == a.getsockname())
        buffer = bytearray(10)
        n, sender = b.recvfrom_into(buffer)
        print(n, bytes(buffer[:n]), sender == a.getsockname())
        a.sendto(b'truncated', b.getsockname())
        print(b.recvfrom(4)[0])
        a.sendto(memoryview(b'xyzzy'), b.getsockname())
        print(b.recvfrom(100, socket.MSG_PEEK)[0], b.recv(100))
        a.sendto(b'sized', b.getsockname())
        buffer = bytearray(10)
        print(b.recvfrom_into(buffer, 3)[0], bytes(buffer))
        a.connect(b.getsockname())
        print(a.send(b'connected', socket.MSG_DONTWAIT), b.recv(100, socket.MSG_DONTWAIT))
        try:
            b.recv(100, socket.MSG_DONTWAIT)
        except BlockingIOError as e:
            print('MSG_DONTWAIT', e.errno == errno.EAGAIN)


exchange(socket.AF_INET, ('127.0.0.1', 0), ('127.0.0.1', 0))
exchange(socket.AF_INET6, ('::1', 0), ('::1', 0))
with tempfile.TemporaryDirectory() as d:
    exchange(socket.AF_UNIX, os.path.join(d, 'a'), os.path.join(d, 'b'))
    exchange(socket.AF_UNIX, b'\0lucifer-dgram-a-%d' % os.getpid(), b'\0lucifer-dgram-b-%d' % os.getpid())
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as unnamed, \
            socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as named:
        named.bind(os.path.join(d, 'c'))
        unnamed.sendto(b'anonymous', named.getsockname())
        print(named.recvfrom(100))

# Timed and nonblocking receives, an empty datagram, a refused peer.
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
    s.bind(('127.0.0.1', 0))
    s.settimeout(0.05)
    for call in (lambda: s.recvfrom(10), lambda: s.recvfrom_into(bytearray(10))):
        try:
            call()
        except TimeoutError as e:
            print(type(e).__name__, e)
    s.setblocking(False)
    try:
        s.recvfrom(10)
    except BlockingIOError as e:
        print('nonblocking', e.errno == errno.EAGAIN)
    s.sendto(b'', s.getsockname())
    print(s.recvfrom(10)[0])
    closed = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    closed.bind(('127.0.0.1', 0))
    free = closed.getsockname()
    closed.close()
    s.setblocking(True)
    s.connect(free)
    s.send(b'nobody')
    try:
        s.recv(10)
    except ConnectionRefusedError as e:
        print(type(e).__name__, e.errno == errno.ECONNREFUSED)

# A stream socket's recvfrom has no address.
a, b = socket.socketpair()
a.send(b'pair', socket.MSG_NOSIGNAL)
print(b.recvfrom(10))
a.close()
b.close()

# Argument errors, as Modules/socketmodule.c words them.
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
for call in (lambda: s.sendto(b'x'), lambda: s.sendto(b'x', 0, ('127.0.0.1', 1), 4),
             lambda: s.sendto(b'x', None), lambda: s.sendto(b'x', ('127.0.0.1', 70000)),
             lambda: s.sendto('x', ('127.0.0.1', 1)), lambda: s.recvfrom(-1),
             lambda: s.recvfrom_into(bytearray(2), -1), lambda: s.recvfrom_into(bytearray(2), 3),
             lambda: s.recvfrom_into(b'xx'), lambda: s.bind(('127.0.0.1', -1)),
             lambda: s.connect('nowhere')):
    try:
        call()
    except (TypeError, ValueError, OverflowError) as e:
        print(type(e).__name__, e)
s.close()
try:
    s.sendto(b'x', ('127.0.0.1', 1))
except OSError as e:
    print('closed', e.errno == errno.EBADF)
s6 = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
try:
    s6.sendto(b'x', '::1')
except TypeError as e:
    print(type(e).__name__, e)
s6.close()

# getnameinfo, which asyncio's datagram endpoints use.
for args in [(('127.0.0.1', 80), 0), (('127.0.0.1', 80), socket.NI_NUMERICHOST | socket.NI_NUMERICSERV),
             (('::1', 53, 0, 0), socket.NI_NUMERICHOST), (('::1', 53), socket.NI_NUMERICSERV | socket.NI_DGRAM),
             (['127.0.0.1', 80], 0), (('127.0.0.1', '80'), 0), ((b'127.0.0.1', 80), 0), (('127.0.0.1',), 0),
             (('127.0.0.1', 80, 0), 0), (('::1', 1, 0x100000), 0), (('nonsense host', 1), 0),
             (('127.0.0.1', 70000), socket.NI_NUMERICHOST), (('::1', 1, -1), 0)]:
    try:
        print(socket.getnameinfo(*args))
    except Exception as e:
        print(type(e).__name__, e)

# An empty Unix-domain address autobinds to an abstract name.
with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as s:
    s.bind(b'')
    name = s.getsockname()
    print(type(name).__name__, name[:1], len(name))
