# Scheduler test vectors (for Milestone 4)

Every number here comes from Varun's scheduling brief (corrected 15 Oct 2026), which
checked them against the real `sprio`/`sshare`/`sacct` on hpc.spit.ac.in.
Your priority code has to reproduce all of them. Then (M8) you check it against live `sprio` yourself.

## Billing (`TRESBillingWeights="CPU=1.0,Mem=0.92G,GRES/gpu=112"` + `PriorityFlags=MAX_TRES`)
billing = floor(max(cpus*1.0, mem_GB*0.92, gpus*112))

| cpus | mem GB | gpus | billing |
|-----:|-------:|-----:|--------:|
| 32 | 80  | 1 | 112 |
| 32 | 128 | 1 | 117 |
| 8  | 16  | 1 | 112 |
| 32 | 64  | 0 | 58  |
| 2  | 8   | 0 | 7   |
| 8  | 32  | 2 | 224 |

## JobSize (W=1000, one node, 224 CPUs, PriorityFavorSmall=No)
points = round(1000 * 0.5 * (1 + cpus/224))

| cpus | points |
|----:|----:|
| 1 | 502 |
| 8 | 518 |
| 32 | 571 |
| 112 | 750 |
| 224 | 1000 |

## Age (W=1000, PriorityMaxAge = 7 days = 10080 min, counted from *eligible* time)
points = 1000 * min(1, minutes_eligible / 10080)

| pending | points |
|---|---:|
| 60 min | 5.95 |
| 1 day | 142.9 |
| 3.5 days | 500 |
| 7 days | 1000 |
| 10 days | 1000 |

## Usage decay (PriorityDecayHalfLife = 7 days)
U_eff = U * 0.5^(days/7)

| days | U=1000 → |
|---:|---:|
| 1 | 905.7 |
| 3 | 743.0 |
| 7 | 500.0 |
| 14 | 250.0 |
| 28 | 62.5 |

## Fair Tree
- LevelFS = S / U at each level (S = shares/sibling shares, U = usage/sibling usage).
- Walk the tree depth-first, siblings in descending LevelFS; users get ranks in visit order (ties share a rank).
- FairShare factor = rank / N_users. With N = 29: last user 1/29 → 3448 points, top user → 100000.
- From live `sshare -l`: hpc_admin LevelFS ≈ 3.1959e5, hpc_researcher 1.303290, hpc_student 0.309338.
  So every researcher outranks every student.
  (Check: student account EffectvUsage 0.808177 with share 0.25 → 0.25/0.808177 = 0.3093.)

## End to end (live job 1900)
student, lowest rank (1 of 29), 32 CPUs + 1 GPU, ~14 h pending
→ FAIRSHARE 3448 + AGE 85 + JOBSIZE 571 = **PRIORITY 4104**
