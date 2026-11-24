---
id: luc-mopl
status: open
deps: []
links: []
created: 2026-11-22T22:12:37Z
type: task
priority: 1
parent: luc-orn7
tags: [perf, jit]
---
# jit: no compiles in steady state

Measured: on 14 pyperformance rows the JIT compiler is 11% and the interpreter 7% of cycles between the 4th and the 23rd value, long after timings are flat (django_template 18% + 20%, logging 17% + 12%, xdsl 16% + 10%, deepcopy 14% + 16%). A program that has reached steady speed should compile nothing and interpret nothing.

Task: record, per unit, why it was built again or retired after the third value (which guard, which assumption, which budget), across those rows; then fix the general causes found (a guard that fails forever should be compiled as a branch, not rebuilt; a retired unit's function should not fall back to the interpreter while its replacement is built). No thresholds tuned per program.

Acceptance: compiles between values 10 and 23 are zero on at least 12 of the 14 rows; compiler + interpreter share there under 3%.


## Notes

**2026-11-24 diagnosis (master 9fcc18bc + event log)**

Method: each program as a pyperformance worker, -n 23, PYTHONHASHSEED=0, LUCIFER_JIT_DEBUG=units (one line per unit build and death, wall-clock stamped). Events are placed in values 4-23 and 10-23 of each sub-benchmark from pyperf's date, duration and values. Compile share = wall time inside compile() over the window. Exits and interpreted entries: exit tallies of the n=23 run minus the n=3 run. Interpreter share: perf cycles with -X perf, n=23 minus n=3, interpreter samples charged to the innermost Python frame.

Two corrections to the epic's attribution. (1) Its "JIT compiler" bucket is everything under runtime/jit, which includes helpers.c (calls compiled code makes: h_prim_call, h_prim, call_argv; mean 4.5%) and jit.c's entry path (lu_jit_run, lu_jit_enter; mean 2.4%). (2) Both LUCIFER_JIT_DUMP and -X perf inflate compile time: under -X perf genshi builds the same units count but XMLSerializer.__call__ grows to 35,370 IR instructions (6,129 without), 4.8 s of compile per run instead of 1.1 s. Unperturbed compile share is below.

Steady compiles by cause (builds / compile ms), values 4-23 and 10-23:

| program | 4-23 compile % | 10-23 compile % | 10-23 builds | 4-23 causes (builds/ms) | interpreter % (perf) |
|---|---|---|---|---|---|
| logging | 6.0 | 2.8 | 20 | grown 8/98, stale 24/90, first 17/20, marks 7/18 | 8.2 (Logger._log after sys._getframe exits) |
| xdsl | 7.3 | 7.5 | 40 | stale 31/636, grown 18/311, hot 3/181, marks 16/103, retired 11/12 (same IR) | 7.0 (import and yield-from retire loops) |
| genshi | 1.9 | 0.7 | 85 | first 55/83, hot 2/58, stale 47/41, marks 26/39, type fails 19/8 | 8.0 |
| deepcopy | 1.6 | 1.3 | 37 | stale 17/36, type fails 14/32, first 15/21, marks 15/22, grown 1/19, retired 6 (same IR) | 10.6 (waits after deaths, import retire loops) |
| regex_v8 | 11.8 | 7.7 | 144 | grown 5/1209, stale 59/300, hot 2/191, first 134/133, marks 164/110 | 5.3 (new pattern code waits) |
| django_template | 1.5 | 1.3 | 19 | stale 17/54, marks 5/16, hot 1/11, first 11/9, retired 4 (same IR) | 6.6 (chain.__next__ raise exits) |
| chameleon | 4.1 | 0.4 | 9 | stale 4/160, hot 1/158, first 4/15 | 3.7 |
| deltablue | 6.7 | 4.9 | 71 | stale 41/171, marks 31/57, hot 4/46, grown 6/30, dep-value 1/25, first 18/20 | 3.8 |
| pprint | 0.0 | 0.0 | 10 | first 5/16 | 3.3 |
| go | 2.6 | 0.9 | 19 | stale 17/40, first 23/33, hot 3/30 | 0.6 |
| json_loads | 0.4 | 0.4 | 14 | first 9/23, stale 7/22 | 3.6 |
| json_dumps | 0.4 | 0.1 | 10 | stale 3/47, first 5/10 | 5.3 |
| raytrace | 9.4 | 6.2 | 16 | grown 4/236, marks 5/225, hot 3/106, stale 3/72 | 2.7 |
| richards | 7.6 | 1.3 | 14 | first 45/39, stale 28/24, marks 38/31 | 0.9 |

Mean compile share 4.4% (values 4-23), 2.6% (10-23). Programs with zero compiles in 10-23: none.

General causes, summed over the 14 programs (percentage points of each program's window, values 4-23 / 10-23; builds 4-23):

| cause | 4-23 pp | 10-23 pp | builds | what happens |
|---|---|---|---|---|
| A profile staleness (learned-since-build) | 18.9 | 14.4 | 301 | a build inlines new call chains; their profiles start empty (profiles are keyed by the chain of inlined frames), learn on first run, and the stale count rebuilds the unit; the rebuild inlines further and repeats. 57-95% (median 72%) of steady learn events are a site and type already learned in another context or root. |
| B budget growth (grown) | 16.2 | 9.9 | 43 | a unit that left calls out for budget doubles it at 4096 entries, up to 4 times; units entered once per value reach 4096 late, and each doubling is a larger build (regex_v8 5 builds 1.2 s, raytrace 4 builds 236 ms) that starts cause A again |
| E guard marks (mark-*, old-unit-*) | 9.3 | 3.0 | 321 | a site's guard reaches its threshold, every unit of the root dies; regex_v8 (new pattern code), deltablue, richards |
| C hot out-of-line site | 7.9 | 3.9 | 20 | a call left out of line turns out hot, the unit is rebuilt with it in line (large builds) |
| F first build of a code | 7.3 | 3.7 | 355 | regex_v8 specializes patterns as each one's budget runs out (new code objects); short sub-benchmarks still warming; harness code between sub-benchmarks |
| D learned type/kind fails | 1.1 | 0.8 | 61 | |
| H dependency | 0.6 | 0.0 | 3 | |
| G retire loop (identical IR) | 0.1 | 0.0 | 21 | cheap builds, but the waits between them are interpreter time (below) |

Interpreter time in steady state, by cause (exits and interpreted entries per 20 values):
- Retire loops at instructions the tier does not compile: IMPORT_NAME (deepcopy _common_reduce and _getstate_default, 261K entries interpreted; xdsl SSAValue.get 60K, irdl_op_init 50K), GET_YIELD_FROM_ITER (xdsl Block.walk 80K); a kept polymorphic-cache guard (xdsl Operation.prev_op 80K). Each rebuild is IR-identical (`same`) and the level doubles the wait.
- An exception raised as a side exit: django_template chain.__next__ RAISE_VARARGS (StopIteration) 404K exits.
- Frame exits: logging Logger._log -> sys._getframe 113K exits; the rest of _log runs interpreted (3.9% of cycles).
- Waits after a death that asked for a rebuild: entries count, not work, so a loop entry that is a whole deepcopy waits (WARMUP + code size) << level iterations interpreted (deepcopy benchmark@43, 5.3% of cycles).
- Back-edges into code built for another way in (unit entered inside a loop): deltablue 2.6K, raytrace 1.5K, json_dumps 560, deepcopy 560 exits.
- Module code (not CO_OPTIMIZED) refused: genshi template <module> 0.8%.

**2026-11-24 C under compiled code (14 rows, values 4-13)**

Method: each program as a worker, -n 13, PYTHONHASHSEED=0, perf cycles with frame-pointer call chains (monotonic clock), LUCIFER_JIT_DUMP for unit addresses (the dump now names each IR instruction's bytecode site, opcode, kernel flag and constant operands) and the event log for build times; samples placed in values 4-13 by pyperf's timestamps. A sample whose chain reaches a unit is charged to the IR instruction at the return address (the helper or direct C call compiled code made), with its leaf function; samples under compile() are left out and so are the shares' denominators (the dump inflates compiling). The dump and perf do not change which units exist: 95-98% of builds per program have the same root and IR size as an events-only run, and no root is built on one side only except one in each of three rows.

Mean share of all non-compile cycles: compiled code 46.3%, C under compiled code 45.8%, the interpreter running code compiled code called (callees not compiled) 6.1%, interpreter otherwise 1.8%. Python-written kernel code calling C: 21.4% of all cycles, 47% of the C-under-compiled time. Per row (C under compiled / compiled / interpreter under compiled / interpreter / kernel calling C): logging 52/26/17/5/21, xdsl 54/37/8/1/35, genshi 50/35/15/0/31, deepcopy 50/29/20/1/22, regex_v8 40/50/5/5/6, django_template 57/36/7/0/36, chameleon 56/39/2/3/38, deltablue 45/49/4/2/17, pprint 60/39/0/0/41, go 41/55/1/3/8, json_loads 42/55/1/1/15, json_dumps 63/35/2/1/25, raytrace 29/68/2/2/4, richards 2/95/1/2/1.

Where the C time goes, by leaf function (mean % of all cycles): the operation's own work about 15.4, call and helper glue (h_prim_call, the thread-state lookup lu_tstate/lu_current_task, lu_jit_direct, call_argv, generic getattr/setattr) about 9.7, allocation, release and collector 7.6, dict and namespace probes 4.0, locks and atomics 2.7, reference counts 2.4, type lookup 2.0, unsymbolized 1.7, copying 0.3. By family: kernel primitive calls (h_prim, h_prim_call, h_prim_words) 16.0, other helpers 9.3, calls through C (h_call, call_spread, call_ex) 8.0, generic attribute loads and stores 7.0, releases at a DECREF (loop back-edge, return, yield) 5.6.

Rows to 80% of the C-under-compiled time (site opcode -> IR instruction -> leaf kinds):

| # | site op | IR op:helper | mean % of all cycles | cum % of C-under-compiled | where the C time goes | top leaf functions | constant operands seen | per program (% of its cycles) |
|---|---|---|---|---|---|---|---|---|
| 1 | CALL | HELPER:call | 3.71 | 8 | work 85%, refcount 6%, alloc/free 5%, dict probe 3% | call_argv.part.0, lu_jit_direct, h_prim | const=method_descriptor; fn=UCTNode.random_playout | loggin 0.8 xdsl 6.4 genshi 1.5 deepco 6.7 regex_ 3.9 django 2.0 chamel 6.8 deltab 4.3 pprint 7.3 go 3.3 json_l 2.4 json_d 5.4 raytra 0.8 richar 0.2 |
| 2 | LOAD_ATTR | HELPER:load_attr | 2.83 | 14 | work 56%, type lookup 17%, dict probe 10%, refcount 9% | type_lookup, lu_getattr_native, lu_tstate | str=__dict__; type=int str=__repr__; str=append | loggin 15.0 xdsl 2.7 genshi 3.7 deepco 2.0 regex_ 4.8 pprint 6.1 json_l 1.6 json_d 1.7 raytra 2.0 |
| 3 | FOR_ITER | JUMP | 2.50 | 20 | alloc/free 48%, work 34%, refcount 13%, lock 4% | Py_DECREF, lu_subtype_dealloc, dealloc_now |  | loggin 2.4 xdsl 1.8 genshi 0.8 deepco 2.7 regex_ 0.8 django 3.4 chamel 0.3 deltab 0.9 pprint 6.1 go 5.4 json_l 0.8 json_d 1.3 raytra 8.2 |
| 4 | RETURN_VALUE | RETURN | 1.95 | 24 | alloc/free 43%, work 37%, refcount 15%, lock 4% | Py_DECREF, _Py_Dealloc, dealloc_now |  | loggin 2.8 xdsl 1.3 genshi 1.6 deepco 2.1 regex_ 2.6 django 2.3 chamel 0.2 deltab 6.1 pprint 3.4 json_l 1.7 json_d 2.9 |
| 5 | CALL_FUNCTION_EX | HELPER:call | 1.84 | 28 | work 94%, type lookup 3%, refcount 2%, alloc/free 1% | lu_jit_direct, call_argv.part.0, lu_kernel_target | fn=chain.__next__; const=wrapper_descriptor | loggin 1.6 xdsl 2.8 genshi 3.0 deepco 1.7 regex_ 0.3 django 6.8 chamel 3.0 deltab 0.4 pprint 3.6 json_d 2.1 raytra 0.2 |
| 6 | CALL | HELPER:prim_call:list_append | 1.18 | 31 | work 70%, alloc/free 19%, lock 9%, refcount 2% | prim_list_append, h_prim_call, [unknown] | const=builtin_function_or_method; const=builtin_function_or_method str=<span; co | loggin 0.3 xdsl 0.2 genshi 1.2 deepco 1.8 django 1.3 chamel 1.7 deltab 2.2 pprint 2.1 go 1.2 json_d 4.5 |
| 7 | STORE_SUBSCR | HELPER:setitem | 1.16 | 33 | work 46%, dict probe 40%, alloc/free 7%, lock 6% | probe_keys, lu_dict_insert, lu_dict_put_native | str=counter0; str=first | xdsl 0.2 genshi 0.4 deepco 3.9 django 3.0 pprint 1.1 json_l 6.0 json_d 1.4 |
| 8 | CALL | HELPER:prim:type_lookup | 1.14 | 36 | type lookup 65%, refcount 20%, work 15%, dict probe 0% | type_lookup, lu_try_incref_fast, prim_type_lookup | const=builtin_function_or_method str=__get__; const=builtin_function_or_method;  | loggin 0.3 xdsl 2.7 genshi 1.3 deepco 1.3 django 2.2 chamel 2.1 pprint 3.8 json_d 1.6 raytra 0.2 |
| 9 | BINARY_OP | HELPER:str_concat | 1.09 | 38 | work 61%, alloc/free 29%, copy 9%, lock 1% | _mi_page_malloc_zero, join_items, [unknown] | str="; str=column-; str=\u | chamel 1.3 json_d 14.0 |
| 10 | LOAD_ATTR | HELPER:load_method | 1.03 | 40 | work 54%, type lookup 29%, refcount 9%, alloc/free 6% | type_lookup, lu_getattr_native, attr_shortcut | type=dict str=__len__; type=object str=__new__; str=isPoint | loggin 0.8 xdsl 1.9 regex_ 0.4 django 0.8 chamel 0.3 deltab 4.0 pprint 0.5 json_l 0.3 json_d 3.3 raytra 1.7 |
| 11 | CALL | HELPER:prim:str_slice | 1.02 | 42 | work 76%, alloc/free 23%, lock 1%, copy 0% | PyUnicode_FromKindAndData, _mi_page_malloc_zero, index_arg | const=builtin_function_or_method; const=builtin_function_or_method str= class="% | loggin 0.8 genshi 0.3 regex_ 1.9 chamel 1.2 deltab 0.3 pprint 0.4 json_l 7.8 json_d 1.5 |
| 12 | CALL_FUNCTION_EX | HELPER:call_spread | 0.98 | 45 | work 85%, alloc/free 7%, refcount 3%, dict probe 2% | call_argv.part.0, h_call_spread, lu_jit_direct | const=tuple; fn=call_special | loggin 1.0 xdsl 3.2 genshi 1.0 deepco 1.3 django 3.4 chamel 1.6 deltab 1.2 raytra 0.7 |
| 13 | CALL | HELPER:prim:str_join | 0.95 | 47 | work 69%, copy 16%, alloc/free 14%, lock 1% | join_items, [unknown], copy_into | const=builtin_function_or_method str=; const=builtin_function_or_method; const=b | loggin 0.7 xdsl 0.6 genshi 0.4 regex_ 0.7 django 0.6 chamel 2.5 deltab 0.5 pprint 2.3 json_d 5.0 |
| 14 | CALL | HELPER:list_append | 0.80 | 48 | work 52%, alloc/free 35%, lock 10%, refcount 3% | _mi_page_malloc_zero, lu_tstate, h_list_append | str={; str= | loggin 0.9 xdsl 0.3 genshi 0.6 deepco 0.3 regex_ 1.3 django 0.2 chamel 1.5 deltab 0.4 pprint 0.9 json_l 0.7 json_d 3.7 raytra 0.4 |
| 15 | STORE_ATTR | HELPER:store_attr | 0.80 | 50 | work 60%, type lookup 24%, refcount 5%, lock 5% | type_lookup, lu_setattr_native, lu_tstate | str=lastindex; str=lastmark; str=ptr | xdsl 3.2 regex_ 4.5 deltab 3.5 |
| 16 | STORE_ATTR | HELPER:attr_store | 0.80 | 52 | work 41%, dict probe 39%, lock 8%, refcount 7% | probe_unicode, lu_dict_replace, lu_ns_store | str=temp_ledges; str=ledges; str=reference | go 10.9 |
| 17 | LOAD_SUPER_ATTR | HELPER:super_attr | 0.77 | 54 | work 69%, dict probe 13%, type lookup 11%, refcount 4% | ns_find_ref, type_lookup, probe_unicode | type=super type=Scope str=get; type=super type=Variable str=__init__; type=super | xdsl 1.4 chamel 6.3 deltab 3.0 |
| 18 | CALL | HELPER:prim_words:dict_probe | 0.72 | 55 | work 59%, dict probe 40%, refcount 1%, alloc/free 0% | probe_keys, stack_int, prim_dict_probe | const=builtin_function_or_method | xdsl 1.0 genshi 1.2 regex_ 0.2 chamel 0.7 json_l 6.0 json_d 1.0 |
| 19 | CALL | HELPER:prim_call:dict_find | 0.63 | 57 | work 49%, dict probe 45%, lock 5%, refcount 1% | probe_keys, prim_dict_find, h_prim_call | const=builtin_function_or_method; const=builtin_function_or_method str=d; const= | loggin 1.0 xdsl 0.3 genshi 0.6 deepco 2.4 django 2.0 chamel 2.3 |
| 20 | CALL | HELPER:prim:str_find | 0.57 | 58 | work 99%, copy 1% | [unknown], find_in, prim_str_find | const=builtin_function_or_method str=sra const=bool; const=builtin_function_or_m | loggin 3.0 deepco 0.5 chamel 1.1 deltab 0.5 pprint 1.3 raytra 1.0 |
| 21 | CALL | HELPER:prim_call:tuple_hash | 0.53 | 59 | work 67%, alloc/free 20%, dict probe 9%, refcount 2% | _mi_page_malloc_zero, [unknown], probe_unicode | const=builtin_function_or_method | xdsl 0.7 genshi 6.0 regex_ 0.7 |
| 22 | BINARY_OP | HELPER:binary_op | 0.52 | 60 | work 72%, dict probe 17%, alloc/free 4%, lock 3% | probe_keys, lu_subscr_native, h_binary_op | const=dict; const=slice | loggin 2.5 xdsl 0.3 genshi 0.3 deepco 2.4 regex_ 0.5 django 0.3 raytra 0.7 |
| 23 | FOR_ITER | HELPER:seqiter_next | 0.49 | 61 | work 77%, lock 19%, refcount 4% | lu_seqiter_next, lu_tstate, _Py_atomic_load_ptr |  | genshi 0.8 deepco 0.2 deltab 4.2 raytra 1.2 |
| 24 | CONTAINS_OP | HELPER:contains | 0.46 | 62 | work 54%, dict probe 39%, lock 3%, refcount 3% | probe, lu_contains_native, hash_native | const=set; type=int const=set | xdsl 0.5 genshi 0.6 deepco 2.5 pprint 0.8 go 1.1 json_d 0.8 |
| 25 | MAKE_FUNCTION | HELPER:make_function | 0.39 | 63 | work 55%, alloc/free 22%, refcount 11%, dict probe 11% | _mi_page_malloc_zero, make_function, Py_INCREF | const=code const=dict | xdsl 0.4 deepco 0.3 django 1.1 pprint 2.8 json_d 0.8 |
| 26 | CALL | HELPER:call:_scan_once | 0.37 | 64 | work 95%, refcount 4%, alloc/free 1% | lu_jit_direct, call_argv.part.0, lu_native_call | const=builtin_function_or_method | json_l 5.1 |
| 27 | YIELD_VALUE | YIELD | 0.34 | 65 | work 49%, alloc/free 36%, refcount 9%, type lookup 3% | lu_truth_exact.part.0, Py_DECREF, mi_slice_first |  | genshi 4.7 |
| 28 | CALL | INLINE_STORE_F | 0.33 | 65 | work 71%, lock 16%, alloc/free 8%, refcount 5% | lu_unboxed, store_at, take_kind |  | raytra 4.7 |
| 29 | CALL | HELPER:prim_call:str_encode | 0.33 | 66 | work 63%, alloc/free 36%, lock 1%, copy 0% | _mi_page_malloc_zero, [unknown], prim_str_encode | const=builtin_function_or_method str=latin-1 str=strict; const=builtin_function_ | genshi 0.7 regex_ 2.3 chamel 1.5 json_l 0.2 |
| 30 | EXTENDED_ARG | HELPER:for_iter | 0.31 | 67 | work 54%, dict probe 28%, alloc/free 7%, refcount 7% | h_for_iter, lu_dictiter_next, lu_dict_next_entry |  | genshi 0.6 json_d 3.8 |
| 31 | CALL | HELPER:prim_call:seqiter_next | 0.30 | 67 | work 81%, lock 9%, refcount 6%, alloc/free 4% | h_prim_call, lu_seqiter_next, prim_seqiter_next | const=builtin_function_or_method | xdsl 0.6 deepco 0.4 django 3.0 |
| 32 | EXTENDED_ARG | JUMP | 0.30 | 68 | alloc/free 64%, work 16%, refcount 13%, lock 5% | Py_DECREF, lu_subtype_dealloc, mi_free_ex |  | chamel 3.0 json_l 1.2 |
| 33 | FOR_ITER | HELPER:for_iter | 0.30 | 69 | work 68%, dict probe 19%, refcount 6%, lock 5% | h_for_iter, lu_next_missing_of, lu_dictiter_next |  | xdsl 0.5 genshi 0.8 deepco 2.1 pprint 0.7 |
| 34 | LOAD_FAST_BORROW | JUMP | 0.29 | 69 | work 47%, alloc/free 27%, refcount 21%, lock 4% | Py_DECREF, visit_decref_unreachable, visit_decref |  | deltab 3.8 |
| 35 | STORE_GLOBAL | HELPER:store_global | 0.28 | 70 | dict probe 47%, work 40%, alloc/free 6%, lock 4% | probe_unicode, lu_dict_send_event, lu_jit_invalidate | const=dict str=TIMESTAMP; const=dict str=MOVES | go 3.9 |
| 36 | CALL | HELPER:prim_call:object_id | 0.28 | 70 | work 66%, alloc/free 32%, lock 2%, copy 0% | _mi_page_malloc_zero, h_prim_call, [unknown] | const=builtin_function_or_method | xdsl 0.6 deepco 2.1 pprint 0.6 json_d 0.7 |
| 37 | CALL | HELPER:prim_call:instance_load | 0.27 | 71 | work 83%, dict probe 15%, refcount 1%, lock 1% | prim_instance_load, h_prim_call, lu_attr_lookup | const=builtin_function_or_method str=is_safe; const=builtin_function_or_method c | loggin 0.9 xdsl 0.4 deepco 0.8 django 1.6 |
| 38 | CALL | HELPER:prim_call:bind | 0.27 | 72 | work 65%, alloc/free 33%, lock 1%, refcount 1% | [unknown], _mi_page_malloc_zero, h_prim_call | const=builtin_function_or_method; const=builtin_function_or_method const=method_ | xdsl 0.5 deepco 0.2 chamel 2.5 deltab 0.4 |
| 39 | CALL | HELPER:prim_words:dict_insert | 0.26 | 72 | work 65%, dict probe 26%, alloc/free 4%, lock 4% | stack_int, lu_long_as_long, lu_dict_insert | const=builtin_function_or_method | xdsl 0.2 json_l 3.4 |
| 40 | RAISE_VARARGS | HELPER:raise | 0.25 | 73 | work 71%, alloc/free 25%, lock 3%, refcount 1% | lu_raise_native, PyType_GenericAlloc, lu_type_is_subtype | type=StopIteration const=object | django 3.5 |
| 41 | LOAD_ATTR | HELPER:dict_lacks | 0.25 | 73 | dict probe 60%, work 40% | ns_find, probe_unicode, lu_ns_lookup | str=find; str=remove; str=move | go 3.4 |
| 42 | BINARY_SLICE | HELPER:binary_slice | 0.25 | 74 | work 64%, alloc/free 35%, lock 1%, refcount 0% | _mi_page_malloc_zero, slice_unpack, PyUnicode_FromKindAndData |  | loggin 1.9 regex_ 1.4 |
| 43 | CALL | HELPER:prim_call:native_hash | 0.22 | 74 | work 77%, alloc/free 21%, lock 1%, refcount 1% | prim_native_hash, hash_native, h_prim_call | const=builtin_function_or_method; const=builtin_function_or_method type=int; con | xdsl 0.5 genshi 0.4 pprint 2.1 |
| 44 | CALL | HELPER:prim_call:box_get | 0.22 | 75 | work 93%, refcount 7% | h_prim_call, prim_box_get, index_arg | const=builtin_function_or_method | loggin 0.5 chamel 1.0 deltab 0.5 pprint 1.1 |
| 45 | STORE_ATTR | HELPER:slot_store | 0.22 | 75 | lock 42%, work 31%, alloc/free 21%, refcount 7% | lu_unlocked_begin, lu_member_exchange, lu_unlocked_end | const=bool; str=: | regex_ 1.0 django 1.3 json_l 0.3 json_d 0.5 |
| 46 | CALL_FUNCTION_EX | HELPER:call_ex | 0.21 | 76 | work 74%, alloc/free 22%, refcount 3%, lock 1% | _mi_page_malloc_zero, lu_jit_direct, unpack_call | fn=escape; fn=mark_safe | deepco 0.8 django 2.1 |
| 47 | CALL | HELPER:prim_call:iter_new | 0.21 | 76 | work 57%, alloc/free 41%, lock 1%, copy 1% | prim_iter_new, _mi_page_malloc_zero, h_prim_call | const=builtin_function_or_method type=list_iterator; const=builtin_function_or_m | xdsl 0.3 genshi 0.3 deepco 0.2 django 1.3 deltab 0.8 |
| 48 | NOP | JUMP | 0.21 | 77 | work 38%, refcount 26%, alloc/free 25%, dict probe 7% | Py_DECREF, visit_decref, visit_decref_unreachable |  | xdsl 2.7 |
| 49 | LIST_EXTEND | HELPER:list_extend | 0.21 | 77 | alloc/free 49%, work 45%, refcount 5%, lock 1% | _mi_page_malloc_zero, h_list_extend, PyMem_Calloc |  | loggin 0.5 chamel 1.1 deltab 0.3 pprint 0.6 |
| 50 | CALL | HELPER:prim:int_format | 0.21 | 78 | work 66%, alloc/free 33%, lock 1% | format_decimal.isra.0, _mi_page_malloc_zero, prim_int_format | const=builtin_function_or_method const=bool | genshi 0.3 django 0.9 chamel 0.8 deltab 0.4 pprint 0.4 json_d 0.2 |
| 51 | CALL | HELPER:prim_call:frame_at | 0.21 | 78 | work 77%, alloc/free 19%, refcount 3% | tcache_get_n, __libc_calloc, take | const=builtin_function_or_method | loggin 2.9 |
| 52 | CALL | HELPER:prim_call:list_pop | 0.20 | 78 | work 74%, lock 14%, alloc/free 11%, copy 1% | prim_list_pop, h_prim_call, [unknown] | const=builtin_function_or_method | genshi 0.5 deltab 1.5 go 0.7 |
| 53 | CALL | HELPER:prim_call:alloc | 0.20 | 79 | alloc/free 54%, work 44%, lock 1%, refcount 1% | _mi_page_malloc_zero, check_new.part.0, PyType_GenericAlloc | const=builtin_function_or_method type=object; const=builtin_function_or_method t | xdsl 0.3 deepco 0.6 regex_ 0.4 django 0.7 json_l 0.3 json_d 0.4 |
| 54 | CALL | HELPER:prim_call:str_subtype | 0.20 | 79 | alloc/free 71%, work 28%, lock 1% | _mi_page_malloc_zero, heap_malloc, prim_str_subtype | const=builtin_function_or_method type=SafeString; const=builtin_function_or_meth | genshi 1.3 django 1.4 |
| 55 | CALL | HELPER:prim_call:range_iter | 0.19 | 80 | work 60%, alloc/free 37%, lock 3%, dict probe 0% | _mi_page_malloc_zero, prim_range_iter, lu_long_as_long | const=builtin_function_or_method; const=builtin_function_or_method const=range | xdsl 0.3 genshi 0.4 regex_ 0.5 pprint 0.2 richar 0.9 |
| 56 | LIST_APPEND | HELPER:list_push | 0.19 | 80 | alloc/free 46%, work 46%, lock 5%, refcount 3% | _mi_page_malloc_zero, [unknown], PyMem_Calloc | const=object | deepco 0.3 go 0.5 raytra 1.5 |

What would keep the top 10 rows in compiled code:
1. CALL through h_call (call_argv, lu_jit_direct): bind the callee at compile time and call its unit directly, guarded on the function version; a method_descriptor's kernel target called directly (specialized helper).
2. LOAD_ATTR generic (type_lookup, lu_getattr_native; logging's instance __dict__ 15% of its cycles): more shapes in the polymorphic cache, among them a managed __dict__ read that does not move the values to a dict, read in line.
3. FOR_ITER back-edge release (Py_DECREF -> dealloc): in-line release for exact types, and no allocation for loop temporaries (unboxed values, escape analysis): luc-eud7, luc-uqyj.
4. RETURN release: the same release fast path for locals freed at return.
5. CALL_FUNCTION_EX through h_call (chain.__next__, wrapper_descriptor): star and keyword calls bound at compile time (luc-3s7n).
6. list.append as a primitive call: in-line append when capacity allows (no C call); its lock goes with serialized mode (luc-6b0l).
7. STORE_SUBSCR dict through h_setitem: a dict-store helper that takes the key's hash as a word (as dict_insert does), or an in-line probe for str keys.
8. type_lookup primitive: fold it under a type guard as of the type's version (luc-szry), else a per-site cache keyed by version.
9. str + str (BINARY_OP str_concat; 14% of json_dumps): append in place when the left operand is a fresh string only the frame holds, or a string-builder representation for accumulate loops.
10. LOAD_ATTR method generic (dict.__len__, object.__new__, isPoint): the method polymorphic cache for those shapes, read in line.

**2026-11-24 stages: fix 3 (measured, dropped) and fix 4**

Fix 3 (not landed: a null result, and the waits it removes were not the cause of the interpreter time it targeted): a death that asked for a rebuild (site marked generic, learned type or kind dropped) builds at the next entry instead of waiting (WARMUP + code size) << level entries; bounded per site (a mark bit once per site, each unit built before it dies once; a profile drops its type once and its kind once). Writer invalidations, retirements, failed compiles and grown budgets keep their waits: a first version also rebuilt grown budgets at once and xdsl then built larger units sooner (IR built per run 609K -> 659K, whole-run instructions +2.8%), so grown waits again. Its test (a loop whose sites fail one after another, then for good) measured entries waited at 2N 29,726 before, 13,527 after under -X lucifer_jit=always; test and lane were dropped with the change.

Fix 4: IMPORT_NAME and IMPORT_FROM are helper calls (FX_RAISE | FX_PY) sharing lu_import_name / lu_import_from with the eval loop. Test tests/kernel/jit_import_hot.py (module imported already and first imported, from m import a, b, a level-0 submodule import, a module replacing itself in sys.modules, a lazy module __getattr__, an ImportError caught in the function, run after the hot loop); jitcheck lane imports (side exits + entries waited 165,235 -> 285,225 from N to 2N with 6 functions retired before; 35,606 -> 35,609 after, none retired). An ImportError caught by the same function still leaves compiled code (luc-kyqx). After review, the test also covers, inside the hot loop: a relative import in a package function (level > 0, its own globals), an import in an inlined callee of another module, builtins.__import__ replaced by a wrapper for a stretch and restored, a module body that rebinds a global the importer reads right after, one that starts and joins a thread, a circular import's error, and `from p import sub` through sys.modules. The lane's `jit: entries .* side (\d+)` matches the stats line `jit: entries N, returns N, direct calls N, compiled calls N, exits guard N stress N poll N side N error N ...`.

Suggested separate compatibility ticket (not fixed here): lu_import_name looks __import__ up in the interpreter's builtins, not the frame's __builtins__. CPython uses the frame's: `exec("def f():\n import os\n return os", {'__builtins__': {'__import__': lambda *a: 42}})`, then f() returns 42 on CPython and the os module on Lucifer (checked on the landing tree; the behaviour predates this change, which shares the eval loop's function with compiled code).

Gate (fix 4, re-run on the landing tree): the new programs and 20-22 neighbours in plain, jit, jit-deopt; 13 thread programs in handoff; full tools/jitcheck: 0 failing (GAP lanes as on master).

Numbers (whole run, n=13 values, PYTHONHASHSEED=0, one core lease per run, two runs per arm; instructions; cycles were noisy on a loaded host, minimum shown):

| program | base -> fix 3 instructions | fix 3 -> fix 4 instructions | fix 3 -> fix 4 cycles (min) |
|---|---|---|---|
| deepcopy | 24.362G -> 24.199G (-0.7%) | 24.191G -> 22.531G (-6.9%) | 18.63G -> 17.77G (-4.6%) |
| xdsl | 40.627G -> 40.957G (+0.8%) | 40.965G -> 40.711G (-0.6%) | 37.71G -> 37.05G (-1.8%) |

Event log, values 4-23 (n=23 minus n=3): deepcopy builds 57 -> 40 -> 38, interpreted entries 274,543 -> 268,371 -> 6,532; xdsl builds 66 -> 69 -> 62, interpreted entries 279,784 -> 278,192 -> 227,614 (what remains: Block.walk's yield from, Operation.prev_op's kept cache guard).

Correction to the diagnosis: deepcopy's interpreted benchmark loop (benchmark@43, 5.3% of cycles) waits the same with and without fix 3: its waits are the loop entry's first warm-up threshold (waited-after-unit counts any code with a unit at another pc), not waits after deaths. Fix 3 was close to null on these two programs (deepcopy -0.7%, xdsl +0.8% instructions; xdsl builds about 5% more IR per run with it).


## State

- Landing tree (branch luc-mopl, on origin/master 5e66292b): 63da0bea event log (LUCIFER_JIT_DEBUG=units), 4f4cb8f5 IR dump labels, 3594cda1 IMPORT_NAME/IMPORT_FROM as helper calls (test tests/kernel/jit_import_hot.py, jitcheck lane imports), then this ticket commit. Fix 3 dropped (kept on branch luc-mopl-with-fix3 for reference, not for landing).
- Diagnosis (compile causes, C-side table) in Notes; scratch scripts in the worktree's untracked .scratch/ (window.py, cside.py, agg.py, nums.py, job9.sh).
- Next after landing: the approved order continues with fix 1 (seed empty context profiles from the site's other contexts) and fix 2 (budget growth), each its own stage.
