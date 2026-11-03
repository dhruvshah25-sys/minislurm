#!/usr/bin/env bash
# Milestone 1 tests for tsrun.  Run:  make test   (or  TSRUN=./build/tsrun tests/test_m1.sh)
# Each test runs tsrun and checks the final report line it prints on stderr:
#   tsrun: state=<STATE> exit=<code>:<signal> elapsed=<sec>s maxrss_kb=<kb>
set -u
TSRUN=${TSRUN:-./build/tsrun}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0; fail=0; xfail=0

ok()   { echo "  PASS  $1"; pass=$((pass+1)); }
bad()  { echo "  FAIL  $1"; echo "        got: $2"; fail=$((fail+1)); }
report_line() { grep '^tsrun: state=' "$1" | tail -1; }
field() { sed -n "s/.* $1=\([^ ]*\).*/\1/p" <<<"$2"; }        # field exit "<line>"
state() { sed -n 's/^tsrun: state=\([A-Z_]*\).*/\1/p' <<<"$1"; }
elapsed_lt() { awk -v e="${1%s}" -v m="$2" 'BEGIN{exit !(e<m)}'; }
elapsed_ge() { awk -v e="${1%s}" -v m="$2" 'BEGIN{exit !(e>=m)}'; }

run() { "$TSRUN" "$@" 2>"$TMP/err" >"$TMP/out"; report_line "$TMP/err"; }

echo "== Milestone 1: tsrun =="
[ -x "$TSRUN" ] || { echo "no binary at $TSRUN (run make first)"; exit 1; }

# 1. success
l=$(run -- true)
[[ $(state "$l") == COMPLETED && $(field exit "$l") == 0:0 ]] && ok "true -> COMPLETED 0:0" || bad "true -> COMPLETED 0:0" "$l"

# 2. plain failure
l=$(run -- false)
[[ $(state "$l") == FAILED && $(field exit "$l") == 1:0 ]] && ok "false -> FAILED 1:0" || bad "false -> FAILED 1:0" "$l"

# 3. exit code is preserved
l=$(run -- sh -c 'exit 3')
[[ $(state "$l") == FAILED && $(field exit "$l") == 3:0 ]] && ok "exit 3 -> FAILED 3:0" || bad "exit 3 -> FAILED 3:0" "$l"

# 4. killed by a signal (segfault) -> exit code 0, signal 11
l=$(run -- sh -c 'kill -SEGV $$')
[[ $(state "$l") == FAILED && $(field exit "$l") == 0:11 ]] && ok "SIGSEGV -> FAILED 0:11" || bad "SIGSEGV -> FAILED 0:11" "$l"

# 5. command not found -> 127 (what bash does too)
l=$(run -- /no/such/binary)
[[ $(state "$l") == FAILED && $(field exit "$l") == 127:0 ]] && ok "missing binary -> FAILED 127:0" || bad "missing binary -> FAILED 127:0" "$l"

# 6. time limit: polite job dies on SIGTERM
l=$(run --time 1 -- sleep 5)
e=$(field elapsed "$l")
[[ $(state "$l") == TIMEOUT ]] && elapsed_lt "$e" 2.5 && ok "--time 1 sleep 5 -> TIMEOUT in ~1s" || bad "--time 1 sleep 5 -> TIMEOUT in ~1s" "$l"

# 7. time limit: stubborn job ignores SIGTERM, must get SIGKILL after kill-wait (Slurm's KillWait)
l=$(run --time 1 --kill-wait 1 -- sh -c 'trap "" TERM; sleep 10')
e=$(field elapsed "$l")
[[ $(state "$l") == TIMEOUT ]] && elapsed_ge "$e" 1.8 && elapsed_lt "$e" 3.5 \
  && ok "SIGTERM-ignoring job -> SIGKILL after kill-wait" || bad "SIGTERM-ignoring job -> SIGKILL after kill-wait" "$l"

# 8. --output captures stdout AND stderr of the job
l=$(run --output "$TMP/job.out" -- sh -c 'echo to-stdout; echo to-stderr >&2')
grep -q to-stdout "$TMP/job.out" 2>/dev/null && grep -q to-stderr "$TMP/job.out" \
  && ok "--output captures stdout+stderr" || bad "--output captures stdout+stderr" "$(cat "$TMP/job.out" 2>&1)"

# 9. the #1 real SPIT failure (KT doc 9.7): --output into a dir that doesn't exist
l=$(run --output "$TMP/nope/job.out" -- true)
[[ $(state "$l") == FAILED ]] && grep -q "cannot open output" "$TMP/err" \
  && ok "missing output dir -> clear error, FAILED" || bad "missing output dir -> clear error, FAILED" "$(cat "$TMP/err")"

# 10. children of the job die with it (process group)
l=$(run --time 1 -- sh -c 'sleep 31.7 & sleep 31.7')
sleep 0.3
if [[ $(state "$l") != TIMEOUT ]]; then bad "background child killed with the job" "job didn't run/time out: $l"
elif pgrep -f "^sleep 31.7$" >/dev/null; then bad "background child killed with the job" "leftover: $(pgrep -af "^sleep 31.7$")"; pkill -f "^sleep 31.7$"
else ok "background child killed with the job"; fi

# 11. scancel: SIGTERM to tsrun cancels the job
"$TSRUN" -- sleep 30 2>"$TMP/err" & tp=$!
sleep 0.5; kill -TERM $tp; wait $tp 2>/dev/null
l=$(report_line "$TMP/err")
[[ $(state "$l") == CANCELLED ]] && ok "SIGTERM to tsrun -> CANCELLED" || bad "SIGTERM to tsrun -> CANCELLED" "$l"

# 12. MaxRSS (the number in SPIT's job-end email)
if command -v python3 >/dev/null; then
  l=$(run -- python3 -c 'b=bytearray(200*1024*1024); b[::4096]=b"x"*len(b[::4096])')
  r=$(field maxrss_kb "$l")
  [[ -n "$r" && "$r" -ge 200000 ]] && ok "maxrss_kb reports ~200 MB job" || bad "maxrss_kb reports ~200 MB job" "$l"
fi

# 13. KNOWN LIMITATION (expected to fail until Milestone 2):
#     a child that calls setsid() leaves your process group and survives the kill.
#     SPIT's own gpu_notify.sh uses `setsid -f` for exactly this reason (KT doc 9.8).
l=$(run --time 1 -- sh -c 'setsid sleep 32.3 & sleep 30')
sleep 0.3
if [[ $(state "$l") != TIMEOUT ]]; then echo "  SKIP  setsid escapee (job didn't run/time out yet)"
elif pgrep -f "^sleep 32.3$" >/dev/null; then
  echo "  XFAIL setsid escapee survived -> this is why Slurm uses proctrack/cgroup. Fixed in M2."
  xfail=$((xfail+1)); pkill -f "^sleep 32.3$"
else ok "setsid escapee killed (you already did M2!)"; fi

echo
echo "passed $pass, failed $fail, expected-fail $xfail"
[ $fail -eq 0 ]
