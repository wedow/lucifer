"""_socket (Modules/socketmodule.c): the constants, the error classes, the
address conversion functions, and stream sockets over AF_INET, AF_INET6 and
AF_UNIX: create, bind, listen, accept, connect, options, and blocking, timed
and nonblocking I/O. Addresses cross into the runtime as raw sockaddr bytes,
packed and unpacked here. Datagram calls (sendto, recvfrom) and recv/send flags
are not supported yet."""

import errno as _errno
import _lucifer
import posix as _posix
import select as _select

error = OSError


class herror(OSError):
    """Error from gethostbyname etc."""


class gaierror(OSError):
    """Error from getaddrinfo etc."""


timeout = TimeoutError
has_ipv6 = True
SocketType = None

_CONSTANTS = {
    'AF_ALG': 38, 'AF_APPLETALK': 5, 'AF_ASH': 18, 'AF_ATMPVC': 8, 'AF_ATMSVC': 20,
    'AF_AX25': 3, 'AF_BLUETOOTH': 31, 'AF_BRIDGE': 7, 'AF_CAN': 29, 'AF_ECONET': 19,
    'AF_INET': 2, 'AF_INET6': 10, 'AF_IPX': 4, 'AF_IRDA': 23, 'AF_KEY': 15, 'AF_LLC': 26,
    'AF_NETBEUI': 13, 'AF_NETLINK': 16, 'AF_NETROM': 6, 'AF_PACKET': 17, 'AF_PPPOX': 24,
    'AF_QIPCRTR': 42, 'AF_RDS': 21, 'AF_ROSE': 11, 'AF_ROUTE': 16, 'AF_SECURITY': 14,
    'AF_SNA': 22, 'AF_TIPC': 30, 'AF_UNIX': 1, 'AF_UNSPEC': 0, 'AF_VSOCK': 40,
    'AF_WANPIPE': 25, 'AF_X25': 9, 'AI_ADDRCONFIG': 32, 'AI_ALL': 16, 'AI_CANONNAME': 2,
    'AI_NUMERICHOST': 4, 'AI_NUMERICSERV': 1024, 'AI_PASSIVE': 1, 'AI_V4MAPPED': 8,
    'EAI_ADDRFAMILY': -9, 'EAI_AGAIN': -3, 'EAI_BADFLAGS': -1, 'EAI_FAIL': -4,
    'EAI_FAMILY': -6, 'EAI_MEMORY': -10, 'EAI_NODATA': -5, 'EAI_NONAME': -2,
    'EAI_OVERFLOW': -12, 'EAI_SERVICE': -8, 'EAI_SOCKTYPE': -7, 'EAI_SYSTEM': -11,
    'INADDR_ALLHOSTS_GROUP': 3758096385, 'INADDR_ANY': 0, 'INADDR_BROADCAST': 4294967295,
    'INADDR_LOOPBACK': 2130706433, 'INADDR_MAX_LOCAL_GROUP': 3758096639,
    'INADDR_NONE': 4294967295, 'INADDR_UNSPEC_GROUP': 3758096384, 'IPPROTO_AH': 51,
    'IPPROTO_DSTOPTS': 60, 'IPPROTO_EGP': 8, 'IPPROTO_ESP': 50, 'IPPROTO_FRAGMENT': 44,
    'IPPROTO_GRE': 47, 'IPPROTO_HOPOPTS': 0, 'IPPROTO_ICMP': 1, 'IPPROTO_ICMPV6': 58,
    'IPPROTO_IDP': 22, 'IPPROTO_IGMP': 2, 'IPPROTO_IP': 0, 'IPPROTO_IPIP': 4,
    'IPPROTO_IPV6': 41, 'IPPROTO_MPTCP': 262, 'IPPROTO_NONE': 59, 'IPPROTO_PIM': 103,
    'IPPROTO_PUP': 12, 'IPPROTO_RAW': 255, 'IPPROTO_ROUTING': 43, 'IPPROTO_RSVP': 46,
    'IPPROTO_SCTP': 132, 'IPPROTO_TCP': 6, 'IPPROTO_TP': 29, 'IPPROTO_UDP': 17,
    'IPPROTO_UDPLITE': 136, 'IPV6_CHECKSUM': 7, 'IPV6_DONTFRAG': 62, 'IPV6_DSTOPTS': 59,
    'IPV6_HOPLIMIT': 52, 'IPV6_HOPOPTS': 54, 'IPV6_JOIN_GROUP': 20, 'IPV6_LEAVE_GROUP': 21,
    'IPV6_MULTICAST_HOPS': 18, 'IPV6_MULTICAST_IF': 17, 'IPV6_MULTICAST_LOOP': 19,
    'IPV6_NEXTHOP': 9, 'IPV6_PATHMTU': 61, 'IPV6_PKTINFO': 50, 'IPV6_RECVDSTOPTS': 58,
    'IPV6_RECVERR': 25, 'IPV6_RECVHOPLIMIT': 51, 'IPV6_RECVHOPOPTS': 53,
    'IPV6_RECVPATHMTU': 60, 'IPV6_RECVPKTINFO': 49, 'IPV6_RECVRTHDR': 56,
    'IPV6_RECVTCLASS': 66, 'IPV6_RTHDR': 57, 'IPV6_RTHDRDSTOPTS': 55, 'IPV6_RTHDR_TYPE_0': 0,
    'IPV6_TCLASS': 67, 'IPV6_UNICAST_HOPS': 16, 'IPV6_V6ONLY': 26, 'IP_ADD_MEMBERSHIP': 35,
    'IP_ADD_SOURCE_MEMBERSHIP': 39, 'IP_BIND_ADDRESS_NO_PORT': 24, 'IP_BLOCK_SOURCE': 38,
    'IP_DEFAULT_MULTICAST_LOOP': 1, 'IP_DEFAULT_MULTICAST_TTL': 1, 'IP_DROP_MEMBERSHIP': 36,
    'IP_DROP_SOURCE_MEMBERSHIP': 40, 'IP_FREEBIND': 15, 'IP_HDRINCL': 3,
    'IP_MAX_MEMBERSHIPS': 20, 'IP_MULTICAST_IF': 32, 'IP_MULTICAST_LOOP': 34,
    'IP_MULTICAST_TTL': 33, 'IP_OPTIONS': 4, 'IP_PKTINFO': 8, 'IP_RECVERR': 11,
    'IP_RECVOPTS': 6, 'IP_RECVORIGDSTADDR': 20, 'IP_RECVRETOPTS': 7, 'IP_RECVTOS': 13,
    'IP_RECVTTL': 12, 'IP_RETOPTS': 7, 'IP_TOS': 1, 'IP_TRANSPARENT': 19, 'IP_TTL': 2,
    'IP_UNBLOCK_SOURCE': 37, 'MSG_CMSG_CLOEXEC': 1073741824, 'MSG_CONFIRM': 2048,
    'MSG_CTRUNC': 8, 'MSG_DONTROUTE': 4, 'MSG_DONTWAIT': 64, 'MSG_EOR': 128,
    'MSG_ERRQUEUE': 8192, 'MSG_FASTOPEN': 536870912, 'MSG_MORE': 32768, 'MSG_NOSIGNAL': 16384,
    'MSG_OOB': 1, 'MSG_PEEK': 2, 'MSG_TRUNC': 32, 'MSG_WAITALL': 256, 'NI_DGRAM': 16,
    'NI_IDN': 32, 'NI_MAXHOST': 1025, 'NI_MAXSERV': 32, 'NI_NAMEREQD': 8, 'NI_NOFQDN': 4,
    'NI_NUMERICHOST': 1, 'NI_NUMERICSERV': 2, 'SCM_CREDENTIALS': 2, 'SCM_J1939_DEST_ADDR': 1,
    'SCM_J1939_DEST_NAME': 2, 'SCM_J1939_ERRQUEUE': 4, 'SCM_J1939_PRIO': 3, 'SCM_RIGHTS': 1,
    'SHUT_RD': 0, 'SHUT_RDWR': 2, 'SHUT_WR': 1, 'SOCK_CLOEXEC': 524288, 'SOCK_DGRAM': 2,
    'SOCK_NONBLOCK': 2048, 'SOCK_RAW': 3, 'SOCK_RDM': 4, 'SOCK_SEQPACKET': 5, 'SOCK_STREAM': 1,
    'SOL_ALG': 279, 'SOL_BLUETOOTH': 274, 'SOL_CAN_BASE': 100, 'SOL_CAN_RAW': 101,
    'SOL_HCI': 0, 'SOL_IP': 0, 'SOL_L2CAP': 6, 'SOL_RDS': 276, 'SOL_RFCOMM': 18, 'SOL_SCO': 17,
    'SOL_SOCKET': 1, 'SOL_TCP': 6, 'SOL_TIPC': 271, 'SOL_UDP': 17, 'SOMAXCONN': 4096,
    'SO_ACCEPTCONN': 30, 'SO_BINDTODEVICE': 25, 'SO_BINDTOIFINDEX': 62, 'SO_BROADCAST': 6,
    'SO_DEBUG': 1, 'SO_DOMAIN': 39, 'SO_DONTROUTE': 5, 'SO_ERROR': 4, 'SO_INCOMING_CPU': 49,
    'SO_J1939_ERRQUEUE': 4, 'SO_J1939_FILTER': 1, 'SO_J1939_PROMISC': 2,
    'SO_J1939_SEND_PRIO': 3, 'SO_KEEPALIVE': 9, 'SO_LINGER': 13, 'SO_MARK': 36,
    'SO_OOBINLINE': 10, 'SO_ORIGINAL_DST': 80, 'SO_PASSCRED': 16, 'SO_PASSSEC': 34,
    'SO_PEERCRED': 17, 'SO_PEERSEC': 31, 'SO_PRIORITY': 12, 'SO_PROTOCOL': 38, 'SO_RCVBUF': 8,
    'SO_RCVLOWAT': 18, 'SO_RCVTIMEO': 20, 'SO_REUSEADDR': 2, 'SO_REUSEPORT': 15,
    'SO_SNDBUF': 7, 'SO_SNDLOWAT': 19, 'SO_SNDTIMEO': 21, 'SO_TYPE': 3,
    'SO_VM_SOCKETS_BUFFER_MAX_SIZE': 2, 'SO_VM_SOCKETS_BUFFER_MIN_SIZE': 1,
    'SO_VM_SOCKETS_BUFFER_SIZE': 0, 'TCP_CC_INFO': 26, 'TCP_CONGESTION': 13, 'TCP_CORK': 3,
    'TCP_DEFER_ACCEPT': 9, 'TCP_FASTOPEN': 23, 'TCP_FASTOPEN_CONNECT': 30,
    'TCP_FASTOPEN_KEY': 33, 'TCP_FASTOPEN_NO_COOKIE': 34, 'TCP_INFO': 11, 'TCP_INQ': 36,
    'TCP_KEEPCNT': 6, 'TCP_KEEPIDLE': 4, 'TCP_KEEPINTVL': 5, 'TCP_LINGER2': 8, 'TCP_MAXSEG': 2,
    'TCP_MD5SIG': 14, 'TCP_MD5SIG_EXT': 32, 'TCP_NODELAY': 1, 'TCP_NOTSENT_LOWAT': 25,
    'TCP_QUEUE_SEQ': 21, 'TCP_QUICKACK': 12, 'TCP_REPAIR': 19, 'TCP_REPAIR_OPTIONS': 22,
    'TCP_REPAIR_QUEUE': 20, 'TCP_REPAIR_WINDOW': 29, 'TCP_SAVED_SYN': 28, 'TCP_SAVE_SYN': 27,
    'TCP_SYNCNT': 7, 'TCP_THIN_DUPACK': 17, 'TCP_THIN_LINEAR_TIMEOUTS': 16,
    'TCP_TIMESTAMP': 24, 'TCP_TX_DELAY': 37, 'TCP_ULP': 31, 'TCP_USER_TIMEOUT': 18,
    'TCP_WINDOW_CLAMP': 10, 'TCP_ZEROCOPY_RECEIVE': 35,
}
globals().update(_CONSTANTS)
_default_timeout = None


