---
id: luc-2b4w
status: closed
deps: []
links: []
created: 2026-11-18T13:33:41Z
type: task
priority: 1
---
# kernelcheck runs its checks one at a time

The daily's check-kernel-jit (jit then jit-deopt, 763 checks each) held one test hold for about 40 minutes on 2026-11-18 because tools/kernelcheck runs each program serially, stalling the queue behind it. Run checks concurrently (--jobs, default the CPUs the process may use), each in its own directory, output in order; tools/floor keeps --jobs 1 since it shards itself. Report any threads_* check that flakes under concurrency.


## Result

Landed. Wall time per lane on a loaded host, 775 checks each: plain 106 s, jit 156 s (warm oracle cache), jit-deopt
374 s (oracle cache cold for the lane), against about 20 minutes a lane serially (daily 2026-11-18). Run beside
other checks, threads_container_protocol, threads_local_race, threads_set_race and threads_frame_locals_race
(-X lucifer_jit=off) hit the 120 s timeout in the jit lane, so the 31 threads_ programs run one at a time after the
rest. check-kernel and check-kernel-jit now depend on oracle-exts: null_item_repr imports _testlimitedcapi, which a
fresh tree lacked.
