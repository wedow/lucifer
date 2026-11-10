# Misbehaving multi-phase extension modules (_testmultiphase's bad
# PyInit_ functions) raise SystemError at import instead of crashing.
import importlib.machinery
import importlib.util
import _testmultiphase

names = ['bad_slot_large', 'bad_slot_negative', 'create_int_with_state', 'negative_size',
         'export_null', 'export_uninitialized', 'export_raise', 'export_unreported_exception',
         'create_null', 'create_raise', 'create_unreported_exception', 'nonmodule_with_exec_slots',
         'exec_err', 'exec_raise', 'exec_unreported_exception', 'multiple_create_slots',
         'multiple_multiple_interpreters_slots']
for base in names:
    name = '_testmultiphase_' + base
    loader = importlib.machinery.ExtensionFileLoader(name, _testmultiphase.__file__)
    spec = importlib.util.spec_from_loader(name, loader)
    try:
        module = importlib.util.module_from_spec(spec)
        loader.exec_module(module)
        print(base, 'loaded')
    except SystemError as e:
        print(base, 'SystemError', e.__cause__ is not None)
