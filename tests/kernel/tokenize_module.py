# tokenize over _tokenize.TokenizerIter, with and without the extra tokens.
import io
import tokenize

source = 'def f(x, *, y=1):\n    """doc"""  # note\n    return f"{x!r:>{y}}" + "a\\\nb"  \n\nclass C: pass\n'
for token in tokenize.generate_tokens(io.StringIO(source).readline):
    print(token)
print(list(tokenize.tokenize(io.BytesIO(source.encode()).readline))[:3])
try:
    list(tokenize.generate_tokens(io.StringIO('x = (1,\n').readline))
except tokenize.TokenError as e:
    print('TokenError', e)
print(tokenize.untokenize(tokenize.generate_tokens(io.StringIO('a = 1 + 2\n').readline)))
