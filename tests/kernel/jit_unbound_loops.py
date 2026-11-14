# locals unbound when a loop starts and numbers round it
import sys

def f(n, flag):
    total = 0.0
    for i in range(n):
        if flag and i == 0:
            try:
                x
            except UnboundLocalError as e:
                total -= 1.0
        x = i * 0.5
        y = i + 1
        total += x * y
    return total, x, y

def g(n):
    out = []
    for i in range(n):
        for j in range(3):
            if j == 2 and i % 50 == 0:
                out.append(sorted(k for k in sys._getframe().f_locals if k in ('a', 'b')))
            a = j * 1.5
            b = j + i
        del a
    return out[:3], out[-1], b

def h(n):
    for i in range(n):
        if i > 2:
            z = z + 1.5
        else:
            z = 0.25
    return z

for _ in range(200):
    r = (f(40, False), f(40, True), g(120), h(30))
print(r)
try:
    def bad(n):
        for i in range(n):
            if i > 60:
                print(w)
            if i > 80:
                w = 1.5
    bad(100)
except UnboundLocalError as e:
    print("unbound", e)
