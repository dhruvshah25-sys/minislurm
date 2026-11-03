# minislurm

A small cluster workload manager written from scratch in C. It's modelled on the real
SPIT GPU cluster (`hpc.spit.ac.in`: Slurm 22.05, Fair Tree FairShare, cgroup v2, 2× RTX 6000 Ada).

The goal isn't to replace Slurm. The goal is that by the end, **every box in the cluster
architecture diagram is something you've built a small version of yourself.** Then
running the real cluster stops being "follow the KT doc" and becomes "I know why it does that."

```
 tssub / tsq / tsprio / tsshare / tsacct      (CLI, like sbatch/squeue/sprio/sshare/sacct)
                │  TCP + HMAC auth (like munge)
           tsctld  ── scheduler (Fair Tree, decay, MAX_TRES billing, backfill)
                │       └─ accounting DB (SQLite, like slurmdbd + MariaDB)
           tsd     ── node daemon (like slurmd): prolog / epilog, GPU ACLs
                │
           tsrun   ── job step (like slurmstepd): fork/exec, cgroups v2, limits
                │
           /metrics ── Prometheus exporter + NVML GPU stats
```

## Ground rules (read these twice)

1. **Never run minislurm daemons as root on hpc.spit.ac.in.** Build and break things on your
   own Linux VM. On the real cluster you only *observe*, using `recon/observe.sh`, which is read-only.
2. Never touch `/dev/watchdog*` on the cluster (KT §19.7: it reboots itself 10 minutes later).
3. Job history and `sshare` output are other students' data. Only the anonymised folder leaves the server.
4. Before you ever run something of yours on the real box (M9), get Varun's or Dr. Kalbande's OK, and use a maintenance reservation.

## Dev setup

- M1: any Linux (a VM, WSL2, or a dual boot). Needs `gcc`, `make`, `python3`.
- M2 onward: a Linux VM where you're root, with cgroup v2. Rocky Linux 9 matches the cluster:
  `stat -fc %T /sys/fs/cgroup` should print `cgroup2fs`.
- M7: CUDA work runs on the real GPUs as a **normal user job** (`sbatch`/`srun`), like any other student.

## Roadmap

Each milestone has the same structure: **Learn** (CS:APP chapter + man pages) → **Build** →
**Compare** (find the matching code in Slurm's source, which is open on GitHub at `SchedMD/slurm`, tag `slurm-22-05-11-1`) →
**Observe** (look at the same thing live on the cluster).

| # | Build | You learn | CS:APP | Real-cluster counterpart |
|---|---|---|---|---|
| **M0** | Run `recon/observe.sh`, answer the questions in `NOTES.md` | What's actually on the box | – | everything |
| **M1** | `tsrun`: run one job with a time limit, KillWait, output redirection, MaxRSS | processes, signals, races, dup2 | 8, 10 | `slurmstepd`, `KillWait=30`, job-end email |
| **M2** | Put each job in a cgroup v2: `cpuset`, `memory.max`, `pids.max`, an `OUT_OF_MEMORY` state, kill-by-cgroup (fixes the XFAIL test) | cgroups, VM and RSS vs page cache, namespaces | 9 | `proctrack/cgroup`, `task/cgroup`, `CR_Core_Memory` |
| **M3** | `tsctld` + `tsd` + CLI over TCP, a binary protocol, HMAC auth, a job state machine, state saved to disk and reloaded after a crash | sockets, concurrency (epoll or threads), serialisation | 11, 12 | slurmctld :6817, slurmd :6818, munge |
| **M4** | Scheduler: multifactor priority, Fair Tree, 7-day decay, MAX_TRES billing, then backfill. Must match `tests/priority_vectors.md` exactly | algorithms on trees, floating point traps (CS:APP 2.4!) | 2.4 | `priority/multifactor`, `fair_tree.c`, `sched/backfill` |
| **M5** | Accounting in SQLite, `tsacct`/`tsshare`/`tsprio`, prolog/epilog hooks (including the `setsid -f` trick) | persistence, fork in daemons, process groups for real | 8 | slurmdbd + MariaDB, `prolog.sh`/`epilog.sh`, `gpu_notify.sh` |
| **M6** | GPU isolation like SPIT's 4 layers: device permissions, per-job ACL in prolog, cgroup device filter (eBPF on cgroup v2), revoke in epilog | Linux permissions, ACLs, eBPF basics | 10 | `fix-gpu-lockdown.sh`, `ConstrainDevices` |
| **M7** | GPU deep dive: CUDA bandwidth + P2P tests between the two GPUs, NUMA-aware CPU binding (bind a job's CPUs to its GPU's socket), and measure the speedup | PCIe, NUMA, CUDA basics, why `nvidia-smi` index ≠ Slurm index | 6, 9 | `gres.conf`, `nvidia-smi topo -m`, `CUDA_DEVICE_ORDER` |
| **M8** | Exporter `/metrics` (text format) + NVML, then **shadow mode**: replay the anonymised `sacct` history through your scheduler and compare its decisions with real Slurm's | observability, discrete-event simulation | – | Prometheus :9092/:9400, Grafana |
| **M9** | Multi-node: run `tsd` on 2–3 VMs, separate login/compute, MPI hello world across them | real HPC topology | 11 | what SPIT would look like with more nodes |

M8's shadow mode is where this stops being a toy. Being able to say *"I replayed 3 months
of our cluster's jobs; with backfill tuned like X, median student wait drops from A to B"* is
useful to the cluster team, and it's exactly the kind of thing that stands out in a systems-engineering application.

## Milestone 1, right now

```bash
make          # builds build/tsrun from src/tsrun.c (a skeleton with 6 TODOs)
make test     # 12 tests + 1 expected-fail; all 12 fail until you do the TODOs
```

Work through TODO 1 to 6 in `src/tsrun.c` in order. Each one lists the CS:APP section and the man pages it needs,
plus questions. Write your answers in `NOTES.md`, because explaining them is half the learning.
You're done when it says `passed 12, failed 0, expected-fail 1`.
Then run `make asan` so the tests also pass under the address and undefined-behaviour sanitizers.

The one expected fail (test 13) is on purpose: a child that calls `setsid()` escapes your process group
and survives. The cluster's own `gpu_notify.sh` uses `setsid -f` for exactly this reason (KT §9.8).
That's the reason Milestone 2 exists.
