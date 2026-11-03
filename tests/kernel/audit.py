import sys

seen = []
interesting = {"compile", "exec", "open", "import", "sys._getframe", "builtins.id"}


def hook(event, args):
    if event in interesting:
        seen.append(event)
        if event == "import":
            print("audit", event, args[0])
        elif event == "open":
            print("audit", event, args[0].rpartition("/")[2], args[2] & 3)
        elif event == "compile":
            print("audit", event, args[1])
        else:
            print("audit", event)


sys.addaudithook(hook)

compile("1 + 1", "<audited>", "eval")
exec("x = 1")
exec(compile("y = 2", "<second>", "exec"))
f = open(__file__, "rb")
f.close()
import colorsys
sys._getframe()
sys.audit("custom.event", 1, 2)
sys.addaudithook(lambda e, a: print("late hook", e) if e == "custom.event" else None)
sys.audit("custom.event", 3)
print(sorted(set(seen)))
