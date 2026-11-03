import atexit
import posix
import sys

# Both streams share one pipe: the buffered stdout comes out before the
# traceback (flushed after the program ran), what atexit callbacks print
# comes out when they finish.
posix.dup2(1, 2)
print("out 1")
sys.stderr.write("err 1\n")
sys.stderr.write("partial err ")
atexit.register(lambda: print("atexit stdout"))
atexit.register(lambda: sys.stderr.write("atexit stderr\n"))
atexit.register(lambda: sys.stdout.write("atexit pending"))
sys.stdout.write("pending")
raise RuntimeError("boom")
