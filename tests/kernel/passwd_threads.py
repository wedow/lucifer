# The user and group databases read from several threads at once: libc keeps
# one enumeration per process, so each getgrall must see the whole database,
# and the lookups by name and id must agree with it. pwd.getpwall is left
# out: the oracle's takes no lock, so it races there.
import grp
import pwd
import threading

users = pwd.getpwall()
groups = grp.getgrall()
wrong = []


def reader():
    for _ in range(40):
        if grp.getgrall() != groups:
            wrong.append("all")
        for p in users[:3]:
            if pwd.getpwnam(p.pw_name).pw_uid != p.pw_uid or pwd.getpwuid(p.pw_uid).pw_uid != p.pw_uid:
                wrong.append("pwd")
        for g in groups[:3]:
            if grp.getgrnam(g.gr_name).gr_gid != g.gr_gid or grp.getgrgid(g.gr_gid).gr_gid != g.gr_gid:
                wrong.append("grp")


threads = [threading.Thread(target=reader) for _ in range(8)]
for t in threads:
    t.start()
for t in threads:
    t.join()
print(len(users) > 0, len(groups) > 0, wrong)
