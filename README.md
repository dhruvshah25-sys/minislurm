# minislurm

A workload manager written from scratch in C++20, modelled on the Slurm 22.05
setup that runs the **SPIT GPU cluster** (`hpc.spit.ac.in`: 2× EPYC 9554,
224 threads, 2× RTX 6000 Ada, Fair Tree FairShare, MAX_TRES billing).

Building minislurm is how I'm learning that cluster: each milestone rebuilds
one piece of the real stack and is checked against what the real cluster
does (`sprio`, `sshare`, `sacct`, `squeue`, cgroups, the prolog and epilog).

```
 msbatch / msqueue / mscancel / msprio / msshare      <- CLI tools        (M3)
                     │  unix/TCP socket, binary protocol
               mslurmctld                              <- controller       (M3, M4, M6)
      priority engine · scheduler · backfill · state
          │                               │
     msslurmd  (per node)            msslurmdbd + SQLite  <- accounting     (M6)
   msstepd: fork/exec, cgroups,                         
   GPU isolation, prolog/epilog      <- executor (M2), isolation (M5), multi-node (M7)
```

## Status

| Milestone | What | State |
|---|---|---|
| M1 | Priority engine: billing, decay, Fair Tree, multifactor | ✅ reproduces live `sprio`/`sshare` |
| M2 | Job executor (`msstepd`): fork/exec, I/O, signals, time limits | ⏭ next |
| M3–M10 | see [ROADMAP.md](ROADMAP.md) | |

## Build

```bash
cmake -S . -B build && cmake --build build -j
./build/msl_tests        # 20 tests, expected values come from the live cluster
./build/msprio-demo      # sshare -l / sprio -l / billing views
```

Debug builds use AddressSanitizer + UBSan by default (`-DMSL_SANITIZE=OFF` to disable).

## Layout

```
src/sched/      M1: pure scheduling math (no syscalls)
  billing.*       TRESBillingWeights + MAX_TRES
  decay.*         PriorityDecayHalfLife
  fairtree.*      Fair Tree: LevelFS, ranking, tie merging
  priority.*      multifactor priority, age, job size
tools/          small executables + the reconstructed spit-hpc scenario
tests/          tiny no-dependency test framework + tests
```

## Ground rules

- **Never experiment on the production box.** It is the login node, compute
  node and storage server for ~3000 portal users at once. Develop in a VM;
  on the real cluster run only read-only commands (`sprio`, `sshare`,
  `sacct`, `scontrol show`) to compare results.
- **Never open `/dev/watchdog*`** on the cluster: doing so arms a 10-minute
  reboot (KT open issue #3).
- No dependencies unless a milestone needs one (SQLite in M6, NVML in M5).
  Writing it yourself is the point.
