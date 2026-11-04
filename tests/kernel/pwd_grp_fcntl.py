import fcntl
import grp
import os
import pwd
import tempfile


def show(f, *args):
    try:
        print(repr(f(*args)))
    except BaseException as e:
        print(type(e).__name__, e)


root = pwd.getpwuid(0)
print(root, root.pw_name, root[2], len(root), type(root).__name__)
print(pwd.getpwnam('root') == root, pwd.getpwuid(os.getuid()).pw_uid == os.getuid())
print(any(p.pw_name == 'root' for p in pwd.getpwall()), sorted(set(type(p).__name__ for p in pwd.getpwall())))
show(pwd.getpwuid, 2**31 - 2)
show(pwd.getpwnam, 'no_such_user_xyz')
show(pwd.getpwnam, 5)
show(pwd.getpwnam, 'a\0b')
show(pwd.getpwuid, 'x')

group = grp.getgrgid(0)
print(group, group.gr_name, group[2], group.gr_mem, type(group).__name__)
print(grp.getgrnam(group.gr_name) == group, any(g.gr_gid == 0 for g in grp.getgrall()))
show(grp.getgrgid, 2**31 - 2)
show(grp.getgrnam, 'no_such_group_xyz')
show(grp.getgrnam, 1)

print(fcntl.F_GETFD, fcntl.F_SETFD, fcntl.FD_CLOEXEC, fcntl.F_GETFL, fcntl.LOCK_EX, fcntl.LOCK_NB, fcntl.LOCK_UN)
r, w = os.pipe()
print(fcntl.fcntl(r, fcntl.F_GETFD) & fcntl.FD_CLOEXEC, fcntl.fcntl(w, fcntl.F_GETFL) & os.O_ACCMODE)
fcntl.fcntl(r, fcntl.F_SETFD, 0)
print(fcntl.fcntl(r, fcntl.F_GETFD))
flags = fcntl.fcntl(r, fcntl.F_GETFL)
fcntl.fcntl(r, fcntl.F_SETFL, flags | os.O_NONBLOCK)
print(bool(fcntl.fcntl(r, fcntl.F_GETFL) & os.O_NONBLOCK))
show(fcntl.fcntl, 999, fcntl.F_GETFD)
os.close(r)
os.close(w)
with tempfile.TemporaryFile() as f:
    fcntl.flock(f, fcntl.LOCK_EX)
    fcntl.flock(f.fileno(), fcntl.LOCK_UN)
    fcntl.lockf(f, fcntl.LOCK_SH | fcntl.LOCK_NB)
    print(fcntl.fcntl(f, fcntl.F_GETFD))
show(fcntl.flock, 'x', fcntl.LOCK_EX)
