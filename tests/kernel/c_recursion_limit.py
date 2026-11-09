# C recursion in an extension (pyexpat's content-model conversion) checks the
# C stack, as CPython's _Py_EnterRecursiveCall does: a RecursionError, not a
# crash.
from xml.parsers import expat

depth = 100000
parser = expat.ParserCreate()
parser.ElementDeclHandler = lambda name, model: None
document = '<!DOCTYPE root [<!ELEMENT root %s>]><root/>' % ('(' * depth + 'a' + ')' * depth)
try:
    parser.Parse(document, True)
    print('parsed')
except RecursionError as e:
    print('RecursionError', str(e).startswith('Stack overflow'))
