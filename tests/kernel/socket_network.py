# Network sockets: TCP over IPv4 and IPv6 loopback and Unix-domain stream
# sockets, with blocking, timed and nonblocking calls, served from threads.
import errno
import http.client
import http.server
import os
import socket
import tempfile
import threading

print(socket.inet_pton(socket.AF_INET6, '::1').hex(), socket.inet_ntop(socket.AF_INET6, bytes(15) + b'\x01'))
print(socket.inet_ntop(socket.AF_INET6, socket.inet_pton(socket.AF_INET6, 'fe80::1:2')))

def echo_server(family, address):
    server = socket.socket(family, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    print('reuseaddr', server.getsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR) != 0)
    server.bind(address)
    server.listen()
    def serve():
        for _ in range(2):
            conn, _ = server.accept()
            with conn:
                while data := conn.recv(65536):
                    conn.sendall(data.upper())
        server.close()
    t = threading.Thread(target=serve)
    t.start()
    return server.getsockname(), t

def round_trip(family, address):
    bound, t = echo_server(family, address)
    print(family.name, type(bound).__name__, bound[0] if family != socket.AF_UNIX else 'path')
    for _ in range(2):
        with socket.socket(family, socket.SOCK_STREAM) as c:
            c.connect(bound)
            print('peer matches', c.getpeername() == bound, c.family == family, c.type == socket.SOCK_STREAM)
            payload = b'abc' * 100000
            c.sendall(payload)
            c.shutdown(socket.SHUT_WR)
            f = c.makefile('rb')
            got = f.read()
            f.close()
            print(len(got), got == payload.upper())
    t.join()

round_trip(socket.AF_INET, ('127.0.0.1', 0))
round_trip(socket.AF_INET6, ('::1', 0))
with tempfile.TemporaryDirectory() as d:
    round_trip(socket.AF_UNIX, os.path.join(d, 'sock'))

# Timeouts, nonblocking calls, refused connections, options, adopted descriptors.
s = socket.socket()
s.bind(('127.0.0.1', 0))
s.listen()
s.settimeout(0.05)
try:
    s.accept()
except TimeoutError as e:
    print('accept', type(e).__name__, e)
s.setblocking(False)
try:
    s.accept()
except BlockingIOError as e:
    print('accept nonblocking', e.errno == errno.EAGAIN)
port = s.getsockname()[1]
c = socket.create_connection(('127.0.0.1', port), timeout=1)
c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
print('nodelay', c.getsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY) != 0, c.gettimeout())
print('linger', len(c.getsockopt(socket.SOL_SOCKET, socket.SO_LINGER, 8)))
c.settimeout(0.05)
try:
    c.recv(10)
except TimeoutError as e:
    print('recv', type(e).__name__, e)
adopted = socket.socket(fileno=os.dup(c.fileno()))
print('adopted', adopted.family == socket.AF_INET, adopted.type == socket.SOCK_STREAM, adopted.proto)
adopted.close()
c.close()
s.close()
closed = socket.socket()
closed.bind(('127.0.0.1', 0))
free = closed.getsockname()
closed.close()
r = socket.socket()
print('connect_ex refused', r.connect_ex(free) == errno.ECONNREFUSED)
try:
    r.connect(free)
except ConnectionRefusedError as e:
    print('connect', type(e).__name__)
r.close()
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as u:
    print(repr(u).startswith('<socket.socket fd='), 'laddr' not in repr(u))

# http.server and http.client over threads with keep-alive connections.
class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def do_GET(self):
        body = ('hello ' + self.path).encode()
        self.send_response(200)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def log_message(self, *args):
        pass

httpd = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
serving = threading.Thread(target=httpd.serve_forever)
serving.start()
results = {}
def client(n):
    conn = http.client.HTTPConnection('127.0.0.1', httpd.server_address[1])
    for i in range(20):
        conn.request('GET', f'/{n}/{i}')
        resp = conn.getresponse()
        results[n, i] = (resp.status, resp.read())
    conn.close()
clients = [threading.Thread(target=client, args=(n,)) for n in range(8)]
for t in clients:
    t.start()
for t in clients:
    t.join()
httpd.shutdown()
serving.join()
httpd.server_close()
print(len(results), all(v == (200, f'hello /{n}/{i}'.encode()) for (n, i), v in results.items()))
