/* Stack effects of the 3.14 opcodes (Include/internal/pycore_opcode_metadata.h
   of CPython 3.14.10: _PyOpcode_num_popped and _PyOpcode_num_pushed, generated
   there from Python/bytecodes.c) and the stack-effect functions the compiler
   API exports (Python/flowgraph.c). The f_lineno setter models the stack with
   them (runtime/interp/frame_lineno.c). */

#include "lucifer.h"
#include "opcode.h"
#include "opcode_ids.h"
#include "interp/code.h"
#include "interp/instrument.h"

static int
_PyOpcode_num_popped(int opcode, int oparg)
{
    switch(opcode) {
        case ANNOTATIONS_PLACEHOLDER:
            return 0;
        case BINARY_OP:
            return 2;
        case BINARY_OP_ADD_FLOAT:
            return 2;
        case BINARY_OP_ADD_INT:
            return 2;
        case BINARY_OP_ADD_UNICODE:
            return 2;
        case BINARY_OP_EXTEND:
            return 2;
        case BINARY_OP_INPLACE_ADD_UNICODE:
            return 2;
        case BINARY_OP_MULTIPLY_FLOAT:
            return 2;
        case BINARY_OP_MULTIPLY_INT:
            return 2;
        case BINARY_OP_SUBSCR_DICT:
            return 2;
        case BINARY_OP_SUBSCR_GETITEM:
            return 2;
        case BINARY_OP_SUBSCR_LIST_INT:
            return 2;
        case BINARY_OP_SUBSCR_LIST_SLICE:
            return 2;
        case BINARY_OP_SUBSCR_STR_INT:
            return 2;
        case BINARY_OP_SUBSCR_TUPLE_INT:
            return 2;
        case BINARY_OP_SUBTRACT_FLOAT:
            return 2;
        case BINARY_OP_SUBTRACT_INT:
            return 2;
        case BINARY_SLICE:
            return 3;
        case BUILD_INTERPOLATION:
            return 2 + (oparg & 1);
        case BUILD_LIST:
            return oparg;
        case BUILD_MAP:
            return oparg*2;
        case BUILD_SET:
            return oparg;
        case BUILD_SLICE:
            return oparg;
        case BUILD_STRING:
            return oparg;
        case BUILD_TEMPLATE:
            return 2;
        case BUILD_TUPLE:
            return oparg;
        case CACHE:
            return 0;
        case CALL:
            return 2 + oparg;
        case CALL_ALLOC_AND_ENTER_INIT:
            return 2 + oparg;
        case CALL_BOUND_METHOD_EXACT_ARGS:
            return 2 + oparg;
        case CALL_BOUND_METHOD_GENERAL:
            return 2 + oparg;
        case CALL_BUILTIN_CLASS:
            return 2 + oparg;
        case CALL_BUILTIN_FAST:
            return 2 + oparg;
        case CALL_BUILTIN_FAST_WITH_KEYWORDS:
            return 2 + oparg;
        case CALL_BUILTIN_O:
            return 2 + oparg;
        case CALL_FUNCTION_EX:
            return 4;
        case CALL_INTRINSIC_1:
            return 1;
        case CALL_INTRINSIC_2:
            return 2;
        case CALL_ISINSTANCE:
            return 2 + oparg;
        case CALL_KW:
            return 3 + oparg;
        case CALL_KW_BOUND_METHOD:
            return 3 + oparg;
        case CALL_KW_NON_PY:
            return 3 + oparg;
        case CALL_KW_PY:
            return 3 + oparg;
        case CALL_LEN:
            return 3;
        case CALL_LIST_APPEND:
            return 3;
        case CALL_METHOD_DESCRIPTOR_FAST:
            return 2 + oparg;
        case CALL_METHOD_DESCRIPTOR_FAST_WITH_KEYWORDS:
            return 2 + oparg;
        case CALL_METHOD_DESCRIPTOR_NOARGS:
            return 2 + oparg;
        case CALL_METHOD_DESCRIPTOR_O:
            return 2 + oparg;
        case CALL_NON_PY_GENERAL:
            return 2 + oparg;
        case CALL_PY_EXACT_ARGS:
            return 2 + oparg;
        case CALL_PY_GENERAL:
            return 2 + oparg;
        case CALL_STR_1:
            return 3;
        case CALL_TUPLE_1:
            return 3;
        case CALL_TYPE_1:
            return 3;
        case CHECK_EG_MATCH:
            return 2;
        case CHECK_EXC_MATCH:
            return 2;
        case CLEANUP_THROW:
            return 3;
        case COMPARE_OP:
            return 2;
        case COMPARE_OP_FLOAT:
            return 2;
        case COMPARE_OP_INT:
            return 2;
        case COMPARE_OP_STR:
            return 2;
        case CONTAINS_OP:
            return 2;
        case CONTAINS_OP_DICT:
            return 2;
        case CONTAINS_OP_SET:
            return 2;
        case CONVERT_VALUE:
            return 1;
        case COPY:
            return 1 + (oparg-1);
        case COPY_FREE_VARS:
            return 0;
        case DELETE_ATTR:
            return 1;
        case DELETE_DEREF:
            return 0;
        case DELETE_FAST:
            return 0;
        case DELETE_GLOBAL:
            return 0;
        case DELETE_NAME:
            return 0;
        case DELETE_SUBSCR:
            return 2;
        case DICT_MERGE:
            return 5 + (oparg - 1);
        case DICT_UPDATE:
            return 2 + (oparg - 1);
        case END_ASYNC_FOR:
            return 2;
        case END_FOR:
            return 1;
        case END_SEND:
            return 2;
        case ENTER_EXECUTOR:
            return 0;
        case EXIT_INIT_CHECK:
            return 1;
        case EXTENDED_ARG:
            return 0;
        case FORMAT_SIMPLE:
            return 1;
        case FORMAT_WITH_SPEC:
            return 2;
        case FOR_ITER:
            return 1;
        case FOR_ITER_GEN:
            return 1;
        case FOR_ITER_LIST:
            return 1;
        case FOR_ITER_RANGE:
            return 1;
        case FOR_ITER_TUPLE:
            return 1;
        case GET_AITER:
            return 1;
        case GET_ANEXT:
            return 1;
        case GET_AWAITABLE:
            return 1;
        case GET_ITER:
            return 1;
        case GET_LEN:
            return 1;
        case GET_YIELD_FROM_ITER:
            return 1;
        case IMPORT_FROM:
            return 1;
        case IMPORT_NAME:
            return 2;
        case INSTRUMENTED_CALL:
            return 2 + oparg;
        case INSTRUMENTED_CALL_FUNCTION_EX:
            return 4;
        case INSTRUMENTED_CALL_KW:
            return 3 + oparg;
        case INSTRUMENTED_END_ASYNC_FOR:
            return 2;
        case INSTRUMENTED_END_FOR:
            return 2;
        case INSTRUMENTED_END_SEND:
            return 2;
        case INSTRUMENTED_FOR_ITER:
            return 1;
        case INSTRUMENTED_INSTRUCTION:
            return 0;
        case INSTRUMENTED_JUMP_BACKWARD:
            return 0;
        case INSTRUMENTED_JUMP_FORWARD:
            return 0;
        case INSTRUMENTED_LINE:
            return 0;
        case INSTRUMENTED_LOAD_SUPER_ATTR:
            return 3;
        case INSTRUMENTED_NOT_TAKEN:
            return 0;
        case INSTRUMENTED_POP_ITER:
            return 1;
        case INSTRUMENTED_POP_JUMP_IF_FALSE:
            return 1;
        case INSTRUMENTED_POP_JUMP_IF_NONE:
            return 1;
        case INSTRUMENTED_POP_JUMP_IF_NOT_NONE:
            return 1;
        case INSTRUMENTED_POP_JUMP_IF_TRUE:
            return 1;
        case INSTRUMENTED_RESUME:
            return 0;
        case INSTRUMENTED_RETURN_VALUE:
            return 1;
        case INSTRUMENTED_YIELD_VALUE:
            return 1;
        case INTERPRETER_EXIT:
            return 1;
        case IS_OP:
            return 2;
        case JUMP:
            return 0;
        case JUMP_BACKWARD:
            return 0;
        case JUMP_BACKWARD_JIT:
            return 0;
        case JUMP_BACKWARD_NO_INTERRUPT:
            return 0;
        case JUMP_BACKWARD_NO_JIT:
            return 0;
        case JUMP_FORWARD:
            return 0;
        case JUMP_IF_FALSE:
            return 1;
        case JUMP_IF_TRUE:
            return 1;
        case JUMP_NO_INTERRUPT:
            return 0;
        case LIST_APPEND:
            return 2 + (oparg-1);
        case LIST_EXTEND:
            return 2 + (oparg-1);
        case LOAD_ATTR:
            return 1;
        case LOAD_ATTR_CLASS:
            return 1;
        case LOAD_ATTR_CLASS_WITH_METACLASS_CHECK:
            return 1;
        case LOAD_ATTR_GETATTRIBUTE_OVERRIDDEN:
            return 1;
        case LOAD_ATTR_INSTANCE_VALUE:
            return 1;
        case LOAD_ATTR_METHOD_LAZY_DICT:
            return 1;
        case LOAD_ATTR_METHOD_NO_DICT:
            return 1;
        case LOAD_ATTR_METHOD_WITH_VALUES:
            return 1;
        case LOAD_ATTR_MODULE:
            return 1;
        case LOAD_ATTR_NONDESCRIPTOR_NO_DICT:
            return 1;
        case LOAD_ATTR_NONDESCRIPTOR_WITH_VALUES:
            return 1;
        case LOAD_ATTR_PROPERTY:
            return 1;
        case LOAD_ATTR_SLOT:
            return 1;
        case LOAD_ATTR_WITH_HINT:
            return 1;
        case LOAD_BUILD_CLASS:
            return 0;
        case LOAD_CLOSURE:
            return 0;
        case LOAD_COMMON_CONSTANT:
            return 0;
        case LOAD_CONST:
            return 0;
        case LOAD_CONST_IMMORTAL:
            return 0;
        case LOAD_CONST_MORTAL:
            return 0;
        case LOAD_DEREF:
            return 0;
        case LOAD_FAST:
            return 0;
        case LOAD_FAST_AND_CLEAR:
            return 0;
        case LOAD_FAST_BORROW:
            return 0;
        case LOAD_FAST_BORROW_LOAD_FAST_BORROW:
            return 0;
        case LOAD_FAST_CHECK:
            return 0;
        case LOAD_FAST_LOAD_FAST:
            return 0;
        case LOAD_FROM_DICT_OR_DEREF:
            return 1;
        case LOAD_FROM_DICT_OR_GLOBALS:
            return 1;
        case LOAD_GLOBAL:
            return 0;
        case LOAD_GLOBAL_BUILTIN:
            return 0;
        case LOAD_GLOBAL_MODULE:
            return 0;
        case LOAD_LOCALS:
            return 0;
        case LOAD_NAME:
            return 0;
        case LOAD_SMALL_INT:
            return 0;
        case LOAD_SPECIAL:
            return 1;
        case LOAD_SUPER_ATTR:
            return 3;
        case LOAD_SUPER_ATTR_ATTR:
            return 3;
        case LOAD_SUPER_ATTR_METHOD:
            return 3;
        case MAKE_CELL:
            return 0;
        case MAKE_FUNCTION:
            return 1;
        case MAP_ADD:
            return 3 + (oparg - 1);
        case MATCH_CLASS:
            return 3;
        case MATCH_KEYS:
            return 2;
        case MATCH_MAPPING:
            return 1;
        case MATCH_SEQUENCE:
            return 1;
        case NOP:
            return 0;
        case NOT_TAKEN:
            return 0;
        case POP_BLOCK:
            return 0;
        case POP_EXCEPT:
            return 1;
        case POP_ITER:
            return 1;
        case POP_JUMP_IF_FALSE:
            return 1;
        case POP_JUMP_IF_NONE:
            return 1;
        case POP_JUMP_IF_NOT_NONE:
            return 1;
        case POP_JUMP_IF_TRUE:
            return 1;
        case POP_TOP:
            return 1;
        case PUSH_EXC_INFO:
            return 1;
        case PUSH_NULL:
            return 0;
        case RAISE_VARARGS:
            return oparg;
        case RERAISE:
            return 1 + oparg;
        case RESERVED:
            return 0;
        case RESUME:
            return 0;
        case RESUME_CHECK:
            return 0;
        case RETURN_GENERATOR:
            return 0;
        case RETURN_VALUE:
            return 1;
        case SEND:
            return 2;
        case SEND_GEN:
            return 2;
        case SETUP_ANNOTATIONS:
            return 0;
        case SETUP_CLEANUP:
            return 0;
        case SETUP_FINALLY:
            return 0;
        case SETUP_WITH:
            return 0;
        case SET_ADD:
            return 2 + (oparg-1);
        case SET_FUNCTION_ATTRIBUTE:
            return 2;
        case SET_UPDATE:
            return 2 + (oparg-1);
        case STORE_ATTR:
            return 2;
        case STORE_ATTR_INSTANCE_VALUE:
            return 2;
        case STORE_ATTR_SLOT:
            return 2;
        case STORE_ATTR_WITH_HINT:
            return 2;
        case STORE_DEREF:
            return 1;
        case STORE_FAST:
            return 1;
        case STORE_FAST_LOAD_FAST:
            return 1;
        case STORE_FAST_MAYBE_NULL:
            return 1;
        case STORE_FAST_STORE_FAST:
            return 2;
        case STORE_GLOBAL:
            return 1;
        case STORE_NAME:
            return 1;
        case STORE_SLICE:
            return 4;
        case STORE_SUBSCR:
            return 3;
        case STORE_SUBSCR_DICT:
            return 3;
        case STORE_SUBSCR_LIST_INT:
            return 3;
        case SWAP:
            return 2 + (oparg-2);
        case TO_BOOL:
            return 1;
        case TO_BOOL_ALWAYS_TRUE:
            return 1;
        case TO_BOOL_BOOL:
            return 1;
        case TO_BOOL_INT:
            return 1;
        case TO_BOOL_LIST:
            return 1;
        case TO_BOOL_NONE:
            return 1;
        case TO_BOOL_STR:
            return 1;
        case UNARY_INVERT:
            return 1;
        case UNARY_NEGATIVE:
            return 1;
        case UNARY_NOT:
            return 1;
        case UNPACK_EX:
            return 1;
        case UNPACK_SEQUENCE:
            return 1;
        case UNPACK_SEQUENCE_LIST:
            return 1;
        case UNPACK_SEQUENCE_TUPLE:
            return 1;
        case UNPACK_SEQUENCE_TWO_TUPLE:
            return 1;
        case WITH_EXCEPT_START:
            return 5;
        case YIELD_VALUE:
            return 1;
        default:
            return -1;
    }
}


