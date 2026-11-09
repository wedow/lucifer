# The process-group calls: a child started in its own session leads its
# group, which killpg signals as one (cadence's deadline reaps its workers so).
import os
import signal
import subprocess
import sys

child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'], start_new_session=True)
print(os.getsid(child.pid) == child.pid, os.getpgid(child.pid) == child.pid, os.getpgid(0) == os.getpgrp())
os.killpg(child.pid, signal.SIGTERM)
print(child.wait())
code = 'import os; os.setpgrp(); a = os.getpgrp() == os.getpid(); os.setpgid(0, 0); print(a, os.getpgid(0) == os.getpid())'
print(subprocess.run([sys.executable, '-c', code], capture_output=True, text=True).stdout.split())
for call in (lambda: os.killpg(2**22 + 12345, 0), lambda: os.getpgid(2**22 + 12345)):
    try:
        call()
    except ProcessLookupError as e:
        print('ProcessLookupError', e.errno)
