import posix
import sys

# A broken stdout: the flush at exit fails, is reported, and the exit status is 120.
r, w = 0, 1
print("buffered, then the descriptor goes away")
posix.close(1)
