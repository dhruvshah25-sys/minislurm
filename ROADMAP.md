# minislurm roadmap

Every milestone follows the same loop:

1. **Learn:** the CS:APP chapter and the man pages.
2. **Build:** the minislurm piece.
3. **Compare:** read the matching code in Slurm (`SchedMD/slurm`, tag
   `slurm-22-05-11-1`, the exact version on the cluster).
4. **Observe:** look at the same thing live on hpc.spit.ac.in, read-only.

Write what you find in `NOTES.md`. If you can't explain it, you haven't learned it yet.

| # | Build | Learn | CS:APP | On the real cluster |
|---|---|---|---|---|
| M0 | run `recon/observe.sh`, answer NOTES M0 | what's actually on the box | – | everything |
| M1 ✅ | priority engine | trees, floating point | 2.4 | `priority/multifactor`, `fair_tree.c`, `sprio`, `sshare` |
| **M2** ⏭ | `msrun`: one job, time limit, KillWait, output, MaxRSS | processes, signals, races, dup2 | 8, 10 | `slurmstepd`, `KillWait=30` |
| M3 | `mslurmctld` + CLI over sockets, auth, crash-safe state | sockets, epoll, protocols, fsync | 11, 12 | slurmctld :6817, munge, `StateSaveLocation` |
| M4 | allocator + scheduler loop + backfill + reservations | bitmaps, bin packing, planning | – | `cons_tres`, `sched/backfill`, squeue reasons |
| M5 | cgroup v2 per job (fixes the XFAIL test) | VM, RSS vs page cache, OOM | 9 | `proctrack/cgroup`, `task/cgroup` |
| M6 | accounting in SQLite, prolog/epilog | persistence, daemons and fork | 8 | slurmdbd + MariaDB, `prolog.sh`, `gpu_notify.sh` |
| M7 | GPU isolation, all 4 layers incl. eBPF | permissions, ACLs, eBPF | 10 | `fix-gpu-lockdown.sh`, `ConstrainDevices` |
| M8 | GPU + NUMA deep dive with CUDA | PCIe, NUMA, CUDA | 6, 9 | `gres.conf`, `nvidia-smi topo -m` |
| M9 | Prometheus exporter + shadow-mode replay of real history | observability, simulation | – | Prometheus, Grafana |
| M10 | multi-node: node daemon on 2–3 VMs, heartbeats, MPI | distributed failure | 11 | what SPIT looks like with more nodes |

---

## M0: Recon (read-only, on the cluster)

Run `recon/observe.sh` on hpc.spit.ac.in as yourself, a few times on
different days. It only reads. Copy **only** the `anon/` folder off the
server. Then answer NOTES M0. Its output also feeds M1's verification and
M9's simulator.

## M1: Priority engine ✅ (built; verifying it is your job)

**Mirrors:** `priority/multifactor`, `PriorityDecayHalfLife=7-0`, Fair Tree,
`TRESBillingWeights` + `MAX_TRES`.

**Done:** the tests reproduce the live `sprio -l` (4104 / 4103 / 4082), the
billing for every job shape in the brief (112 / 117 / 58 / 7), and the
account-level LevelFS (1.3033 / 0.3093). See `tests/priority_vectors.md`.

**Your part:** NOTES M1. Most importantly, load the real `sshare -l -P` and
check every user. The per-user split in `tools/spit_scenario.hpp` is
reconstructed; only the account totals are real. Then implement the classic
`NO_FAIR_TREE` formula (`F = 2^(-U/S)`) and compare which students gain or lose.

## M2: `msrun` ⏭ NEXT, and yours to write

`src/exec/msrun.cpp` is a skeleton with 6 TODOs, each with the CS:APP
section and the questions it needs. Argument parsing and a RAII `UniqueFd`
are given.

```bash
cmake --build build --target m2-test
```

You're done at `passed 12, failed 0, expected-fail 1`, with the sanitizer
build on. The expected fail (test 13) is on purpose: a child that calls
`setsid()` escapes your process group and survives. The cluster's own
`gpu_notify.sh` uses `setsid -f` for exactly this reason. M5 fixes it.

**Stretch:** replace `alarm` + `sigsuspend` with `pidfd_open` + `signalfd` +
`timerfd` in one `poll()` loop. That's how a modern daemon does it, and it's
the shape M3's event loop needs.

## M3: Controller + CLI over sockets

**Mirrors:** `slurmctld`, `sbatch`, `squeue`, `scancel`, `sprio`, munge.

