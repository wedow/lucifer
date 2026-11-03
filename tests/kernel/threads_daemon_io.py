# A daemon thread busy on the buffer under sys.stdout when the program ends: the exit flushes the
# standard streams and does not wait for a lock that the daemon, stopped for good, may hold. Each
# child process is one such exit; one daemon only, since CPython aborts if daemons contend for it.
import subprocess
import sys

CHILD = '''
import sys, threading, time
def flusher():
    while True:
        sys.stdout.buffer.flush()
threading.Thread(target=flusher, daemon=True).start()
time.sleep(0.02)
print('child done')
'''
results = []
for _ in range(10):
    try:
        child = subprocess.run([sys.executable, '-I', '-S', '-c', CHILD], capture_output=True, timeout=20)
        results.append((child.returncode, child.stdout))
    except subprocess.TimeoutExpired:
        results.append('hung')
print(set(map(str, results)))