def _unsupported(*args, **kwargs):
    raise OSError(_errno.ENOSYS, 'sockets are not supported yet')


class socket(_lucifer.socket_base()):
    """socket(family=AF_INET, type=SOCK_STREAM, proto=0) -> socket object"""
    __slots__ = ()

    def __init__(self, family=-1, type=-1, proto=-1, fileno=None):
        if fileno is None:
            family = AF_INET if family == -1 else family
            type = SOCK_STREAM if type == -1 else type
            proto = 0 if proto == -1 else proto
            fileno = _posix._call('socket', family, type, proto)
        else:
            if not isinstance(fileno, int):
                raise TypeError('an integer is required')
            if fileno < 0:
                raise ValueError('negative file descriptor')
            _posix.fstat(fileno)
            try:
                if family == -1:
                    family = _lucifer.os_getsockopt(fileno, SOL_SOCKET, SO_DOMAIN, 0)
                if type == -1:
                    type = _lucifer.os_getsockopt(fileno, SOL_SOCKET, SO_TYPE, 0)
                if proto == -1:
                    proto = _lucifer.os_getsockopt(fileno, SOL_SOCKET, SO_PROTOCOL, 0)
            except OSError as e:
                if e.errno != _errno.ENOTSOCK:
                    raise
                family, type, proto = (AF_UNIX if family == -1 else family,
                                       SOCK_STREAM if type == -1 else type, 0 if proto == -1 else proto)
        self._fd = fileno
        self._family = family
        self._type = type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC)
        self._proto = proto
        self._timeout = _default_timeout if _posix.get_blocking(fileno) else 0.0
        if type & SOCK_NONBLOCK:
            self._timeout = 0.0
        elif self._timeout is not None and self._timeout > 0:
            _posix.set_blocking(fileno, False)

    def __repr__(self):
        return '<socket object, fd=%d, family=%d, type=%d, proto=%d>' % (
            self._fd, self._family, self._type, self._proto)

    family = property(lambda self: self._family)
    type = property(lambda self: self._type)
    proto = property(lambda self: self._proto)

    def _check(self):
        if self._fd < 0:
            raise OSError(_errno.EBADF, 'Bad file descriptor')
        return self._fd

    def _wait(self, events):
        """Waits up to the timeout for the descriptor; a timeout of 0 or None does not wait."""
        if self._timeout and not _select._wait([(self._fd, events)], self._timeout):
            raise timeout('timed out')

    def __del__(self):
        if self._fd >= 0:
            self.close()

    def close(self):
        fd, self._fd = self._fd, -1
        if fd >= 0:
            _posix.close(fd)

    def detach(self):
        fd, self._fd = self._fd, -1
        return fd

    def fileno(self):
        return self._fd

    def gettimeout(self):
        return self._timeout

    def settimeout(self, value):
        if value is not None:
            value = float(value)
            if value < 0:
                raise ValueError('Timeout value out of range')
        self._timeout = value
        _posix.set_blocking(self._check(), value is None)

    def setblocking(self, flag):
        self.settimeout(None if flag else 0.0)

    def getblocking(self):
        self._check()
        return self._timeout is None

    timeout = property(gettimeout)

    def recv(self, bufsize, flags=0, /):
        if bufsize < 0:
            raise ValueError('negative buffersize in recv')
        fd = self._check()
        _no_flags(flags)
        self._wait(_POLLIN)
        return _posix.read(fd, bufsize)

    def recv_into(self, buffer, nbytes=0, flags=0):
        view = memoryview(buffer).cast('B')
        if nbytes < 0:
            raise ValueError('negative buffersize in recv_into')
        if nbytes > len(view):
            raise ValueError('buffer too small for requested bytes')
        data = self.recv(nbytes or len(view), flags)
        view[:len(data)] = data
        return len(data)

    def send(self, data, flags=0, /):
        fd = self._check()
        _no_flags(flags)
        self._wait(_POLLOUT)
        return _posix.write(fd, data)

    def bind(self, address, /):
        _lucifer.os_sockaddr('bind', self._check(), _pack(self._family, address))

    def listen(self, backlog=128, /):
        _posix._call('listen', self._check(), max(0, backlog))

    def _accept(self):
        fd = self._check()
        self._wait(_POLLIN)
        new, address = _lucifer.os_sockaddr('accept', fd)
        return new, _unpack(address)

    def connect(self, address, /):
        fd = self._check()
        try:
            _lucifer.os_sockaddr('connect', fd, _pack(self._family, address))
        except BlockingIOError as e:
            if not self._timeout or e.errno != _errno.EINPROGRESS:
                raise
            self._wait(_POLLOUT)
            error = _lucifer.os_getsockopt(fd, SOL_SOCKET, SO_ERROR, 0)
            if error:
                raise OSError(error, _posix.strerror(error)) from None

    def connect_ex(self, address, /):
        try:
            self.connect(address)
        except OSError as e:
            if e.errno is None:
                raise
            return e.errno
        return 0

    def getsockname(self):
        return _unpack(_lucifer.os_sockaddr('getsockname', self._check()))

    def getpeername(self):
        return _unpack(_lucifer.os_sockaddr('getpeername', self._check()))

    def setsockopt(self, level, optname, value, optlen=None, /):
        if value is None:
            value = bytes(optlen)
        elif not isinstance(value, int):
            value = bytes(value)
        _lucifer.os_setsockopt(self._check(), level, optname, value)

    def getsockopt(self, level, optname, buflen=0, /):
        return _lucifer.os_getsockopt(self._check(), level, optname, buflen)

    def sendall(self, data, flags=0, /):
        view = memoryview(data).cast('B')
        while view:
            view = view[self.send(view):]

    def shutdown(self, how, /):
        _posix._call('shutdown', self._check(), how)

    def dup(self):
        return socket(self._family, self._type, self._proto, _posix.dup(self._check()))

    recvfrom = sendto = _unsupported


