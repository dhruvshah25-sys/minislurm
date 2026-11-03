/*
 * tsrun - Milestone 1 of minislurm.
 *
 * Runs ONE job on THIS machine, the way slurmstepd does on hpc.spit.ac.in:
 *   - start the job in its own process group
 *   - send its stdout+stderr to --output (like #SBATCH --output)
 *   - enforce --time: SIGTERM the whole group, wait --kill-wait seconds,
 *     then SIGKILL (Slurm's KillWait; SPIT runs KillWait=30)
 *   - turn SIGINT/SIGTERM sent to tsrun into a cancel (like scancel)
 *   - report state, exit code:signal, elapsed time and MaxRSS
 *     (the same fields as the job-end email gpu_notify.sh sends)
 *
 * Final line on stderr, exactly this format (the tests parse it):
 *   tsrun: state=<STATE> exit=<code>:<signal> elapsed=<sec>s maxrss_kb=<kb>
 * STATE is one of COMPLETED, FAILED, TIMEOUT, CANCELLED.
 *
 * Read first (CS:APP 3e):
 *   8.4   Process control: fork, execve, waitpid, the status macros
 *   8.5   Signals; 8.5.5 writing safe handlers; 8.5.6 synchronizing to
 *         avoid races; 8.5.7 explicitly waiting with sigsuspend  <- the core
 *   10.9  I/O redirection with dup2
 * Man pages: setpgid(2), kill(2) (what does a negative pid mean?),
 *   wait4(2), getrusage(2) (ru_maxrss units on Linux), alarm(2), sigsuspend(2)
 *
 * Build: make      Test: make test
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ---------- given: small helpers ---------- */

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static void report(const char *state, int code, int sig, double elapsed, long maxrss_kb)
{
    fprintf(stderr, "tsrun: state=%s exit=%d:%d elapsed=%.2fs maxrss_kb=%ld\n",
            state, code, sig, elapsed, maxrss_kb);
}

static void usage(void)
{
    fprintf(stderr,
        "usage: tsrun [--output FILE] [--time SEC] [--kill-wait SEC] -- cmd [args...]\n");
    exit(2);
}

/* ---------- TODO 1: signal handlers ----------
 * Handlers should do almost nothing: set a flag and return (CS:APP 8.5.5, G0-G3).
 * You need to know about three events: child changed state (SIGCHLD),
 * time limit hit (SIGALRM), user cancelled (SIGINT or SIGTERM to tsrun).
 * Q: why volatile sig_atomic_t and not plain int?
 */
static volatile sig_atomic_t got_chld, got_alrm, got_cancel;
/* static void on_chld(int sig) { ... } */

int main(int argc, char **argv)
{
    const char *output = NULL;
    int time_limit = 0;   /* seconds, 0 = unlimited */
    int kill_wait = 2;    /* seconds between SIGTERM and SIGKILL */

    /* ---------- given: argument parsing ---------- */
    static struct option opts[] = {
        {"output",    required_argument, 0, 'o'},
        {"time",      required_argument, 0, 't'},
        {"kill-wait", required_argument, 0, 'k'},
        {0, 0, 0, 0}};
    int c;
    /* leading '+' = stop at the first non-option, so the job's own flags are left alone */
    while ((c = getopt_long(argc, argv, "+o:t:k:", opts, NULL)) != -1) {
        switch (c) {
        case 'o': output = optarg; break;
        case 't': time_limit = atoi(optarg); break;
        case 'k': kill_wait = atoi(optarg); break;
        default: usage();
        }
    }
    if (optind < argc && strcmp(argv[optind], "--") == 0) optind++;
    if (optind >= argc) usage();
    char **job_argv = &argv[optind];

    (void)output; (void)time_limit; (void)kill_wait; (void)job_argv;
    (void)got_chld; (void)got_alrm; (void)got_cancel;

    /* ---------- TODO 2: open the output file BEFORE forking ----------
     * If it fails (e.g. the directory doesn't exist - the most common
     * failed job on the SPIT cluster), print
     *     tsrun: cannot open output '<path>': <strerror>
     * then report("FAILED", 1, 0, 0, 0) and exit 1. No job is started.
     * Q: why is O_CLOEXEC a good idea on this fd?
     */

    /* ---------- TODO 3: install handlers and BLOCK the signals ----------
     * Use sigaction, not signal(). Block SIGCHLD/SIGALRM/SIGINT/SIGTERM
     * *before* fork, keep the old mask, and only unblock inside sigsuspend.
     * Q: what race happens if the child exits before you start waiting
     *    and SIGCHLD was not blocked? (CS:APP fig 8.40 vs 8.41)
     */

    /* ---------- TODO 4: fork ----------
     * Child:
     *   - setpgid(0,0): become leader of a new process group
     *   - reset the handlers to SIG_DFL and restore the old signal mask
     *     Q: which of these survive execve and which don't? (signal(7))
     *   - dup2 the output fd onto 1 and 2
     *   - execvp; if it returns, print why and _exit(127)
     *     Q: why _exit and not exit in the child?
     * Parent:
     *   - also call setpgid(pid, pid)   Q: why do BOTH sides call it?
     *   - start the clock, arm alarm(time_limit) if set
     */

    /* ---------- TODO 5: the wait loop ----------
     * Loop:
     *   - wait4(pid, &status, WNOHANG, &ru): if the child is done, break
     *   - otherwise sigsuspend(&old_mask) until a flag is set
     *   - on timeout or cancel (first time): remember TIMEOUT/CANCELLED,
     *     kill(-pid, SIGTERM), then arm alarm(kill_wait)
     *   - if the alarm fires again while still waiting: kill(-pid, SIGKILL)
     * After the loop: SIGKILL the group once more so background children
     * of the job don't outlive it (test 10).
     */

    /* ---------- TODO 6: decode status and report ----------
     * WIFEXITED/WEXITSTATUS -> code, WIFSIGNALED/WTERMSIG -> signal.
     * State: TIMEOUT or CANCELLED if you forced it, else COMPLETED if
     * code==0 && signal==0, else FAILED. maxrss_kb = ru.ru_maxrss.
     * Exit 0 for COMPLETED, 1 otherwise.
     */

    report("FAILED", 0, 0, now() * 0, 0); /* placeholder - delete me */
    return 1;
}