**Build:**
- An `mslurmctld` daemon holding the job queue.
- `msbatch`, `msqueue`, `mscancel` and `msprio`, talking to it over a Unix
  or TCP socket using a length-prefixed binary protocol you design (version,
  type, length, HMAC).
- An `epoll` loop and a `#SBATCH` parser.
- Job states PENDING → RUNNING → COMPLETED, FAILED, CANCELLED, TIMEOUT.
- Run jobs with M2's code and order the queue with M1.
- State saved with a write-ahead log + `fsync`.

**Done when:** many clients work at once, a client killed mid-message
doesn't hurt the daemon, and after `kill -9` and a restart no job is lost.

**Read:** CS:APP 11, 12; Beej's Guide to Network Programming; `epoll(7)`.

## M4: Allocation + scheduling + backfill

**Mirrors:** `select/cons_tres`, `CR_Core_Memory`, `OverSubscribe=FORCE:4`,
`DefMemPerCPU=4000`, `DefCpuPerGPU=8`, `sched/backfill`, maintenance
reservations, and the reasons `(Priority)`, `(Resources)`,
`(PartitionTimeLimit)` and `(ReqNodeNotAvail)`.

**Build:**
- The node as 2 sockets × 56 cores × 2 threads in core bitmaps, plus memory
  and 2 GPUs.
- A main scheduler that runs the highest-priority job that fits.
- Backfill: a lower-priority job may start only if it ends before the top
  job's reserved start.

**Done when:** an 8-day request shows `(PartitionTimeLimit)` and never
starts, and a job that would run into a reservation shows
`(ReqNodeNotAvail)`.

## M5: cgroups v2

**Mirrors:** `proctrack/cgroup`, `task/cgroup`, and `CR_Core_Memory` enforcement.

**Build:**
- A per-job cgroup with `cpuset.cpus`, `memory.max` and `pids.max`.
- Launch straight into it with `clone3(CLONE_INTO_CGROUP)`.
- Kill the job with `cgroup.kill`.
- An `OUT_OF_MEMORY` job state.

**Done when:** test 13 passes, a fork bomb stops at `pids.max`, and a
memory hog is OOM-killed inside its own cgroup only.

**Read:** CS:APP 9; the kernel's `admin-guide/cgroup-v2`. **Run this in a
VM where you're root, never on the production box.**

## M6: Accounting + prolog/epilog

**Mirrors:** slurmdbd + MariaDB, `sacct`, `sshare`, `sync_slurm_users.sh`,
`AccountingStorageEnforce=associations`, `prolog.sh`, `epilog.sh`, and
`gpu_notify.sh`'s `setsid -f` trick.

**Build:** `msslurmdbd` backed by SQLite, `mssacct` with the same columns as
`sacct -X -o JobID,AllocTRES`, and usage decay on the stored usage
(feeding M1).

## M7: GPU isolation, SPIT's 4 layers

The layers are:
1. Device permissions 0660.
2. A per-job ACL set in the prolog.
3. A cgroup device filter.
4. Revoking the ACL in the epilog.

On cgroup v2, layer 3 is an **eBPF program**
(`BPF_PROG_TYPE_CGROUP_DEVICE`). Write it as raw BPF instructions from C++,
the way Slurm's `cgroup_v2.c` does. Use NVML to map index ↔ PCI bus ID ↔
`/dev/nvidiaN` minor.

**Done when:** inside a job, opening the other GPU fails with `EPERM`.

## M8: GPU + NUMA deep dive

Write CUDA C++ microbenchmarks: host↔device bandwidth, P2P between the two
GPUs, and NUMA-local versus remote CPU binding. Run them on the real GPUs as
a **normal user job** (`sbatch`), like any student. Then make M4's allocator
bind a job's CPUs to its GPU's socket, and measure the gain.

## M9: Observability + shadow mode

Build an embedded HTTP server that serves Prometheus text format: queue
depth, per-user usage, NVML GPU stats, and scheduler loop latency. Then build
**shadow mode**: replay the anonymised `sacct` history through M1 and M4 in a
discrete-event simulation and compare with what Slurm actually did. That lets
you answer *"what if the GPU weight were 80?"* or *"what does a 3-day half-life
do to student waits?"* with data, before the next internal vote. That's useful
to the cluster team, and it stands out on a systems-engineering application.

## M10: Multi-node

Run the node daemon on 2–3 VMs and add heartbeats, `SlurmdTimeout`, node
states (IDLE, MIXED, ALLOCATED, DRAIN, DOWN), `ReturnToService`, separate
login and compute nodes, and an MPI hello world across them. Stretch goals:
job arrays, dependencies, `srun` steps, PMIx, preemption, and MPS or MIG.
