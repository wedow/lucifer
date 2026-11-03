import resource
import socket

soft, hard = resource.getrlimit(resource.RLIMIT_NOFILE)
print(soft > 0, hard == -1 or hard >= soft, resource.RLIM_INFINITY, resource.getpagesize())
resource.setrlimit(resource.RLIMIT_CORE, (0, hard if False else resource.getrlimit(resource.RLIMIT_CORE)[1]))
print(resource.getrlimit(resource.RLIMIT_CORE)[0])
usage = resource.getrusage(resource.RUSAGE_SELF)
print(type(usage).__name__, len(usage), usage.ru_maxrss > 0, usage.ru_utime >= 0)
try:
    resource.getrlimit(99)
except ValueError as e:
    print(e)
print(socket.AF_INET, socket.SOCK_STREAM, socket.AF_INET.name, socket.SOL_SOCKET, socket.has_ipv6, repr(socket.SocketKind.SOCK_DGRAM))
print(socket.htons(1), socket.ntohl(1), socket.inet_aton('1.2.3.4'), socket.inet_ntoa(b'\x01\x02\x03\x04'), socket.inet_pton(socket.AF_INET, '9.8.7.6'))
print(isinstance(socket.gethostname(), str), socket.getdefaulttimeout())
import asyncio
print(asyncio.iscoroutinefunction(print))
