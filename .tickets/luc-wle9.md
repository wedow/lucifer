---
id: luc-wle9
status: open
deps: []
links: []
created: 2026-11-09T05:53:29Z
type: task
priority: 3
tags: [perf, capi, numpy]
---
# perf: number conversions from C run kernel float.__new__/int.__new__

PyNumber_Float is 2-3% of Lucifer's time in the cadence programs, nearly all from numpy's DOUBLE_setitem (storing a Python number into a float64 array element), and py::int.__new__/py::float.__new__ add 1-3%: the C-API number protocol (PyNumber_Float, PyNumber_Long, PyNumber_Index) calls the kernel's Python constructors. CPython converts an exact int or float in C and only calls nb_float/nb_int otherwise.

## Design

Exact int and float (and their subclasses with C slots) convert in C in PyNumber_Float/PyNumber_Long/PyNumber_Index; the kernel is asked only for a Python-defined __float__/__int__/__index__.