_POLLIN, _POLLOUT = 1, 4


def _no_flags(flags):
    if flags:
        raise OSError(_errno.ENOSYS, 'socket flags are not supported yet')


def _host(family, host):
    """The packed address of a host name or numeric address, as Modules/socketmodule.c setipaddr."""
    if not isinstance(host, str):
        host = bytes(host).decode('idna')
    if host == '':
        return bytes(4 if family == AF_INET else 16)
    if host == '<broadcast>' and family == AF_INET:
        return b'\xff' * 4
    packed = _lucifer.os_inet_pton(family, host)
    if packed is None:
        packed = _lucifer.os_inet_pton(family, getaddrinfo(host, None, family)[0][4][0])
    return packed


def _port(address):
    port = address[1]
    if not isinstance(port, int):
        raise TypeError("'%s' object cannot be interpreted as an integer" % type(port).__name__)
    if not 0 <= port <= 0xffff:
        raise OverflowError('bind(): port must be 0-65535.')
    return port.to_bytes(2, 'big')


def _pack(family, address):
    """The sockaddr bytes of a Python address (struct sockaddr_in, sockaddr_in6, sockaddr_un)."""
    head = family.to_bytes(2, 'little')
    if family == AF_INET:
        if not isinstance(address, tuple) or len(address) != 2:
            raise TypeError('AF_INET address must be tuple, not %s' % type(address).__name__)
        return head + _port(address) + _host(family, address[0]) + bytes(8)
    if family == AF_INET6:
        if not isinstance(address, tuple) or not 2 <= len(address) <= 4:
            raise TypeError('AF_INET6 address must be a tuple (host, port[, flowinfo[, scopeid]])')
        flowinfo, scope = (tuple(address[2:]) + (0, 0))[:2]
        return (head + _port(address) + flowinfo.to_bytes(4, 'big') + _host(family, address[0])
                + scope.to_bytes(4, 'little'))
    if family == AF_UNIX:
        path = address.encode('utf-8', 'surrogateescape') if isinstance(address, str) else bytes(address)
        return head + path + (b'' if path[:1] == b'\0' else b'\0')
    raise OSError(_errno.EAFNOSUPPORT, 'Address family not supported by protocol')


