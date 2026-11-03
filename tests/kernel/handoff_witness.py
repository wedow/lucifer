# How many carriers ran this loop: the distinct native thread ids seen over
# 1,000 iterations. One plain, two under -X lucifer_handoff (the runner's one
# documented difference from the oracle).
import _thread

seen = set()
for i in range(1000):
    seen.add(_thread.get_native_id())
print(len(seen))
