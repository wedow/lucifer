# A generator consumed by a loop that compiled code resumes in line
# (ast.iter_child_nodes over ast.iter_fields, walked as ast.walk walks).
# Entering it in line makes it running; the check that the entry did not
# decline must never leave under -X lucifer_stress=deopt:N, or the
# interpreter resumes a generator that is already running ("generator
# already executing" in the kernelcheck jit-deopt lane).
import ast

tree = ast.parse("def f(a, b=1):\n    return a + b\nx = [f(i) for i in range(3)]\n")


def walk(node):
    todo = [node]
    while todo:
        node = todo.pop(0)
        todo.extend(ast.iter_child_nodes(node))
        yield node


for k in range(60):
    n = sum(1 for _ in walk(tree))
print(n, [type(x).__name__ for x in ast.iter_child_nodes(tree.body[0])])
