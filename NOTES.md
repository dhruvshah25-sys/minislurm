# NOTES

Your lab notebook. Answer in your own words, one or two lines each, and cite the file or command that proved it.

## M0: the real cluster (from recon/ output + the KT docs)
1. Which NUMA node is each GPU on (PCI 01:00.0 and 61:00.0)? Which CPU numbers are local to each?
2. Slurm's GPU 0 is PCI 61:00.0, but nvidia-smi calls it index 1. Why do they disagree? (hint: `CUDA_DEVICE_ORDER`, `gres.conf`)
3. Pick one running job. Find its cgroup. What are its `cpuset.cpus.effective` and `memory.max`? Does `memory.max` match what the job asked for?
4. Read `prolog.sh`. Exactly which command grants GPU access? What does Slurm do to the node if the prolog exits non-zero?
5. Take one line of `sprio -l` and recompute its priority by hand.
6. In `sshare -l`, check that every user's FairShare is k/29. Which rank are you?
7. Trace one `sbatch` from your terminal to a MariaDB row: which daemons, which ports, which scripts, in order.
8. `OverSubscribe=FORCE:4` but `CR_Core_Memory`: what can be shared between jobs, and what never can?
9. What happens to running jobs if slurmctld restarts? What if slurmd restarts?
10. `/dev/nvidia*` is still 0666 (KT §17.2). As a plain SSH user, what could you do that you shouldn't be able to? (Answer this, but don't try it.)

## M1: priority engine (src/sched/, already built; your job is to verify it)
1. Load the real `sshare -l -P` (from recon/anon) into `AssocTree`. Does every FairShare match? If not, where does my Fair Tree model differ from `fair_tree.c` in slurm-22-05-11-1?
2. Why does job 1901 show PRIORITY 4103 when its columns add up to 4102? Explain it using CS:APP 2.4 (floating point → integer conversion).
3. Does `sprio` truncate or round each column? Find a pending job with 8 CPUs (517.86) or read `src/sprio/print.c`. Fix `Priority::*_col()` if I got it wrong.
4. With Fair Tree, can a student ever outrank a researcher? What would have to change in `sshare`?
5. Before 2026-10-15, why did GPU hogs look like light users? Use `compute_billing` with `BillingWeights{}` to show it.

## M2: msrun
- Why `volatile sig_atomic_t`? Why isn't `std::atomic<int>` the textbook answer here?
- The SIGCHLD race and how blocking + sigsuspend fixes it:
- Why do both parent and child call setpgid?
- What survives execve: handlers, ignored signals, the mask?
- Why `_exit` in the child? What would `exit()` run in a C++ program that `_exit` skips?
- Why O_CLOEXEC on the output fd?
- What does `kill(-pid, sig)` do?
- Units of `ru_maxrss` on Linux:
