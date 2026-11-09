# os.abort ends the process with SIGABRT at once; os._exit skips atexit, so a
# handler that would abort does not run (cadence's MLX discovery child).
import subprocess
import sys

for code in ("import os; print('before', flush=True); os.abort(); print('after')",
             "import atexit, os; atexit.register(os.abort); print('exiting', flush=True); os._exit(3)"):
    done = subprocess.run([sys.executable, '-c', code], capture_output=True, text=True)
    print(done.returncode, done.stdout.split())
