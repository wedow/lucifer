# os.statvfs and os.fstatvfs (multiprocessing.heap picks /dev/shm by them).
import os

for st in (os.statvfs('.'), os.statvfs(b'.'), os.fstatvfs(0)):
    print(type(st).__name__, len(st), st.f_bsize > 0, st.f_namemax == st[9], isinstance(st.f_fsid, int))
print(os.statvfs_result.n_sequence_fields, os.statvfs_result.n_fields)
try:
    os.statvfs('/nonexistent/path')
except FileNotFoundError as e:
    print(e.errno, e.filename)
