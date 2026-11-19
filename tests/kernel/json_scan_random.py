# json.loads over random documents and mutations of them (broken
# delimiters, stray characters, truncation) with each hook: every value,
# and every error's message and position, as the C scanner gives them;
# arrays and objects nested 400 deep.
import json
import random

random.seed(11)
ATOMS = [None, True, False, 0, -1, 12345678901234567890, 1.5, -2e-5, 'x', '', 'caf\xe9 € \U0001f600', 'a"b\\c\n']


def document(depth=0):
    r = random.random()
    if depth > 4 or r < 0.4:
        return random.choice(ATOMS)
    if r < 0.7:
        return [document(depth + 1) for _ in range(random.randrange(4))]
    return {random.choice(['a', 'b', 'k\xe9', '']): document(depth + 1) for _ in range(random.randrange(4))}


def mutate(text):
    t = list(text)
    for _ in range(random.randrange(3)):
        op, i = random.randrange(3), random.randrange(len(t) + 1)
        ch = random.choice('{}[]:," \n\t\\u0123456789eE.-+aflnrstuINy')
        if op == 0 and t:
            del t[min(i, len(t) - 1)]
        elif op == 1:
            t.insert(i, ch)
        elif t:
            t[min(i, len(t) - 1)] = ch
    return ''.join(t)


HOOKS = [{}, {'object_pairs_hook': lambda p: ('P', p)}, {'object_hook': lambda d: ('H', sorted(d.items(), key=repr))},
         {'parse_float': lambda x: ('F', x), 'parse_int': lambda x: ('I', x), 'parse_constant': lambda x: ('C', x)},
         {'strict': False}]
for k in range(400):
    doc = json.dumps(document(), indent=random.choice([None, 1, '\t']), ensure_ascii=random.random() < 0.5)
    for text in (doc, mutate(doc), ' ' + doc[:random.randrange(len(doc) + 1)]):
        hooks = random.randrange(len(HOOKS))
        try:
            print(k, hooks, ascii(json.loads(text, **HOOKS[hooks]))[:200])
        except json.JSONDecodeError as e:
            print(k, hooks, 'error', e.msg, e.pos)
for opener, closer in (('[', ']'), ('{"a":', '}')):
    value, depth = json.loads(opener * 400 + '[1, {"b": null}]' + closer * 400), 0
    while isinstance(value, (list, dict)) and value != [1, {'b': None}]:
        value, depth = value[0] if isinstance(value, list) else value['a'], depth + 1
    print(depth, value)
