"""_socket (Modules/socketmodule.c): the constants, the error classes, the
address conversion functions, and sockets made from descriptors: socketpair()
and socket(fileno=...), which read, write and shut down as nonblocking or
blocking descriptors do. Creating a network socket raises OSError: networking
arrives with the scheduler's I/O layer, and until then importing socket,
asyncio and everything that imports them works."""

import errno as _errno
import _lucifer
import posix as _posix

error = OSError


class herror(OSError):
    """Error from gethostbyname etc."""


class gaierror(OSError):
    """Error from getaddrinfo etc."""


timeout = TimeoutError
has_ipv6 = True
SocketType = None
CAPI = None

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


class socket:
    """socket(family=AF_INET, type=SOCK_STREAM, proto=0) -> socket object

Only sockets over an existing descriptor (fileno=) exist yet."""
    __slots__ = ('_fd', '_timeout', '_family', '_type', '_proto')

    def __init__(self, family=-1, type=-1, proto=-1, fileno=None):
        if fileno is None:
            _unsupported()
        if not isinstance(fileno, int):
            raise TypeError('an integer is required')
        if fileno < 0:
            raise ValueError('negative file descriptor')
        _posix.fstat(fileno)
        self._fd = fileno
        self._family = AF_UNIX if family == -1 else family
        self._type = (SOCK_STREAM if type == -1 else type) & ~(SOCK_NONBLOCK | SOCK_CLOEXEC)
        self._proto = 0 if proto == -1 else proto
        self._timeout = _default_timeout if _posix.get_blocking(fileno) else 0.0
        if self._timeout is not None and self._timeout > 0:
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
        if self._timeout:
            ready = _lucifer.os_poll([(self._fd, events)], int(self._timeout * 1000) or 1)
            if ready is not None and not ready:      # None: interrupted by a signal
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

    def recv(self, bufsize, flags=0, /):
        if bufsize < 0:
            raise ValueError('negative buffersize in recv')
        fd = self._check()
        self._wait(_POLLIN)
        return _posix.read(fd, bufsize)

    def send(self, data, flags=0, /):
        fd = self._check()
        self._wait(_POLLOUT)
        return _posix.write(fd, data)

    def sendall(self, data, flags=0, /):
        view = memoryview(data).cast('B')
        while view:
            view = view[self.send(view):]

    def shutdown(self, how, /):
        _posix._call('shutdown', self._check(), how)

    def dup(self):
        return socket(self._family, self._type, self._proto, _posix.dup(self._check()))

    connect = connect_ex = bind = listen = accept = recvfrom = sendto = _unsupported
    setsockopt = getsockopt = getsockname = getpeername = _unsupported


_POLLIN, _POLLOUT = 1, 4


SocketType = socket
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
    if address_family == AF_INET:
        try:
            return inet_aton(ip_string)
        except OSError:
            raise OSError('illegal IP address string passed to inet_pton') from None
    raise OSError(_errno.EAFNOSUPPORT, 'Address family not supported by protocol')


def inet_ntop(address_family, packed_ip, /):
    """Convert a packed IP address of the given family to string format."""
    if address_family == AF_INET:
        return inet_ntoa(packed_ip)
    raise ValueError('unknown address family %d' % address_family)


def getaddrinfo(host, port, family=0, type=0, proto=0, flags=0):
    """Resolve host and port into addrinfo struct."""
    raise gaierror(-2, 'Name or service not known')


def gethostbyname(hostname, /):
    """Return the IP address (a string of the form '255.255.255.255') for a host."""
    raise gaierror(-2, 'Name or service not known')


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
