/* Embeds the runtime: each argument is a program, run by PyRun_SimpleString
   in its own Py_Initialize / Py_FinalizeEx cycle. The output must be what
   `bin/lucifer -c PROGRAM` prints for each, one after another. */
#include <Python.h>

#include <stdio.h>

int
main(int argc, char **argv)
{
    int status = 0;
    for (int i = 1; i < argc; i++) {
        if (Py_IsInitialized()) {
            fprintf(stderr, "initialized before Py_Initialize\n");
            return 2;
        }
        Py_Initialize();
        if (!Py_IsInitialized()) {
            fprintf(stderr, "not initialized after Py_Initialize\n");
            return 2;
        }
        if (PyRun_SimpleString(argv[i]) != 0) {
            status = 1;
        }
        if (Py_FinalizeEx() < 0) {
            return 3;
        }
    }
    return status;
}
