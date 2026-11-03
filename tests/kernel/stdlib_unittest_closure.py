# The import closure of unittest and test.support.
import importlib

for name in ('os', 'warnings', 'traceback', 'pickle', 'copy', 'unicodedata', 'locale', 'contextlib',
             'difflib', 'pprint', 'string', 'weakref', 'signal', 'unittest', 'test.support'):
    module = importlib.import_module(name)
    print(name, module.__name__)
