# The posix calls os, shutil and tempfile need, in a scratch directory.
import os
import stat

root = 'posix_calls_scratch'
os.mkdir(root, 0o700)
try:
    a = os.path.join(root, 'a')
    with open(a, 'w') as f:
        f.write('hello')
    os.mkdir(os.path.join(root, 'sub'))
    os.symlink('a', os.path.join(root, 'link'))
    print(os.readlink(os.path.join(root, 'link')), os.path.islink(os.path.join(root, 'link')))
    os.link(a, os.path.join(root, 'hard'))
    print(os.stat(a).st_nlink)
    os.rename(os.path.join(root, 'hard'), os.path.join(root, 'moved'))
    os.replace(os.path.join(root, 'moved'), os.path.join(root, 'again'))
    os.chmod(a, 0o640)
    print(oct(stat.S_IMODE(os.stat(a).st_mode)))
    os.utime(a, (1000, 2000))
    print(os.stat(a).st_atime, os.stat(a).st_mtime)
    os.utime(a, ns=(5_000_000_001, 6_000_000_002))
    print(os.stat(a).st_atime_ns, os.stat(a).st_mtime_ns)
    print(os.access(a, os.R_OK), os.access(os.path.join(root, 'nope'), os.F_OK))
    print(sorted(e.name for e in os.scandir(root)))
    with os.scandir(root) as it:
        print(sorted((e.name, e.is_dir(), e.is_file(), e.is_symlink()) for e in it))
    print(sorted(e.path for e in os.scandir(root.encode())))
    for top, dirs, files in os.walk(root):
        print(top, sorted(dirs), sorted(files))
    os.truncate(a, 2)
    print(os.stat(a).st_size)
    fd = os.open(a, os.O_RDWR)
    os.fsync(fd)
    print(os.get_inheritable(fd))
    os.set_inheritable(fd, True)
    print(os.get_inheritable(fd))
    os.close(fd)
    r, w = os.pipe()
    os.write(w, b'xyz')
    print(os.read(r, 10), os.get_blocking(r))
    os.set_blocking(r, False)
    print(os.get_blocking(r))
    os.close(r), os.close(w)
    os.unlink(os.path.join(root, 'link'))
    os.remove(os.path.join(root, 'again'))
    try:
        os.rmdir(root)
    except OSError as e:
        print(type(e).__name__, e.errno, e.filename)
    try:
        os.unlink(os.path.join(root, 'missing'))
    except FileNotFoundError as e:
        print(e.errno, e.filename)
finally:
    import shutil
    shutil.rmtree(root)
print(os.path.exists(root))

old = os.umask(0o22)
print(oct(os.umask(old)))
print(os.getuid() == os.geteuid(), os.getgid() == os.getegid(), os.getppid() > 0, os.getpgrp() > 0)
print(len(os.urandom(16)), os.urandom(0))
u = os.uname()
print(u.sysname, len(u), u[4] == u.machine)
print(os.cpu_count() >= 1)
os.environ['POSIX_CALLS_VAR'] = 'v1'
print(os.getenv('POSIX_CALLS_VAR'), os.environb[b'POSIX_CALLS_VAR'])
del os.environ['POSIX_CALLS_VAR']
print(os.getenv('POSIX_CALLS_VAR'))
cwd = os.getcwd()
os.mkdir('posix_calls_cd')
os.chdir('posix_calls_cd')
print(os.path.basename(os.getcwd()))
os.chdir(cwd)
os.rmdir('posix_calls_cd')
print(os.WEXITSTATUS(3 << 8), os.WIFEXITED(3 << 8), os.WIFSIGNALED(9), os.WTERMSIG(9))
