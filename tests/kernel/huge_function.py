# A function of 10,000 statements compiles to the same bytecode and runs: the
# compiler's passes stay linear in the function's size (EXTENDED_ARG jumps
# included: the loop body outgrows a two-byte argument).
N = 10_000
source = "\n".join([
    "def f(output):",
    "    output.append(1)",
    "    for _ in spam:",
    *(f"        output.append({i})" for i in range(3, N)),
    f"    output.append({N})",
])
namespace = {"spam": ()}
exec(source, namespace)
f = namespace["f"]
out = []
f(out)
print(out)
code = f.__code__
print(len(code.co_code), sum(code.co_code), code.co_stacksize, len(code.co_consts))
print(sum(1 for _ in code.co_lines()), sum(1 for _ in code.co_positions()))
namespace["spam"] = (0,)
out = []
f(out)
print(len(out), out[:3], out[-2:])
