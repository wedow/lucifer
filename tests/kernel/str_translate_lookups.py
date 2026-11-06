# str.translate looks characters up in its table as CPython does: an ASCII
# string once per distinct character while every answer is one ASCII
# character, a deletion or nothing (a LookupError), and from the first other
# answer on once per character (that one twice); any other string once per
# character. The lookups each call makes, and what it returns or raises.


class Counting(dict):
    def __init__(self, *args):
        super().__init__(*args)
        self.keys_asked = []

    def __getitem__(self, key):
        self.keys_asked.append(key)
        return super().__getitem__(key)


class Indexed:
    def __init__(self, text):
        self.text = text
        self.keys_asked = []

    def __getitem__(self, key):
        self.keys_asked.append(key)
        return self.text[key - 97]


class Letter(str):
    pass


def show(s, table):
    try:
        out = repr(s.translate(table))
    except Exception as e:
        out = type(e).__name__ + ': ' + str(e)
    print(repr(s), out, [chr(k) if k < 0x110000 else k for k in table.keys_asked])


show('hello world', Counting({ord('o'): '0', ord('l'): None}))
show('hello world', Counting({ord('o'): 'oo'}))
show('abcabc', Counting({ord('b'): 200, ord('a'): 66}))
show('abcabc', Counting({ord('c'): Letter('C')}))
show('héllo hé', Counting({ord('l'): 'L'}))
show('aaa', Counting({ord('a'): 0x110000}))
show('aaa', Counting({ord('a'): 1.5}))
show('aaa', Counting({ord('a'): -1}))
show('', Counting({}))
show('abcdabcz', Indexed('xyz'))
show('abc' * 3, Counting({ord('a'): True, ord('b'): 'é'}))
print(type('abc'.translate({ord('a'): Letter('A')})))
print('tab\tand\nlines'.translate({9: ' ', 10: ' '}))