static int
_PyOpcode_num_pushed(int opcode, int oparg)
{
    switch(opcode) {
        case ANNOTATIONS_PLACEHOLDER:
            return 0;
        case BINARY_OP:
            return 1;
        case BINARY_OP_ADD_FLOAT:
            return 1;
        case BINARY_OP_ADD_INT:
            return 1;
        case BINARY_OP_ADD_UNICODE:
            return 1;
        case BINARY_OP_EXTEND:
            return 1;
        case BINARY_OP_INPLACE_ADD_UNICODE:
            return 0;
        case BINARY_OP_MULTIPLY_FLOAT:
            return 1;
        case BINARY_OP_MULTIPLY_INT:
            return 1;
        case BINARY_OP_SUBSCR_DICT:
            return 1;
        case BINARY_OP_SUBSCR_GETITEM:
            return 0;
        case BINARY_OP_SUBSCR_LIST_INT:
            return 1;
        case BINARY_OP_SUBSCR_LIST_SLICE:
            return 1;
        case BINARY_OP_SUBSCR_STR_INT:
            return 1;
        case BINARY_OP_SUBSCR_TUPLE_INT:
            return 1;
        case BINARY_OP_SUBTRACT_FLOAT:
            return 1;
        case BINARY_OP_SUBTRACT_INT:
            return 1;
        case BINARY_SLICE:
            return 1;
        case BUILD_INTERPOLATION:
            return 1;
        case BUILD_LIST:
            return 1;
        case BUILD_MAP:
            return 1;
        case BUILD_SET:
            return 1;
        case BUILD_SLICE:
            return 1;
        case BUILD_STRING:
            return 1;
        case BUILD_TEMPLATE:
            return 1;
        case BUILD_TUPLE:
            return 1;
        case CACHE:
            return 0;
        case CALL:
            return 1;
        case CALL_ALLOC_AND_ENTER_INIT:
            return 0;
        case CALL_BOUND_METHOD_EXACT_ARGS:
            return 0;
        case CALL_BOUND_METHOD_GENERAL:
            return 0;
        case CALL_BUILTIN_CLASS:
            return 1;
        case CALL_BUILTIN_FAST:
            return 1;
        case CALL_BUILTIN_FAST_WITH_KEYWORDS:
            return 1;
        case CALL_BUILTIN_O:
            return 1;
        case CALL_FUNCTION_EX:
            return 1;
        case CALL_INTRINSIC_1:
            return 1;
        case CALL_INTRINSIC_2:
            return 1;
        case CALL_ISINSTANCE:
            return 1;
        case CALL_KW:
            return 1;
        case CALL_KW_BOUND_METHOD:
            return 0;
        case CALL_KW_NON_PY:
            return 1;
        case CALL_KW_PY:
            return 0;
        case CALL_LEN:
            return 1;
        case CALL_LIST_APPEND:
            return 0;
        case CALL_METHOD_DESCRIPTOR_FAST:
            return 1;
        case CALL_METHOD_DESCRIPTOR_FAST_WITH_KEYWORDS:
            return 1;
        case CALL_METHOD_DESCRIPTOR_NOARGS:
            return 1;
        case CALL_METHOD_DESCRIPTOR_O:
            return 1;
        case CALL_NON_PY_GENERAL:
            return 1;
        case CALL_PY_EXACT_ARGS:
            return 0;
        case CALL_PY_GENERAL:
            return 0;
        case CALL_STR_1:
            return 1;
        case CALL_TUPLE_1:
            return 1;
        case CALL_TYPE_1:
            return 1;
        case CHECK_EG_MATCH:
            return 2;
        case CHECK_EXC_MATCH:
            return 2;
        case CLEANUP_THROW:
            return 2;
        case COMPARE_OP:
            return 1;
        case COMPARE_OP_FLOAT:
            return 1;
        case COMPARE_OP_INT:
            return 1;
        case COMPARE_OP_STR:
            return 1;
        case CONTAINS_OP:
            return 1;
        case CONTAINS_OP_DICT:
            return 1;
        case CONTAINS_OP_SET:
            return 1;
        case CONVERT_VALUE:
            return 1;
        case COPY:
            return 2 + (oparg-1);
        case COPY_FREE_VARS:
            return 0;
        case DELETE_ATTR:
            return 0;
        case DELETE_DEREF:
            return 0;
        case DELETE_FAST:
            return 0;
        case DELETE_GLOBAL:
            return 0;
        case DELETE_NAME:
            return 0;
        case DELETE_SUBSCR:
            return 0;
        case DICT_MERGE:
            return 4 + (oparg - 1);
        case DICT_UPDATE:
            return 1 + (oparg - 1);
        case END_ASYNC_FOR:
            return 0;
        case END_FOR:
            return 0;
        case END_SEND:
            return 1;
        case ENTER_EXECUTOR:
            return 0;
        case EXIT_INIT_CHECK:
            return 0;
        case EXTENDED_ARG:
            return 0;
        case FORMAT_SIMPLE:
            return 1;
        case FORMAT_WITH_SPEC:
            return 1;
        case FOR_ITER:
            return 2;
        case FOR_ITER_GEN:
            return 1;
        case FOR_ITER_LIST:
            return 2;
        case FOR_ITER_RANGE:
            return 2;
        case FOR_ITER_TUPLE:
            return 2;
        case GET_AITER:
            return 1;
        case GET_ANEXT:
            return 2;
        case GET_AWAITABLE:
            return 1;
        case GET_ITER:
            return 1;
        case GET_LEN:
            return 2;
        case GET_YIELD_FROM_ITER:
            return 1;
        case IMPORT_FROM:
            return 2;
        case IMPORT_NAME:
            return 1;
        case INSTRUMENTED_CALL:
            return 1;
        case INSTRUMENTED_CALL_FUNCTION_EX:
            return 1;
        case INSTRUMENTED_CALL_KW:
            return 1;
        case INSTRUMENTED_END_ASYNC_FOR:
            return 0;
        case INSTRUMENTED_END_FOR:
            return 1;
        case INSTRUMENTED_END_SEND:
            return 1;
        case INSTRUMENTED_FOR_ITER:
            return 2;
        case INSTRUMENTED_INSTRUCTION:
            return 0;
        case INSTRUMENTED_JUMP_BACKWARD:
            return 0;
        case INSTRUMENTED_JUMP_FORWARD:
            return 0;
        case INSTRUMENTED_LINE:
            return 0;
        case INSTRUMENTED_LOAD_SUPER_ATTR:
            return 1 + (oparg & 1);
        case INSTRUMENTED_NOT_TAKEN:
            return 0;
        case INSTRUMENTED_POP_ITER:
            return 0;
        case INSTRUMENTED_POP_JUMP_IF_FALSE:
            return 0;
        case INSTRUMENTED_POP_JUMP_IF_NONE:
            return 0;
        case INSTRUMENTED_POP_JUMP_IF_NOT_NONE:
            return 0;
        case INSTRUMENTED_POP_JUMP_IF_TRUE:
            return 0;
        case INSTRUMENTED_RESUME:
            return 0;
        case INSTRUMENTED_RETURN_VALUE:
            return 1;
        case INSTRUMENTED_YIELD_VALUE:
            return 1;
        case INTERPRETER_EXIT:
            return 0;
        case IS_OP:
            return 1;
        case JUMP:
            return 0;
        case JUMP_BACKWARD:
            return 0;
        case JUMP_BACKWARD_JIT:
            return 0;
        case JUMP_BACKWARD_NO_INTERRUPT:
            return 0;
        case JUMP_BACKWARD_NO_JIT:
            return 0;
        case JUMP_FORWARD:
            return 0;
        case JUMP_IF_FALSE:
            return 1;
        case JUMP_IF_TRUE:
            return 1;
        case JUMP_NO_INTERRUPT:
            return 0;
        case LIST_APPEND:
            return 1 + (oparg-1);
        case LIST_EXTEND:
            return 1 + (oparg-1);
        case LOAD_ATTR:
            return 1 + (oparg&1);
        case LOAD_ATTR_CLASS:
            return 1 + (oparg & 1);
        case LOAD_ATTR_CLASS_WITH_METACLASS_CHECK:
            return 1 + (oparg & 1);
        case LOAD_ATTR_GETATTRIBUTE_OVERRIDDEN:
            return 1;
        case LOAD_ATTR_INSTANCE_VALUE:
            return 1 + (oparg & 1);
        case LOAD_ATTR_METHOD_LAZY_DICT:
            return 2;
        case LOAD_ATTR_METHOD_NO_DICT:
            return 2;
        case LOAD_ATTR_METHOD_WITH_VALUES:
            return 2;
        case LOAD_ATTR_MODULE:
            return 1 + (oparg & 1);
        case LOAD_ATTR_NONDESCRIPTOR_NO_DICT:
            return 1;
        case LOAD_ATTR_NONDESCRIPTOR_WITH_VALUES:
            return 1;
        case LOAD_ATTR_PROPERTY:
            return 0;
        case LOAD_ATTR_SLOT:
            return 1 + (oparg & 1);
        case LOAD_ATTR_WITH_HINT:
            return 1 + (oparg & 1);
        case LOAD_BUILD_CLASS:
            return 1;
        case LOAD_CLOSURE:
            return 1;
        case LOAD_COMMON_CONSTANT:
            return 1;
        case LOAD_CONST:
            return 1;
        case LOAD_CONST_IMMORTAL:
            return 1;
        case LOAD_CONST_MORTAL:
            return 1;
        case LOAD_DEREF:
            return 1;
        case LOAD_FAST:
            return 1;
        case LOAD_FAST_AND_CLEAR:
            return 1;
        case LOAD_FAST_BORROW:
            return 1;
        case LOAD_FAST_BORROW_LOAD_FAST_BORROW:
            return 2;
        case LOAD_FAST_CHECK:
            return 1;
        case LOAD_FAST_LOAD_FAST:
            return 2;
        case LOAD_FROM_DICT_OR_DEREF:
            return 1;
        case LOAD_FROM_DICT_OR_GLOBALS:
            return 1;
        case LOAD_GLOBAL:
            return 1 + (oparg & 1);
        case LOAD_GLOBAL_BUILTIN:
            return 1 + (oparg & 1);
        case LOAD_GLOBAL_MODULE:
            return 1 + (oparg & 1);
        case LOAD_LOCALS:
            return 1;
        case LOAD_NAME:
            return 1;
        case LOAD_SMALL_INT:
            return 1;
        case LOAD_SPECIAL:
            return 2;
        case LOAD_SUPER_ATTR:
            return 1 + (oparg & 1);
        case LOAD_SUPER_ATTR_ATTR:
            return 1;
        case LOAD_SUPER_ATTR_METHOD:
            return 2;
        case MAKE_CELL:
            return 0;
        case MAKE_FUNCTION:
            return 1;
        case MAP_ADD:
            return 1 + (oparg - 1);
        case MATCH_CLASS:
            return 1;
        case MATCH_KEYS:
            return 3;
        case MATCH_MAPPING:
            return 2;
        case MATCH_SEQUENCE:
            return 2;
        case NOP:
            return 0;
        case NOT_TAKEN:
            return 0;
        case POP_BLOCK:
            return 0;
        case POP_EXCEPT:
            return 0;
        case POP_ITER:
            return 0;
        case POP_JUMP_IF_FALSE:
            return 0;
        case POP_JUMP_IF_NONE:
            return 0;
        case POP_JUMP_IF_NOT_NONE:
            return 0;
        case POP_JUMP_IF_TRUE:
            return 0;
        case POP_TOP:
            return 0;
        case PUSH_EXC_INFO:
            return 2;
        case PUSH_NULL:
            return 1;
        case RAISE_VARARGS:
            return 0;
        case RERAISE:
            return oparg;
        case RESERVED:
            return 0;
        case RESUME:
            return 0;
        case RESUME_CHECK:
            return 0;
        case RETURN_GENERATOR:
            return 1;
        case RETURN_VALUE:
            return 1;
        case SEND:
            return 2;
        case SEND_GEN:
            return 1;
        case SETUP_ANNOTATIONS:
            return 0;
        case SETUP_CLEANUP:
            return 2;
        case SETUP_FINALLY:
            return 1;
        case SETUP_WITH:
            return 1;
        case SET_ADD:
            return 1 + (oparg-1);
        case SET_FUNCTION_ATTRIBUTE:
            return 1;
        case SET_UPDATE:
            return 1 + (oparg-1);
        case STORE_ATTR:
            return 0;
        case STORE_ATTR_INSTANCE_VALUE:
            return 0;
        case STORE_ATTR_SLOT:
            return 0;
        case STORE_ATTR_WITH_HINT:
            return 0;
        case STORE_DEREF:
            return 0;
        case STORE_FAST:
            return 0;
        case STORE_FAST_LOAD_FAST:
            return 1;
        case STORE_FAST_MAYBE_NULL:
            return 0;
        case STORE_FAST_STORE_FAST:
            return 0;
        case STORE_GLOBAL:
            return 0;
        case STORE_NAME:
            return 0;
        case STORE_SLICE:
            return 0;
        case STORE_SUBSCR:
            return 0;
        case STORE_SUBSCR_DICT:
            return 0;
        case STORE_SUBSCR_LIST_INT:
            return 0;
        case SWAP:
            return 2 + (oparg-2);
        case TO_BOOL:
            return 1;
        case TO_BOOL_ALWAYS_TRUE:
            return 1;
        case TO_BOOL_BOOL:
            return 1;
        case TO_BOOL_INT:
            return 1;
        case TO_BOOL_LIST:
            return 1;
        case TO_BOOL_NONE:
            return 1;
        case TO_BOOL_STR:
            return 1;
        case UNARY_INVERT:
            return 1;
        case UNARY_NEGATIVE:
            return 1;
        case UNARY_NOT:
            return 1;
        case UNPACK_EX:
            return 1 + (oparg & 0xFF) + (oparg >> 8);
        case UNPACK_SEQUENCE:
            return oparg;
        case UNPACK_SEQUENCE_LIST:
            return oparg;
        case UNPACK_SEQUENCE_TUPLE:
            return oparg;
        case UNPACK_SEQUENCE_TWO_TUPLE:
            return 2;
        case WITH_EXCEPT_START:
            return 6;
        case YIELD_VALUE:
            return 1;
        default:
            return -1;
    }
}


