#!/usr/bin/env bash
# Milestone 0 - READ-ONLY reconnaissance of hpc.spit.ac.in.
#
# Run it ON the cluster, as yourself:   bash observe.sh
# It only reads things. It does not restart, edit, drain, lock or kill anything,
# and it never touches /dev/watchdog* (KT doc 19.7: that reboots the box).
#
# Output: ~/minislurm-recon/<timestamp>/
#   raw/     - everything, with real usernames. STAYS ON THE SERVER.
#   anon/    - same files with usernames replaced by user001, user002 ...
#              Only ever copy anon/ off the cluster: it's other students' data.
#
# Run it a few times on different days (busy evening vs quiet morning).
set -u
TS=$(date +%Y%m%d-%H%M%S)
OUT=~/minislurm-recon/$TS
RAW=$OUT/raw; ANON=$OUT/anon
mkdir -p "$RAW" "$ANON"
chmod 700 ~/minislurm-recon "$OUT"

cap() {               # cap <file> <command...>   save output, never abort
  local f=$1; shift
  { echo "\$ $*"; "$@" 2>&1; } > "$RAW/$f" || true
}
capsh() { local f=$1; shift; { echo "\$ $*"; bash -c "$*" 2>&1; } > "$RAW/$f" || true; }

echo "[1/6] scheduler state"
cap sinfo.txt          sinfo -N -l
cap squeue.txt         squeue -l
cap sprio.txt          sprio -l
cap sprio_weights.txt  sprio -w
cap sshare.txt         sshare -l -a
cap scontrol_config.txt scontrol show config
cap scontrol_node.txt  scontrol show node
cap scontrol_part.txt  scontrol show partition
cap reservations.txt   scontrol show reservation
cap sdiag.txt          sdiag

echo "[2/6] job history since Aug 1 (needs sudo for all users)"
capsh sacct_history.psv "sudo sacct -a -X -P --starttime=2026-08-01 \
  --format=JobID,User,Account,Partition,Submit,Eligible,Start,End,Elapsed,Timelimit,State,ExitCode,ReqCPUS,ReqMem,AllocTRES%80,NNodes"

echo "[3/6] slurm config + hook scripts (read only)"
for f in slurm.conf cgroup.conf gres.conf slurmdbd.conf; do
  [ "$f" = slurmdbd.conf ] && continue     # holds the DB password - don't copy it
  capsh "etc_$f" "sudo cat /etc/slurm/$f"
done
capsh slurm_scripts_ls.txt "sudo ls -la /etc/slurm/scripts/"
for s in prolog.sh epilog.sh epilog_slurmctld.sh task_prolog.sh; do
  capsh "script_$s" "sudo cat /etc/slurm/scripts/$s"
done

echo "[4/6] hardware + topology"
cap lscpu.txt          lscpu
cap lscpu_ext.txt      lscpu -e
capsh numa.txt         "numactl -H 2>/dev/null || cat /sys/devices/system/node/node*/cpulist"
cap nvidia_topo.txt    nvidia-smi topo -m
cap nvidia_q.txt       nvidia-smi -q
capsh nvidia_pci.txt   "cat /proc/driver/nvidia/gpus/*/information; for d in /sys/bus/pci/devices/0000:01:00.0 /sys/bus/pci/devices/0000:61:00.0; do echo \$d numa_node=\$(cat \$d/numa_node) local_cpus=\$(cat \$d/local_cpulist); done"
capsh dev_nvidia.txt   "ls -l /dev/nvidia*; getfacl /dev/nvidia0 /dev/nvidia1 2>/dev/null"
cap meminfo.txt        cat /proc/meminfo
cap df.txt             df -hT
capsh mdstat.txt       "cat /proc/mdstat"

echo "[5/6] cgroups (how slurm isolates jobs)"
capsh cgroup.txt "stat -fc %T /sys/fs/cgroup; cat /sys/fs/cgroup/cgroup.controllers; \
  ls /sys/fs/cgroup/system.slice/ | grep -i slurm; \
  find /sys/fs/cgroup/system.slice/slurmstepd.scope -maxdepth 3 -type d 2>/dev/null | head -50"
capsh cgroup_job_limits.txt 'for d in /sys/fs/cgroup/system.slice/slurmstepd.scope/job_*; do [ -d "$d" ] || continue; echo "== $d"; for f in cpuset.cpus.effective cpuset.mems.effective memory.max memory.current memory.peak pids.current cgroup.procs; do printf "%-24s " $f; tr "\n" " " < $d/$f 2>/dev/null; echo; done; done'

echo "[6/6] services + timers"
capsh services.txt "systemctl is-active slurmctld slurmd slurmdbd munge mariadb sssd httpd ondemand-dex prometheus grafana-server"
capsh ports.txt    "ss -ltnp 2>/dev/null | head -60"
capsh cron.txt     "ls /etc/cron.d/; sudo crontab -l -u root"

# ---- anonymise: every Slurm user -> userNNN (map stays in raw/) ----
sacctmgr -nP show assoc format=User 2>/dev/null | sort -u | grep -v '^$' \
  | awk '{printf "%s user%03d\n", $1, NR}' > "$RAW/_usermap.txt"
sedexpr=$(awk '{printf "s/\\<%s\\>/%s/g;", $1, $2}' "$RAW/_usermap.txt")
for f in "$RAW"/*; do
  b=$(basename "$f"); [ "$b" = _usermap.txt ] && continue
  sed -e "$sedexpr" "$f" > "$ANON/$b"
done
# home paths and email addresses can still leak names: blank them
sed -i -E 's#/(home|data|scratch)/[a-z0-9._-]+#/\1/<user>#g; s/[A-Za-z0-9._%+-]+@spit\.ac\.in/<email>/g' "$ANON"/*

echo
echo "done -> $OUT"
echo "copy off the server ONLY the anon/ folder, e.g. from your laptop:"
echo "  scp -r <you>@hpc.spit.ac.in:$ANON ./recon-$TS"
echo "skim anon/ by eye once before copying - anonymising with sed is best-effort."
