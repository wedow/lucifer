# subprocess and select over fork_exec, poll and the pipes.
import os
import select
import subprocess

r = subprocess.run(['/bin/sh', '-c', 'echo out; echo err >&2; exit 3'], capture_output=True)
print(r.returncode, r.stdout, r.stderr)
print(subprocess.check_output(['/bin/echo', 'a', 'b']), subprocess.check_output('echo $FOO', shell=True, env={'FOO': 'bar'}))
print(subprocess.run(['cat'], input=b'hello', capture_output=True).stdout)
print(subprocess.run(['pwd'], cwd='/usr', capture_output=True, text=True).stdout)
print(subprocess.run('echo $0; exit 0', shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True).stdout)
print(subprocess.call(['/bin/sh', '-c', 'kill -9 $$']), subprocess.call(['true']), subprocess.call(['false']))
for bad in (lambda: subprocess.run(['/no/such/program']), lambda: subprocess.run(['sh', '-c', 'exit 1'], check=True),
            lambda: subprocess.run(['sleep', '5'], timeout=0.2), lambda: subprocess.run(['x'], cwd='/no/such/dir')):
    try:
        bad()
    except Exception as e:
        print(type(e).__name__, getattr(e, 'returncode', None), getattr(e, 'errno', None))
p = subprocess.Popen(['/bin/sh', '-c', 'read x; echo got $x; sleep 0.1; echo done'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
print(p.communicate('line\n'), p.returncode, p.poll())
rfd, wfd = os.pipe()
print(select.select([rfd], [], [], 0), select.select([], [wfd], [], 0) == ([], [wfd], []))
os.write(wfd, b'x')
print(select.select([rfd], [], [], 1)[0] == [rfd])
poller = select.poll()
poller.register(rfd, select.POLLIN)
print(poller.poll(0) == [(rfd, select.POLLIN)], poller.poll(0.01))
os.read(rfd, 1)
print(poller.poll(10))
os.close(wfd)
print(poller.poll(1000) == [(rfd, select.POLLHUP)])
poller.unregister(rfd)
os.close(rfd)
fd = os.open('/dev/null', os.O_RDONLY)
print(subprocess.run(['ls', '/proc/self/fd'], capture_output=True, text=True).stdout.split(), subprocess.run(['ls', '/proc/self/fd'], capture_output=True, text=True, close_fds=False).returncode)
r2, w2 = os.pipe()
print(subprocess.run(['sh', '-c', 'echo hi >&%d' % w2], pass_fds=[w2]).returncode, os.read(r2, 10))
print(subprocess.run(['sh', '-c', 'umask'], umask=0o77, capture_output=True, text=True).stdout.strip(), os.waitstatus_to_exitcode(256 * 5), os.waitstatus_to_exitcode(9))
os.execv if False else None
