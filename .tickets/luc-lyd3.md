---
id: luc-lyd3
status: open
deps: []
links: []
created: 2026-11-10T05:11:01Z
type: feature
priority: 3
tags: [stdlib, libtest]
---
# fcntl.ioctl raises NotImplementedError

lib/modules/fcntl.py's ioctl is a stub. With os.openpty (luc-clibs) test_ioctl's IoctlTestsPty run and error on it (4 cases; CPython 3.14t passes them), and termios-adjacent code (tty window sizes, TIOCSTI) cannot use it. Port fcntl_ioctl_impl of Modules/fcntlmodule.c: int, bytes and mutable-buffer arguments, the 1024-byte copy buffer.

