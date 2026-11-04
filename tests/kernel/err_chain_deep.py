# An uncaught exception whose __context__ chain is far deeper than the C
# stack could recurse. The renderer that stands in for the traceback module
# must print and free the chain iteratively. Output goes to /dev/null so the
# comparison covers the exit status only.
import os
import sys
import types


def refuse(*args):
    raise RuntimeError("no printer")


printer = types.ModuleType("traceback")
printer._print_exception_bltin = refuse
sys.modules["traceback"] = printer
sys.excepthook = refuse
os.dup2(os.open(os.devnull, os.O_WRONLY), 2)

chain = ValueError(0)
for i in range(1, 30000):
    link = ValueError(i)
    link.__context__ = chain
    chain = link
raise chain
