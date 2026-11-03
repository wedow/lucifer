# _opcode: the opcode properties and stack effects, against every opcode number.
import _opcode
import dis
import opcode

for name in ('is_valid', 'has_arg', 'has_const', 'has_name', 'has_jump', 'has_free', 'has_local', 'has_exc'):
    f = getattr(_opcode, name)
    print(name, [i for i in range(-3, 700) if f(i)])
print(_opcode.get_intrinsic1_descs(), _opcode.get_intrinsic2_descs(), _opcode.get_special_method_names())
print(_opcode.get_nb_ops(), _opcode.ENABLE_SPECIALIZATION, _opcode.ENABLE_SPECIALIZATION_FT)
effects = []
for op in sorted(set(opcode.opmap.values())):
    for oparg in (None, 0, 1, 2, 3, 5, 8, 17):
        for jump in (None, True, False):
            try:
                effects.append(_opcode.stack_effect(op, oparg, jump=jump))
            except ValueError as e:
                effects.append(str(e))
print(effects)
for bad in ((1000,), (1, 2, 3), (1, 2)):
    try:
        print(_opcode.stack_effect(*bad, jump=2))
    except (TypeError, ValueError) as e:
        print(type(e).__name__, e)
print(opcode.opname[:20], opcode.hasarg == sorted(opcode.hasarg), len(opcode.hasjump), opcode.hasexc, dis.stack_effect(opcode.opmap['LOAD_CONST'], 0))
print(dis.opname[100:110], opcode.hascompare, len(opcode.hasname))
