# float() and json number parsing: Python's grammar (whitespace, underscores,
# inf/nan spellings, signs), the exact ValueError text, and correctly rounded
# results on generated adversarial strings: exact halfway points between
# adjacent doubles and their neighbours, long mantissas, subnormals, overflow,
# huge exponents and malformed text. `float_parse.py N` generates N cases.
import json
import sys

state = 20261111


def rnd(n):
    global state
    state = (state * 6364136223846793005 + 1442695040888963407) % (2 ** 64)
    return (state >> 20) % n


def parse(s):
    try:
        return float(s).hex()
    except Exception as e:
        return type(e).__name__ + ': ' + str(e)


GRAMMAR = [
    '1.5', ' 1.5 ', '\t\n1.5\x0b\x0c\r', ' -2.5　', '\xa01e3\x85',
    '1_000.000_1', '1_0e1_0', '1__0', '_1', '1_', '1_.5', '1._5', '1_e5',
    '1e_5', '1e5_', '-_1', '0_0', '0x10', '0b1', '1e', '1e+', '1e-', '1.e5',
    '.5e-3', '-.5', '+.5', '.', '-', '+', 'e5', '', ' ', '+-1', '--1', '1 2',
    '1\x00', 'inf', '-inf', '+inf', 'INF', 'iNfInItY', '-Infinity', 'infinit',
    'infinityy', 'nan', '-nan', '+NaN', 'nan(1)', 'nanx', 'in', 'na',
    '1e308', '1e309', '-1e309', '1e-324', '-1e-400', '4.9e-324', '2.5e-324',
    '2.4703282292062328e-324', '1.7976931348623158080e308',
    '1e99999999999999999999', '1e-99999999999999999999', '0e999999999',
    '١٢٣.٤', '١_٢', '1١', '\U0001d7e3.5',
    '1.5é', 'x' * 300, '1' * 400 + '.5',
    b'2.5', b' 2.5 ', b'1_0', b'inf', b'bad', b'\xff', bytearray(b'3.25'),
    memoryview(b'4.5'),
]


def show_grammar():
    for s in GRAMMAR:
        print(type(s).__name__ if type(s) is memoryview else repr(s), parse(s))
    for bad in (None, 1j, [], object()):
        print(type(bad).__name__, parse(bad))


def digits(n):
    out = [str(1 + rnd(9))]
    i = 1
    while i < n:
        out.append(str(rnd(10)))
        i += 1
    return ''.join(out)


def halfway():
    """The exact midpoint above m * 2**e, in decimal, maybe nudged."""
    kind = rnd(8)
    if kind == 0:
        m, e = rnd(2 ** 52), -1074                       # subnormal
    elif kind == 1:
        m, e = 2 ** 53 - 1 - rnd(4), 971                  # near overflow
    else:
        m, e = 2 ** 52 + rnd(2 ** 52), rnd(2046) - 1074
    mid = 2 * m + 1
    if e >= 1:
        text = str(mid << (e - 1))
        exp = 0
    else:
        text = str(mid * 5 ** (1 - e))
        exp = e - 1
    nudge = rnd(6)
    if nudge == 1:
        text = str(int(text) + 1)
    elif nudge == 2:
        text = str(int(text) - 1)
    elif nudge == 3:
        cut = 17 + rnd(30)
        exp += max(len(text) - cut, 0)
        text = text[:cut]
    elif nudge == 4:
        tail = '0' * rnd(30) + '1'
        text += tail
        exp -= len(tail)
    if exp == 0:
        return text
    return text + 'e' + str(exp)


def random_number():
    out = []
    if rnd(2):
        out.append('-' if rnd(2) else '+')
    n = 1 + rnd(25)
    if rnd(20) == 0:
        n = 1 + rnd(800)
    body = digits(n) if rnd(4) else '0' * rnd(30) + digits(n)
    point = rnd(len(body) + 1)
    if rnd(3):
        body = body[:point] + '.' + body[point:]
    out.append(body)
    if rnd(2):
        out.append('e' if rnd(2) else 'E')
        r = rnd(3)
        if r == 1:
            out.append('-')
        elif r == 2:
            out.append('+')
        out.append(str(rnd(400)) if rnd(8) else digits(1 + rnd(25)))
    return ''.join(out)


def mutate(s):
    chars = '0123456789.eE+-_ xin\t'
    k = rnd(4)
    i = rnd(len(s) + 1)
    c = chars[rnd(len(chars))]
    if k == 0:
        return s[:i] + c + s[i:]
    if k == 1:
        return s[:i] + s[i + 1:]
    if k == 2 and '.' in s:
        return s.replace('.', '_', 1)
    if k == 3 and len(s) > 2:
        j = 1 + rnd(len(s) - 1)
        if s[j - 1].isdigit() and s[j].isdigit():
            return s[:j] + '_' + s[j:]
    return ' ' + s + ' '


def case():
    """A generated string, and whether it is a JSON number."""
    k = rnd(10)
    if k < 4:
        return halfway(), True
    s = random_number()
    if k >= 8:
        s = mutate(s)
    return s, False


def run(n):
    json_numbers = []
    i = 0
    while i < n:
        s, is_json = case()
        print(i, parse(s.encode() if i % 7 == 0 else s))
        if is_json and len(json_numbers) < 500:
            json_numbers.append(s)
        i += 1
    for x in json.loads('[' + ','.join(json_numbers) + ']'):
        print('json', x.hex() if type(x) is float else 'int')


show_grammar()
run(int(sys.argv[1]) if len(sys.argv) > 1 else 4000)
