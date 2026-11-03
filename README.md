# minislurm

A small cluster workload manager written from scratch in **C++20**. It's
modelled on the real SPIT GPU cluster (`hpc.spit.ac.in`: Slurm 22.05, Fair
Tree FairShare, MAX_TRES billing, cgroup v2, 2× EPYC 9554, 2× RTX 6000 Ada).

The goal isn't to replace Slurm. The goal is that by the end, **every box in
the cluster architecture diagram is something you've built a small version
of yourself.** Then running the real cluster stops being "follow the KT doc"
and becomes "I know why it does that."

```
 msbatch / msqueue / msprio / msshare / mssacct   (CLI, like sbatch/squeue/sprio/sshare/sacct)
                │  socket + HMAC auth (like munge)
          mslurmctld ── scheduler: Fair Tree, decay, MAX_TRES billing, backfill
                │         └─ accounting DB (SQLite, like slurmdbd + MariaDB)
           node daemon ── prolog / epilog, GPU ACLs, eBPF device filter
                │
           msrun ── job step (like slurmstepd): fork/exec, cgroups v2, limits
                │
          /metrics ── Prometheus exporter + NVML GPU stats
```

## Status

| | |
|---|---|
| M0 recon | `recon/observe.sh` ready; run it on the cluster |
| **M1 priority engine** | ✅ built: `src/sched/`, 20 tests reproduce live `sprio`/`sshare`/billing |
| **M2 msrun** | ⏭ skeleton with 6 TODOs in `src/exec/msrun.cpp`, 13 tests waiting |
| M3–M10 | see [ROADMAP.md](ROADMAP.md) |

## Build

```bash
cmake -S . -B build && cmake --build build -j
./build/msl_tests                     # M1: 20 tests
./build/msprio-demo                   # M1: sshare -l / sprio -l / billing views
cmake --build build --target m2-test  # M2: fails until you finish the TODOs
```

Debug builds use AddressSanitizer + UBSan (`-DMSL_SANITIZE=OFF` to turn off).
Needs g++ ≥ 11 or clang ≥ 14, CMake ≥ 3.20, bash, and python3 for one test.

## Layout

```
recon/observe.sh   M0: read-only cluster recon (raw/ stays on server, anon/ leaves)
src/sched/         M1: billing, decay, Fair Tree, multifactor priority (pure math)
src/exec/          M2: msrun, the job runner you write
tools/             msprio-demo + the reconstructed spit-hpc association tree
tests/             a tiny test framework, unit tests, the msrun shell tests, priority vectors
NOTES.md           your lab notebook: questions per milestone
ROADMAP.md         M0–M10
```

## Ground rules (read these twice)

1. **Never run minislurm daemons as root on hpc.spit.ac.in.** Build and break
   things on your own Linux VM. On the real cluster you only *observe*,
   using `recon/observe.sh`, which is read-only.
2. Never touch `/dev/watchdog*` on the cluster: it reboots the box 10
   minutes later.
3. Job history and `sshare` output are other students' data. Only the
   anonymised folder leaves the server.
4. Before you ever run something of yours on the real box, get Varun's or
   Dr. Kalbande's OK, and use a maintenance reservation.

## Dev setup

- M1–M4: any Linux (a VM, WSL2, or a dual boot).
- M5 onward: a Linux VM where you're root, with cgroup v2. Rocky Linux 9
  matches the cluster: `stat -fc %T /sys/fs/cgroup` should print `cgroup2fs`.
- M8: CUDA work runs on the real GPUs as a **normal user job** (`sbatch`), like any other student.

## Why C++ and not C

Slurm itself is C, and the kernel interface is C (syscalls take `int` and
`char*`), so minislurm talks to it directly. There are no wrapper
libraries, because the point is learning. C++ is used where it pays off:

- RAII, so fds, cgroups and locks can't leak.
- `std::` containers for the scheduler.
- Strong types instead of `void*`.

The one place to write C-style code is inside signal handlers
(`signal-safety(7)`).
