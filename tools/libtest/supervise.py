"""Run one libtest command and reap its entire process tree on Linux."""
import ctypes
import json
import os
from pathlib import Path
import signal
import subprocess
import sys


PR_SET_CHILD_SUBREAPER = 36


def reap_descendants():
    children = Path('/proc/thread-self/children')
    while True:
        try:
            os.waitpid(-1, os.WNOHANG)
        except ChildProcessError:
            return
        # Killing an adopted child can expose another generation, including
        # processes that made their own sessions. Repeat until ECHILD.
        for child in map(int, children.read_text().split()):
            try:
                os.kill(child, signal.SIGKILL)
            except ProcessLookupError:
                pass
            os.waitpid(child, 0)


def execute(command, timeout, output):
    # This supervisor is private to one command, including on parallel runs.
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) != 0:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error))

    try:
        proc = subprocess.Popen(command, stdout=output, start_new_session=True)
    except OSError as error:
        os.write(output, str(error).encode())
        return None, False
    try:
        return proc.wait(timeout=timeout), False
    except subprocess.TimeoutExpired:
        proc.kill()
        return proc.wait(), True
    finally:
        if proc.returncode is None:
            proc.kill()
            proc.wait()
        reap_descendants()


if __name__ == '__main__':
    timeout, output, *command = sys.argv[1:]
    print(json.dumps(execute(command, float(timeout), int(output))))
