# The installed standard library is on sys.path, found from the executable.
import os
import sys
import posixpath

print(sys.prefix != '' and os.path.isdir(sys.prefix))
print(any(os.path.isfile(os.path.join(p, 'os.py')) for p in sys.path))
print(os.__file__.endswith('os.py'), posixpath.__file__.endswith('posixpath.py'))
print(os.path.isfile(os.path.join(os.path.dirname(os.__file__), 'test', 'support', '__init__.py')))
import string, textwrap, bisect, heapq, random, shutil
print(string.__file__.endswith('string.py'), textwrap.__spec__.cached.endswith('.pyc'))
