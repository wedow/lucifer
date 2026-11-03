# Pseudo-random bytes through the methods, with identities checked and a
# checksum of the results printed.
state = 987654321


def rnd(n):
    global state
    state = (state * 6364136223846793005 + 1442695040888963407) % (2 ** 64)
    return (state >> 33) % n



def build(n, alphabet):
    out = []
    i = 0
    while i < n:
        out.append(alphabet[rnd(len(alphabet))])
        i += 1
    return bytes(out)


check = 0
bad = 0
alphabets = (b'ab', b'abc ', b'abcdefgh \n\t', bytes(range(256)), b'\x00\xff')
i = 0
while i < 300:
    alpha = alphabets[i % 5]
    s = build(rnd(60), alpha)
    sub = build(rnd(4), alpha)
    t = bytearray(s)
    if s.find(sub) != t.find(sub) or s.rfind(sub) != t.rfind(sub) or s.count(sub) != t.count(sub):
        bad += 1
    f = s.find(sub)
    if sub and f >= 0 and s[f:f + len(sub)] != sub:
        bad += 1
    if sub and f < 0 and sub in s:
        bad += 1
    if sub:
        parts = s.split(sub)
        if sub.join(parts) != s or len(parts) != s.count(sub) + 1:
            bad += 1
        if s.replace(sub, b'') != b''.join(parts):
            bad += 1
        head, mid, tail = s.partition(sub)
        if head + mid + tail != s:
            bad += 1
        head, mid, tail = s.rpartition(sub)
        if head + mid + tail != s:
            bad += 1
    ws = s.split()
    if b' '.join(ws) != b' '.join(t.split()) or len(s.strip()) > len(s):
        bad += 1
    if s.upper().lower() != s.lower() or s.lower().upper() != s.upper():
        bad += 1
    if s.hex() != s.hex(':').replace(':', '') or bytes.fromhex(s.hex()) != s:
        bad += 1
    if s[::-1][::-1] != s or s[::2] + s[1::2] == s and len(s) > 2 and s != s[::2] + s[1::2] and False:
        bad += 1
    if s.decode('latin-1').encode('latin-1') != s:
        bad += 1
    if s.translate(bytes.maketrans(b'ab', b'ba')).translate(bytes.maketrans(b'ab', b'ba')) != s:
        bad += 1
    k = rnd(5)
    if s * k != b''.join([s] * k) or len(s * k) != len(s) * k:
        bad += 1
    if (s < s + b'\x00') is not True or s == s + b'x' or (s == t) is not True:
        bad += 1
    j = rnd(len(s) + 1)
    u = bytearray(s)
    u[j:j] = sub
    if u != s[:j] + sub + s[j:]:
        bad += 1
    del u[j:j + len(sub)]
    if u != s:
        bad += 1
    if len(s) > 3:
        u[1:3] = b'xyz'
        if u != s[:1] + b'xyz' + s[3:]:
            bad += 1
        u = bytearray(s)
        u[::2] = bytes([7]) * len(u[::2])
        if u[::2] != bytes([7]) * len(s[::2]) or u[1::2] != s[1::2]:
            bad += 1
        del u[::3]
        if len(u) != len(s) - len(s[::3]):
            bad += 1
    mv = memoryview(s)
    if mv.tobytes() != s or mv[j:].tobytes() != s[j:] or bytes(mv[::3]) != s[::3] or mv.hex() != s.hex():
        bad += 1
    if hash(s) != hash(bytes(bytearray(s))) or hash(s) != hash(mv):
        bad += 1
    if s.startswith(sub) != (s[:len(sub)] == sub) or s.endswith(sub) != (s[len(s) - len(sub):] == sub):
        bad += 1
    for part in (s.upper(), s.title(), s.swapcase(), s.capitalize(), s.center(70, b'*'), s.zfill(40), s.expandtabs(3)):
        for ch in part:
            check = (check * 31 + ch) % 1000000007
    check = (check + hash(s) * 0 + len(s) + s.count(b'a') * 7) % 1000000007
    i += 1
print(bad, check)
