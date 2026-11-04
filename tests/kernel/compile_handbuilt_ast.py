# compile() of a tree built by hand: end_lineno and end_col_offset default to
# lineno and col_offset; lineno and col_offset are required.
import ast

tree = ast.Module(body=[ast.Import(names=[ast.alias(name='os', lineno=1, col_offset=7)], lineno=1, col_offset=0),
                        ast.Expr(value=ast.Call(func=ast.Name(id='print', ctx=ast.Load(), lineno=2, col_offset=0),
                                                args=[ast.Attribute(value=ast.Name(id='os', ctx=ast.Load(), lineno=2, col_offset=6),
                                                                    attr='sep', ctx=ast.Load(), lineno=2, col_offset=6)],
                                                keywords=[], lineno=2, col_offset=0), lineno=2, col_offset=0)],
                  type_ignores=[])
exec(compile(tree, '<built>', 'exec'))
print(tree.body[0].end_lineno, tree.body[0].end_col_offset)

try:
    compile(ast.Module(body=[ast.Pass()], type_ignores=[]), '<built>', 'exec')
except TypeError as e:
    print(type(e).__name__, e)

try:
    exec(compile(ast.Module(body=[ast.Expr(value=ast.Name(id='undefined_name', ctx=ast.Load(), lineno=3, col_offset=4),
                                           lineno=3, col_offset=4)], type_ignores=[]), '<built>', 'exec'))
except NameError as e:
    print(type(e).__name__, e, e.__traceback__.tb_next.tb_lineno)
