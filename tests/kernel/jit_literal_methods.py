# A list created inside the unit has an exact type even before it is
# materialized. Its calls can use the descriptor directly.
import sys


def total(n):
    result = 0
    for i in range(n):
        items = []
        items.append(i)
        result += items[0]
    return result


print(total(int(sys.argv[1]) if len(sys.argv) > 1 else 3000))
