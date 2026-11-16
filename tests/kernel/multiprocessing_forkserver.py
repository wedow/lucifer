# multiprocessing's default start method on Linux is forkserver (Lib/multiprocessing/context.py),
# which passes descriptors over AF_UNIX with sendmsg/recvmsg and SCM_RIGHTS, and whose server waits
# for SIGCHLD through signal.set_wakeup_fd and reads pids with os.readinto. A Pool started that
# way maps in its workers, which the server forks; fds sent with SCM_RIGHTS arrive as new fds.
import array
import multiprocessing
import os
import signal
import socket


def square(x):
    return x * x


def pid(_):
    return os.getpid()


if __name__ == "__main__":
    # SCM_RIGHTS: two pipe ends cross a socketpair and still name the same pipes.
    a, b = socket.socketpair()
    r, w = os.pipe()
    fds = array.array("i", [r, w])
    print(a.sendmsg([b"x", b"yz"], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, fds)]))
    msg, anc, flags, addr = b.recvmsg(10, socket.CMSG_SPACE(2 * fds.itemsize))
    level, kind, data = anc[0]
    got = array.array("i", data)
    os.write(got[1], b"through")
    print(msg, len(anc), level == socket.SOL_SOCKET, kind == socket.SCM_RIGHTS, flags, addr,
          sorted(got) != sorted(fds), os.read(r, 10))
    for fd in (r, w, *got):
        os.close(fd)
    # No ancillary data, an ancillary buffer too small (MSG_CTRUNC), recvmsg_into.
    a.sendmsg([b"plain"])
    print(b.recvmsg(10, 64))
    a.sendmsg([b"q"], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array("i", [0, 1, 2]))])
    msg, anc, flags, addr = b.recvmsg(10, socket.CMSG_LEN(4))
    print(msg, bool(flags & socket.MSG_CTRUNC), [len(d) for _, _, d in anc])
    for _, _, d in anc:
        for fd in array.array("i", d[:len(d) - len(d) % 4]):
            os.close(fd)
    a.sendall(b"abcdef")
    buf1, buf2 = bytearray(2), bytearray(10)
    print(b.recvmsg_into([buf1, buf2]), buf1, bytes(buf2[:4]))
    print(socket.CMSG_LEN(0), socket.CMSG_LEN(4), socket.CMSG_SPACE(0), socket.CMSG_SPACE(4))
    a.close()
    b.close()

    # set_wakeup_fd: each signal's number is written to the fd; it must be non-blocking.
    r, w = os.pipe()
    try:
        signal.set_wakeup_fd(w)
    except ValueError as e:
        print(str(e) == "the fd %d must be in non-blocking mode" % w)
    os.set_blocking(w, False)
    signal.signal(signal.SIGUSR1, lambda *args: None)
    print(signal.set_wakeup_fd(w) == -1, signal.set_wakeup_fd(w) == w)
    signal.raise_signal(signal.SIGUSR1)
    print(signal.set_wakeup_fd(-1) == w, list(os.read(r, 10)) == [signal.SIGUSR1])
    buf = bytearray(5)
    os.write(w, b"abc")
    print(os.readinto(r, buf), buf)
    os.close(r)
    os.close(w)

    print(multiprocessing.get_start_method())
    with multiprocessing.Pool(3) as pool:
        print(pool.map(square, range(20)))
        print(sorted(pool.imap(square, range(10))))
        pids = set(pool.map(pid, range(30), chunksize=1))
        print(0 < len(pids) <= 3, os.getpid() not in pids)
    p = multiprocessing.Process(target=square, args=(3,))
    p.start()
    p.join()
    print(p.exitcode)