def _unpack(sockaddr):
    family = int.from_bytes(sockaddr[:2], 'little')
    if family == AF_INET:
        return _lucifer.os_inet_ntop(family, sockaddr[4:8]), int.from_bytes(sockaddr[2:4], 'big')
    if family == AF_INET6:
        return (_lucifer.os_inet_ntop(family, sockaddr[8:24]), int.from_bytes(sockaddr[2:4], 'big'),
                int.from_bytes(sockaddr[4:8], 'big'), int.from_bytes(sockaddr[24:28], 'little'))
    if family == AF_UNIX:
        path = sockaddr[2:]
        if path[:1] == b'\0':
            return path
        return path.split(b'\0', 1)[0].decode('utf-8', 'surrogateescape')
    return family, sockaddr[2:]


SocketType = socket
CAPI = _lucifer.socket_capi(socket, error, timeout)
socket.__module__ = '_socket'


def getdefaulttimeout():
    """Returns the default timeout in seconds (float) for new socket objects."""
    return _default_timeout


def setdefaulttimeout(timeout, /):
    """Set the default timeout in seconds (float) for new socket objects."""
    global _default_timeout
    if timeout is not None:
        timeout = float(timeout)
        if timeout < 0:
            raise ValueError('Timeout value out of range')
    _default_timeout = timeout


