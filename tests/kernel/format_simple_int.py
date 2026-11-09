# f-strings of ints: exact ints format natively, bools and subclasses through
# their methods, and the digit limit still raises; compiled and interpreted.
import sys
class I(int): pass
vals = [0, -1, 7, 10**30, -10**25, True, I(5), 2**63]
for i in range(300):
    out = [f'{v}' for v in vals] + [f'Task-{i}']
print(out)
sys.set_int_max_str_digits(1000)
try:
    f'{10**2000}'
except ValueError as e:
    print('ValueError', str(e)[:40])
