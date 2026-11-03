// msrun: M2 of minislurm.  YOU write this one.
//
// Runs ONE job on THIS machine, the way slurmstepd does on hpc.spit.ac.in:
//   - start the job in its own process group
//   - send its stdout+stderr to --output (like #SBATCH --output)
//   - enforce --time: SIGTERM the whole group, wait --kill-wait seconds,
//     then SIGKILL (Slurm's KillWait; SPIT runs KillWait=30)
//   - turn SIGINT/SIGTERM sent to msrun into a cancel (like scancel)
//   - report state, exit code:signal, elapsed time and MaxRSS
//     (the same fields as the job-end email gpu_notify.sh sends)
//
// Final line on stderr, exactly this format (the tests parse it):
//   msrun: state=<STATE> exit=<code>:<signal> elapsed=<sec>s maxrss_kb=<kb>
// STATE is one of COMPLETED, FAILED, TIMEOUT, CANCELLED.
//
// Read first (CS:APP 3e):
//   8.4   Process control: fork, execve, waitpid, the status macros
//   8.5   Signals; 8.5.5 writing safe handlers; 8.5.6 synchronizing to
//         avoid races; 8.5.7 explicitly waiting with sigsuspend  <- the core
//   10.9  I/O redirection with dup2
// Man pages: setpgid(2), kill(2) (what does a negative pid mean?),
//   wait4(2), getrusage(2) (ru_maxrss units on Linux), alarm(2), sigsuspend(2)
//
// On C++ here: the OS interface is C (syscalls take ints and char*), and
// that's fine. Use C++ where it actually helps: RAII so fds can't leak
// (UniqueFd below), std::string, std::optional, enum class for state.
// Signal handlers are the exception: inside one, use only
// async-signal-safe calls (signal-safety(7)). No std::cout, no new, no locks.
//
// Build: cmake --build build        Test: cmake --build build --target m2-test
#include <fcntl.h>
#include <getopt.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <utility>

namespace {

// ---------- given: small helpers ----------

double now() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) / 1e9;
}

void report(const char* state, int code, int sig, double elapsed, long maxrss_kb) {
    std::fprintf(stderr, "msrun: state=%s exit=%d:%d elapsed=%.2fs maxrss_kb=%ld\n", state, code,
                 sig, elapsed, maxrss_kb);
}

[[noreturn]] void usage() {
    std::fprintf(stderr,
                 "usage: msrun [--output FILE] [--time SEC] [--kill-wait SEC] -- cmd [args...]\n");
    std::exit(2);
}

// RAII owner of a file descriptor: closes it when it goes out of scope, so
// every early return is leak-free. Move-only (two owners = double close).
class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : fd_(fd) {}
    UniqueFd(UniqueFd&& o) noexcept : fd_(std::exchange(o.fd_, -1)) {}
    UniqueFd& operator=(UniqueFd&& o) noexcept {
        if (this != &o) { reset(); fd_ = std::exchange(o.fd_, -1); }
        return *this;
    }
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    ~UniqueFd() { reset(); }

    int get() const { return fd_; }
    explicit operator bool() const { return fd_ >= 0; }
    void reset() { if (fd_ >= 0) ::close(fd_); fd_ = -1; }

private:
    int fd_ = -1;
};

// ---------- TODO 1: signal handlers ----------
// Handlers should do almost nothing: set a flag and return (CS:APP 8.5.5, G0-G3).
// You need to know about three events: child changed state (SIGCHLD),
// time limit hit (SIGALRM), user cancelled (SIGINT or SIGTERM to msrun).
// Q: why volatile sig_atomic_t and not plain int? (and why not std::atomic<int>?)
// C++20 note: `a = b = 0;` and `flag++` on volatiles are deprecated
// (-Wvolatile). Write one plain assignment per flag.
volatile std::sig_atomic_t got_chld = 0, got_alrm = 0, got_cancel = 0;
// void on_chld(int) { ... }

}  // namespace

int main(int argc, char** argv) {
    const char* output = nullptr;
    int time_limit = 0;  // seconds, 0 = unlimited
    int kill_wait = 2;   // seconds between SIGTERM and SIGKILL

    // ---------- given: argument parsing ----------
    static const option opts[] = {{"output", required_argument, nullptr, 'o'},
                                  {"time", required_argument, nullptr, 't'},
                                  {"kill-wait", required_argument, nullptr, 'k'},
                                  {nullptr, 0, nullptr, 0}};
    int c;
    // leading '+' = stop at the first non-option, so the job's own flags are left alone
    while ((c = getopt_long(argc, argv, "+o:t:k:", opts, nullptr)) != -1) {
        switch (c) {
            case 'o': output = optarg; break;
            case 't': time_limit = std::atoi(optarg); break;
            case 'k': kill_wait = std::atoi(optarg); break;
            default: usage();
        }
    }
    if (optind < argc && std::strcmp(argv[optind], "--") == 0) optind++;
    if (optind >= argc) usage();
    char** job_argv = &argv[optind];

    (void)output; (void)time_limit; (void)kill_wait; (void)job_argv;
    (void)got_chld; (void)got_alrm; (void)got_cancel;

    // ---------- TODO 2: open the output file BEFORE forking ----------
    // Hold it in a UniqueFd. If open fails (e.g. the directory doesn't
    // exist: the most common failed job on the SPIT cluster), print
    //     msrun: cannot open output '<path>': <strerror>
    // then report("FAILED", 1, 0, 0, 0) and return 1. No job is started.
    // Q: why is O_CLOEXEC a good idea on this fd?

    // ---------- TODO 3: install handlers and BLOCK the signals ----------
    // Use sigaction, not signal(). Block SIGCHLD/SIGALRM/SIGINT/SIGTERM
    // *before* fork, keep the old mask, and only unblock inside sigsuspend.
    // Q: what race happens if the child exits before you start waiting
    //    and SIGCHLD was not blocked? (CS:APP fig 8.40 vs 8.41)

    // ---------- TODO 4: fork ----------
    // Child:
    //   - setpgid(0,0): become leader of a new process group
    //   - reset the handlers to SIG_DFL and restore the old signal mask
    //     Q: which of these survive execve and which don't? (signal(7))
    //   - dup2 the output fd onto 1 and 2
    //   - execvp; if it returns, print why and _exit(127)
    //     Q: why _exit and not exit in the child? (C++ twist: what would
    //        exit() do with static destructors the parent also owns?)
    // Parent:
    //   - also call setpgid(pid, pid)   Q: why do BOTH sides call it?
    //   - start the clock, arm alarm(time_limit) if set

    // ---------- TODO 5: the wait loop ----------
    // Loop:
    //   - wait4(pid, &status, WNOHANG, &ru): if the child is done, break
    //   - otherwise sigsuspend(&old_mask) until a flag is set
    //   - on timeout or cancel (first time): remember TIMEOUT/CANCELLED,
    //     kill(-pid, SIGTERM), then arm alarm(kill_wait)
    //   - if the alarm fires again while still waiting: kill(-pid, SIGKILL)
    // After the loop: SIGKILL the group once more so background children
    // of the job don't outlive it (test 10).

    // ---------- TODO 6: decode status and report ----------
    // WIFEXITED/WEXITSTATUS -> code, WIFSIGNALED/WTERMSIG -> signal.
    // State: TIMEOUT or CANCELLED if you forced it, else COMPLETED if
    // code==0 && signal==0, else FAILED. maxrss_kb = ru.ru_maxrss.
    // Return 0 for COMPLETED, 1 otherwise.

    report("FAILED", 0, 0, now() * 0, 0);  // placeholder - delete me
    return 1;
}
