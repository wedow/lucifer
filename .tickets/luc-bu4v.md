---
id: luc-bu4v
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# class keywords: __init_subclass__ error names object and the keyword instead of the class

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_diff, about 120 cases over seeds 1 and 2 at 1,000 cases each.

```python
def t(src):
    try: exec(src, {})
    except Exception as e: print(type(e).__name__, e)
t("class A(flag=1): pass")
t("class A(str, flag=1): pass")
t("class A:\n  def __init_subclass__(cls): pass\nclass B(A, flag=1): pass")
t("class A(Exception, x=1): pass")
```
CPython 3.14t:
```
TypeError A.__init_subclass__() takes no keyword arguments
TypeError A.__init_subclass__() takes no keyword arguments
TypeError A.__init_subclass__() got an unexpected keyword argument 'flag'
TypeError A.__init_subclass__() takes no keyword arguments
```
Lucifer:
```
TypeError object.__init_subclass__() got an unexpected keyword argument 'flag'
TypeError object.__init_subclass__() got an unexpected keyword argument 'flag'
TypeError A.__init_subclass__() got an unexpected keyword argument 'flag'
TypeError object.__init_subclass__() got an unexpected keyword argument 'x'
```


