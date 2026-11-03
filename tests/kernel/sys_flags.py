# sys.flags and the other struct sequences; run again with -X and -W flags (tools/kernelcheck).
import sys

flags = sys.flags
print(flags)
print(type(flags), type(flags).__name__, type(flags).__module__, type(flags).__doc__)
print(len(flags), flags.n_fields, flags.n_sequence_fields, flags.n_unnamed_fields)
print(flags[0], flags[-1], flags[1:3], tuple(flags) == tuple(flags[:]), flags.__match_args__[:3])
print(flags.gil, flags.thread_inherit_context, flags.context_aware_warnings)
print(flags.hash_randomization, flags.int_max_str_digits, flags.safe_path, flags.dev_mode)
print(flags.__reduce__()[1][1])
print(flags.__replace__(debug=3).debug, flags.debug)
for attempt in (lambda: type(flags)(), lambda: setattr(flags, "debug", 1), lambda: delattr(flags, "debug"),
                lambda: setattr(flags, "nothing", 1), lambda: flags.__replace__(nothing=1)):
    try:
        attempt()
    except (TypeError, AttributeError) as e:
        print(type(e).__name__, e)

vi = sys.version_info
print(vi, vi.major, vi.releaselevel, vi >= (3, 14), vi[:2], vi == (3, 14, 10, "final", 0))
print(type(vi).__name__, vi.__match_args__, hash(vi) == hash(tuple(vi)))
print(sys.hexversion == vi.major << 24 | vi.minor << 16 | vi.micro << 8 | 0xF0, sys.abiflags)
print(sys.implementation.name, sys.implementation.cache_tag, sys.implementation.version == vi)
print(type(sys.implementation).__name__, type(sys.implementation).__module__, sys.implementation.hexversion == sys.hexversion)
print(sys.hash_info, sys.int_info)
print(sys.float_info.max, sys.float_info.dig, sys.float_info)
print(sys.maxsize, sys.maxunicode, sys.byteorder, sys.platform, sys.api_version, sys.float_repr_style)
print(sys.getrecursionlimit(), sys.get_int_max_str_digits(), sys.getdefaultencoding())
print(sys.getfilesystemencoding(), sys.getfilesystemencodeerrors(), sys.getswitchinterval())
print(sys.get_asyncgen_hooks(), type(sys.get_asyncgen_hooks()).__name__)
print(sys.intern("ab" + "c") is sys.intern("abc"), sys.argv[0].endswith("sys_flags.pyc"), len(sys.argv))
print(sys.warnoptions, sys.dont_write_bytecode, sys._xoptions)
print(sys._is_gil_enabled(), sys.is_finalizing(), sys.getrefcount(sys) > 1, sys.executable.endswith(("lucifer", "python3.14t")))
print(sorted(k for k in vars(sys.implementation) if k != "lucifer"))
try:
    sys.intern(3)
except TypeError as e:
    print(e)
print(sys.exception(), sys.exc_info())
try:
    1 / 0
except ZeroDivisionError:
    print(sys.exception().__class__.__name__, sys.exc_info()[0].__name__)
print(sys.modules["sys"] is sys, sys.modules["builtins"].__name__, sys.builtin_module_names.__class__.__name__)
print(type(sys.meta_path), len(sys.meta_path) >= 3, sys.path_hooks.__class__, sys.path_importer_cache.__class__)
