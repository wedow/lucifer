"""Small ints against constants past one digit, in hot loops: within 64
bits, past them, negative, on either side, and sequence repetition, whose
count the kernel checks against the machine-word range."""
BIG = 2 ** 63 - 1
NEG = -2 ** 63
HUGE = 2 ** 70
MID = 2 ** 40


def count(n):
    hits = [0] * 8
    for i in range(-n, n):
        hits[0] += i < BIG
        hits[1] += i > NEG
        hits[2] += BIG > i
        hits[3] += i < HUGE
        hits[4] += -HUGE < i
        hits[5] += i == MID
        hits[6] += i != MID
        hits[7] += i >= MID
    return hits


def repeat(n):
    total = 0
    for i in range(n):
        total += len([None] * (i % 7)) + len((1, 2) * (i % 3))
    return total


print(count(3000))
print(repeat(5000))
try:
    [None] * HUGE
except OverflowError as e:
    print('OverflowError', e)
