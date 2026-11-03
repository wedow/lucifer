import sys

# What the harness feeds in: two lines (the second ends \r\n) and a last one without a newline.
print(repr(input()))
print(repr(input("prompt> ")))
print(repr(sys.stdin.readline()))
try:
    input()
except EOFError as e:
    print("EOFError", e)
print(repr(sys.stdin.read()), sys.stdin.readline() == "", sys.stdin.closed)
sys.stdin.close()
print(sys.stdin.closed, sys.__stdin__.closed)
try:
    input()
except ValueError as e:
    print("ValueError", e)
sys.stdin = None
try:
    input()
except RuntimeError as e:
    print(e)
sys.stderr = None
sys.stdin = sys.__stdin__
try:
    input()
except (ValueError, EOFError) as e:
    print(type(e).__name__)
sys.stdout = None
try:
    input()
except RuntimeError as e:
    sys.__stdout__.write(str(e) + "\n")