def gethostname():
    """Return the current host name."""
    import posix
    return posix.uname().nodename


def htons(x, /):
    """Convert a 16-bit integer from host to network byte order."""
    if not 0 <= x <= 0xffff:
        raise OverflowError('htons: Python int too large to convert to C 16-bit unsigned integer')
    return ((x & 0xff) << 8) | (x >> 8)


ntohs = htons


def htonl(x, /):
    """Convert a 32-bit integer from host to network byte order."""
    if not 0 <= x <= 0xffffffff:
        raise OverflowError('int larger than 32 bits')
    return int.from_bytes(x.to_bytes(4, 'little'), 'big')


ntohl = htonl


def inet_aton(ip_addr, /):
    """Convert an IP address in string format (123.45.67.89) to the 32-bit packed binary format used in low-level network functions."""
    parts = ip_addr.split('.')
    if len(parts) != 4 or not all(p.isdigit() and int(p) < 256 for p in parts):
        raise OSError('illegal IP address string passed to inet_aton')
    return bytes(int(p) for p in parts)


def inet_ntoa(packed_ip, /):
    """Convert an IP address from 32-bit packed binary format to string format."""
    packed_ip = bytes(packed_ip)
    if len(packed_ip) != 4:
        raise OSError('packed IP wrong length for inet_ntoa')
    return '.'.join(str(b) for b in packed_ip)


