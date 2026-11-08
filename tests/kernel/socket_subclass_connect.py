# socket.connect_ex is the socket's own connect, not a subclass's override
# (ssl.SSLSocket overrides connect and calls connect_ex), and an unclosed
# socket warns when it is collected.
import gc
import socket
import warnings


class Overrides(socket.socket):
    def connect(self, address):
        raise AssertionError('connect_ex called the override')


server = socket.socket()
server.bind(('127.0.0.1', 0))
server.listen()
s = Overrides()
print(s.connect_ex(server.getsockname()))
s.close()
closed = socket.socket()
closed.bind(('127.0.0.1', 0))
port = closed.getsockname()[1]
closed.close()
s = socket.socket()
print(s.connect_ex(('127.0.0.1', port)) != 0)
s.close()
server.close()
with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter('always')
    s = socket.socket()
    del s
    gc.collect()
print([(w.category.__name__, str(w.message).startswith('unclosed <socket.socket')) for w in caught])
