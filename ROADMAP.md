# minislurm roadmap

Each milestone has four parts:

- **Mirrors:** the piece of the SPIT cluster it rebuilds.
- **Learn:** the systems concepts it teaches.
- **Read:** what to read first.
- **Done when:** a check you can run, ideally compared with the real cluster.

The order follows CS:APP: M2 uses chapters 8 and 10, M3 uses 11 and 12, and
M5 uses 9.

---

## M1: Priority engine ✅

**Mirrors:** `priority/multifactor`, `PriorityDecayHalfLife=7-0`, Fair Tree,
`TRESBillingWeights` + `MAX_TRES`, `sprio`, `sshare`.

**Learn:** how a scheduler turns "fairness" into one number, and how
floating-point truncation shows up in operator-visible output (`sprio`
columns don't add up to PRIORITY).

**Done:** the tests reproduce the live `sprio -l` output (4104 / 4103 / 4082),
the billing for every job shape in the brief (112 / 117 / 58 / 7), and the
account-level LevelFS (1.3033 / 0.3093).

**Your exercises (do these before M2, they're short):**
1. Run `sshare -l -P` on the cluster, write a small parser that loads it into
   `AssocTree`, and check that every user's FairShare matches. Any mismatch
   means my model of Fair Tree is wrong somewhere, and finding out where is
   the lesson.
2. Implement the classic formula (`PriorityFlags=NO_FAIR_TREE`):
   `F = 2^(-U/S)`. Compare the two algorithms on the same tree. Which
   students gain and which lose?
3. Look up how Slurm defines FairShare for a user with 0 shares, and check
   that this code agrees.

---

## M2: Job executor (`msstepd`) ⏭ NEXT

**Mirrors:** `slurmstepd`, the process that actually runs your batch script
on the node. `sbatch --output=slurm-%j.out`, `--time`, `scancel`,
`KillWait=30`, job exit codes in `sacct`.

**Build:** a library `src/exec/` and a tool `msrun <script> [--time=SECONDS]`:
1. `fork()` + `execve()` the script with `/bin/bash`.
2. Put the child in its own session and process group (`setsid()`) so you can
   signal the whole tree: `kill(-pgid, sig)`.
3. Redirect stdout and stderr to `slurm-<jobid>.out` (`open`, `dup2`, close
   everything else: no fd leaks into user jobs).
4. Set the environment the way Slurm does: `SLURM_JOB_ID`,
   `SLURM_CPUS_ON_NODE`, `SLURM_SUBMIT_DIR`, `TMPDIR`.
5. Time limit: when it expires send `SIGTERM`, wait `KillWait` (30 s), then
   send `SIGKILL`. Use `signalfd` or `pidfd_open` with `poll` rather than
   `sleep` loops.
6. Collect the result with `wait4()`: exit code, signal, and `rusage` (peak
   RSS = MaxRSS in sacct, CPU time).
7. Handle `SIGINT` to `msrun` itself as `scancel`.

**Learn:** process control, signals and the races around them, file
descriptors, why a job's children can escape a naive kill (that's why
Slurm uses cgroups for process tracking, which comes in M5).

**Read:** CS:APP ch. 8 (Exceptional Control Flow), all of it, especially
8.5 on signals. CS:APP ch. 10 (System-Level I/O). `man 7 signal`,
`man 2 pidfd_open`.

**Done when:**
- A script that runs `sleep 1000 & sleep 1000 & wait` is fully killed at the
  time limit, with no orphans left (check with `ps -ef`).
- A script that traps SIGTERM gets 30 s and then dies by SIGKILL.
- The exit status is reported as `exit:signal` like `sacct`'s `ExitCode`
  (`0:0`, `1:0`, `0:9`).
- Tests cover all of the above. `valgrind --track-fds=yes` shows no leaked fds.

---

## M3: Controller + CLI over sockets

**Mirrors:** `slurmctld`, `sbatch`, `squeue`, `scancel`, `sprio`.

**Build:** `mslurmctld` daemon holding the job queue; CLIs that talk to it
over a Unix socket with a length-prefixed binary protocol you design
(header: version, msg type, length, auth). An `epoll` event loop and a
`#SBATCH` directive parser. Job states PENDING → RUNNING → COMPLETED,
FAILED, CANCELLED, TIMEOUT. Wire M1's priority into `msqueue`'s ordering.

**Learn:** sockets, framing, partial reads and writes, event loops versus
threads, protocol versioning.

**Read:** CS:APP ch. 11 (Network Programming), ch. 12 (Concurrent
Programming). Beej's Guide to Network Programming. `man 7 epoll`.

**Done when:** `msbatch job.sh; msqueue; mscancel <id>` works with many
concurrent clients, and killing a client mid-message doesn't hurt the daemon.

---

## M4: Resource allocation + scheduling loop + backfill

**Mirrors:** `select/cons_tres`, `CR_Core_Memory`, `OverSubscribe=FORCE:4`,
`DefMemPerCPU=4000`, `DefCpuPerGPU=8`, `sched/backfill`, `squeue` reasons
`(Priority)`, `(Resources)`, `(PartitionTimeLimit)`, `(ReqNodeNotAvail)`,
maintenance reservations.

**Build:** a node model of 2 sockets × 56 cores × 2 threads as core bitmaps,
memory, and 2 GPUs. An allocator that picks cores (socket-packed), a main
scheduler (highest priority job that fits), and backfill: lower-priority jobs
may start if they end before the top job's reserved start time. Reservations.

**Learn:** bitmaps, bin packing, why backfill needs accurate time limits, and
time-based planning.

**Done when:** you replay a week of real `sacct` history (M9's simulator
starts here) and get plausible start times. A job asking for 8 days shows
`(PartitionTimeLimit)` and never starts.

---

## M5: Isolation: cgroups v2 + GPU lockdown

**Mirrors:** `proctrack/cgroup`, `task/cgroup`, `ConstrainDevices`, the
prolog `setfacl` on `/dev/nvidiaN`, the epilog revoke, `CUDA_VISIBLE_DEVICES`,
and the trap where Slurm GPU 0 = PCI 61:00.0 = `nvidia-smi` index 1.

**Build:** per-job cgroup v2 (`cpuset.cpus`, `memory.max`, `pids.max`), with
jobs launched straight into it (`clone3` with `CLONE_INTO_CGROUP`). Kill the
whole job via `cgroup.kill`. Device control on cgroup v2 is an **eBPF
program** (`BPF_PROG_TYPE_CGROUP_DEVICE`): write it as raw BPF instructions
in C++, the way Slurm does. Use NVML to map GPU index ↔ PCI bus ID ↔ minor
number. Prolog and epilog hooks.

**Learn:** virtual memory and OOM behaviour, how the kernel enforces limits,
eBPF basics, and the PCI device model.

**Read:** CS:APP ch. 9 (Virtual Memory). Kernel docs `admin-guide/cgroup-v2`.
Slurm's `cgroup_v2.c` and its eBPF device program.

**Done when:** inside a job, opening the other GPU gives `EPERM`; a fork bomb
hits `pids.max`; a memory hog is OOM-killed inside its cgroup only; M2's
orphan test passes without process-group tricks. **Use a VM with a
passthrough GPU or an emulated device node, never the production box.**

---

## M6: Accounting + crash safety

**Mirrors:** `slurmdbd` + MariaDB, `sacct`, `sshare`, `StateSaveLocation`,
the hourly `sync_slurm_users.sh` (associations), and
`AccountingStorageEnforce=associations`.

**Build:** `msslurmdbd` backed by SQLite (job records, associations, usage).
The controller persists its state with write-ahead logging and `fsync`, then
survives `kill -9` and restarts with running jobs still known.

**Learn:** durability, fsync semantics, crash-consistent file formats, and
schema design.

**Done when:** `kill -9 mslurmctld` mid-workload, restart, and nothing is lost.
`mssacct` output has the same columns as the real `sacct -X -o JobID,AllocTRES`.

---

## M7: Multi-node: node daemon, heartbeats, auth

**Mirrors:** `slurmd`, `SlurmdTimeout=300`, node states (IDLE, MIXED,
ALLOCATED, DRAIN, DOWN), `ReturnToService=1`, and munge (the KT says the
munge key has only an offline copy, and now you'll know why it matters).

**Build:** split controller and node daemon. Use 3 VMs or containers as
nodes. HMAC-signed credentials with a shared key. Heartbeats and failure
detection. Drain and resume.

**Learn:** distributed failure detection, clocks, authentication, and idempotent RPCs.

---

## M8: Observability

**Mirrors:** `node_exporter :9100`, `dcgm :9400`, the slurm exporter `:9092`,
and the Grafana dashboards.

**Build:** an embedded HTTP server serving Prometheus text format (queue
depth, per-user usage, GPU utilisation via NVML, scheduler loop latency).
Point the real Prometheus at your VM and build a dashboard.

---

## M9: Trace-driven policy simulator (useful to the admin team)

**Build:** feed `sacct -P` history from the real cluster into M1 and M4 in a
discrete-event simulation. Answer questions like *"what if GPU weight were
80 instead of 112?"* or *"what does a 3-day half-life do to student wait
times?"* with data, before the internal vote.

---

## M10: Stretch

Job arrays and dependencies, `srun` steps, MPI launch (PMIx), preemption,
GPU sharing (MPS/MIG), a C++ REST API, and a CUDA benchmark job that
measures what the RTX 6000 Adas actually deliver.
