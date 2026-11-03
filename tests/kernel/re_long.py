# Long subjects and deep repeats: the matcher's backtracking stack is heap, not frames.
import re

s = 'ab' * 1000 + 'c'
print(re.match(r'(?:ab)*c', s).span())
m = re.match(r'(a|b)*c', s)
print(m.span(), m.group(1), m.lastindex)
print(re.match(r'(?:a|b)*?c', s).span(), re.match(r'(?:(a)|b)*c', s).span(), re.match(r'(?:ab)*?c', s).span())
print(re.match('.*c', s).span(), re.match('.*?c', s).span(), re.match('[ab]*+c', s).span(), re.match('(?>[ab]*)c', s).span())
print(re.fullmatch(r'[ab]*c', s).span(), re.search(r'c$', s).span(), re.search(r'x', s), re.search(r'(?:ab)*x', s))
print(len(re.findall(r'\w', s)), len(re.findall(r'b', s)), len(re.sub('a', 'xy', s)), len(re.split('b', s)))
print(re.sub(r'(a)(b)', r'\2\1', s)[:8], sum(1 for _ in re.finditer('ab', s)))
print(re.match(r'(?:ab){1000}c', s).span(), re.match(r'(?:ab){1001}c', s), re.match(r'(?:ab){10,}c', s).span(), re.match(r'(?:ab){,1000}c', s).span())
print(re.match(r'(a+)+b', 'a' * 20 + 'b').span(), re.match(r'(a|ab)*c', 'ab' * 600 + 'c').span())
print(re.match('(' * 60 + 'a' + ')' * 60, 'a').groups()[:2], re.match('(?:' * 60 + 'a' + ')' * 60, 'a'))

big = 'x' * 5000
print(re.search('y', big), re.search('x$', big).span(), re.match('x*', big).span(), re.match('x*y', big), re.search(r'(x)\1', big).span())
print(re.search('xxxxy', big), len(re.findall('xx', big)), len(re.findall('', big)))

wide = 'ж\U0001f600' * 400
print(re.match(r'(?:ж\U0001f600)*', wide).span(), len(re.findall('[Ѐ-ӿ]', wide)), re.search('\U0001f600$', wide).span())
print(len(re.sub('ж', 'ab', wide)), re.fullmatch(r'.*', wide).span(), re.search(r'x', wide))