def inet_pton(address_family, ip_string, /):
    """Convert an IP address from string format to a packed string suitable for use with low-level network functions."""
    if address_family not in (AF_INET, AF_INET6):
        raise OSError(_errno.EAFNOSUPPORT, 'Address family not supported by protocol')
    packed = _lucifer.os_inet_pton(address_family, ip_string)
    if packed is None:
        raise OSError('illegal IP address string passed to inet_pton')
    return packed


def inet_ntop(address_family, packed_ip, /):
    """Convert a packed IP address of the given family to string format."""
    packed_ip = bytes(packed_ip)
    if address_family not in (AF_INET, AF_INET6):
        raise ValueError('unknown address family %d' % address_family)
    if len(packed_ip) != (4 if address_family == AF_INET else 16):
        raise ValueError('invalid length of packed IP address string')
    return _lucifer.os_inet_ntop(address_family, packed_ip)


def _bytes_or_none(value, what):
    if value is None:
        return None
    if isinstance(value, str):
        return value.encode('idna') if what == 'host' else value.encode()
    if isinstance(value, (bytes, bytearray)):
        return bytes(value)
    if what == 'port' and isinstance(value, int):
        return str(value).encode()
    raise OSError('Int or String expected' if what == 'port' else 'str, bytes or bytearray expected, not %s' % type(value).__name__)


def getaddrinfo(host, port, family=0, type=0, proto=0, flags=0):
    """Resolve host and port into addrinfo struct."""
    found = _lucifer.os_getaddrinfo(_bytes_or_none(host, 'host'), _bytes_or_none(port, 'port'),
                                    int(family), int(type), int(proto), int(flags))
    if isinstance(found, tuple):
        raise gaierror(*found)
    return found


def gethostbyname(hostname, /):
    """Return the IP address (a string of the form '255.255.255.255') for a host."""
    for family, _, _, _, address in getaddrinfo(hostname, None, AF_INET, SOCK_STREAM):
        return address[0]
    raise gaierror(-2, 'Name or service not known')


def gethostbyaddr(ip_address, /):
    """Return the true host name, a list of aliases, and a list of IP addresses, for a host."""
    if not isinstance(ip_address, str):
        ip_address = bytes(ip_address).decode('idna')
    for family in (AF_INET, AF_INET6):
        packed = _lucifer.os_inet_pton(family, ip_address)
        if packed is not None:
            break
    else:
        family, _, _, _, address = getaddrinfo(ip_address, None)[0]
        packed = _lucifer.os_inet_pton(family, address[0])
    found = _lucifer.os_gethostbyaddr(family, packed)
    if isinstance(found[0], int):
        raise herror(*found)
    name, aliases, addresses = found
    return name, aliases, [_lucifer.os_inet_ntop(family, a) for a in addresses]


def getservbyname(servicename, protocolname=None, /):
    """Return a port number from a service name and protocol name."""
    raise OSError('service/proto not found')


def socketpair(family=AF_UNIX, type=SOCK_STREAM, proto=0):
    """Create a pair of socket objects from the sockets returned by the platform socketpair() function."""
    a, b = _lucifer.os_socketpair(family, type | SOCK_CLOEXEC, proto)
    return socket(family, type, proto, a), socket(family, type, proto, b)


def close(fd, /):
    """Close an integer socket file descriptor."""
    import posix
    posix.close(fd)


def if_nameindex():
    """Returns a list of network interface information (index, name) tuples."""
    return []


def sethostname(name, /):
    """Sets the hostname to name."""
    _unsupported()


def dup(fd, /):
    """Duplicate an integer socket file descriptor."""
    import posix
    return posix.dup(fd)
