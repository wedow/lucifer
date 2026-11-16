# recvmsg_into checks its buffers before it receives (Modules/socketmodule.c sock_recvmsg_into):
# a read-only buffer fails with TypeError and the message stays queued for the next receive.
import socket

a, b = socket.socketpair()
b.setblocking(False)
a.sendall(b"kept")
for buffers in ([b"abc"], [bytearray(2), b"ro"]):
    try:
        b.recvmsg_into(buffers)
    except TypeError:
        print("TypeError")
    except BlockingIOError:
        print("message lost")
try:
    print(b.recv(10))
except BlockingIOError:
    print("message lost")
a.close()
b.close()