static int
get_stack_effect(int opcode, int oparg, int jump, int *net)
{
    if (opcode < 0) {
        return -1;
    }
    if (opcode < MIN_INSTRUMENTED_OPCODE && lu_opcode_deopt(opcode) != opcode) {
        return -1;              /* specialized instructions are not supported */
    }
    int popped = _PyOpcode_num_popped(opcode, oparg);
    int pushed = _PyOpcode_num_pushed(opcode, oparg);
    if (popped < 0 || pushed < 0) {
        return -1;
    }
    /* SETUP_FINALLY, SETUP_CLEANUP and SETUP_WITH only push when they jump. */
    if ((opcode == SETUP_FINALLY || opcode == SETUP_CLEANUP || opcode == SETUP_WITH) && !jump) {
        *net = 0;
        return 0;
    }
    *net = pushed - popped;
    return 0;
}

int
PyCompile_OpcodeStackEffectWithJump(int opcode, int oparg, int jump)
{
    int net;
    return get_stack_effect(opcode, oparg, jump, &net) < 0 ? PY_INVALID_STACK_EFFECT : net;
}

int
PyCompile_OpcodeStackEffect(int opcode, int oparg)
{
    int net;
    return get_stack_effect(opcode, oparg, -1, &net) < 0 ? PY_INVALID_STACK_EFFECT : net;
}
